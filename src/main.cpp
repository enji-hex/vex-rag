#include "inc/base.hpp"
#include "param.cpp"

using json = nlohmann::json;
using std::cerr;
using std::endl;

extern "C" {
    int sqlite3_vec_init(sqlite3 *db, char **pzErrMsg, const struct sqlite3_api_routines *pApi);
}

template <typename T>
void bind_arg(sqlite3_stmt* stmt, int idx, T&& val) {
    if constexpr (std::is_integral_v<std::decay_t<T>>) {
        sqlite3_bind_int64(stmt, idx, static_cast<sqlite3_int64>(val));
    } else {
        sqlite3_bind_text(stmt, idx, std::string(val).c_str(), -1, SQLITE_TRANSIENT);
    }
}

template <typename... Args>
bool exec_stmt(sqlite3* db, const char* sql, Args&&... args) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    int idx = 1;
    (bind_arg(stmt,idx++,std::forward<Args>(args)),...);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

class DocEngine {
   private:
    sqlite3* db;
    void init_schema() {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "SELECT vec_version()", -1, &stmt, nullptr) == SQLITE_OK &&
            sqlite3_step(stmt) != SQLITE_ROW) {
            cerr << "Failed to get sqlite version: " << sqlite3_errmsg(db) << endl;
        }

        const char* sql_meta = R"(
            CREATE TABLE IF NOT EXISTS doc_hashes (
            title TEXT NOT NULL,
            hash TEXT NOT NULL,
            PRIMARY KEY (title,hash)
            );
        )";
        const char* sql_fts = R"(
            CREATE VIRTUAL TABLE IF NOT EXISTS docs USING fts5(
            title,
            content,
            domain,
            tokenize = 'porter ascii'
            );
        )";
        const char* sql_vec = R"(
            CREATE VIRTUAL TABLE IF NOT EXISTS doc_vecs USING vec0(
                chunk_id INTEGER PRIMARY KEY,
                embedding float[384] -- 384 dimensions
            );
        )";
        char* err_msg = nullptr;
        for (auto &&sql : {sql_meta,sql_fts,sql_vec})
        {
            if (sqlite3_exec(db, sql, nullptr, nullptr, &err_msg) != SQLITE_OK) {
                cerr << "Schema Init Err: " << (err_msg ? err_msg : "") << endl;
                sqlite3_free(err_msg);
            } 
        }
    }

   public:
    DocEngine(cstring db_path) {
        sqlite3_initialize();
        sqlite3_auto_extension(reinterpret_cast<void(*)()>(sqlite3_vec_init));
        if (sqlite3_open(db_path, &db) != SQLITE_OK) {
            cerr << "Failed to open DB" << endl;
            return;
        }
        sqlite3_exec(db, "PRAGMA journal_mode = WAL;", nullptr, nullptr, nullptr);
        init_schema();
    }
    ~DocEngine() {
        if (db) sqlite3_close(db);
    }

    std::vector<std::string> get_docs(){
        std::vector<std::string> titles;
        sqlite3_stmt* stmt;
        sqlite3_prepare_v2(db,"SELECT title FROM docs;",-1,&stmt,nullptr);
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            titles.push_back(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0)));
        }
        return titles;
    }

    bool delete_doc(
        const std::string& title
    ){
        return exec_stmt(db,
            "DELETE FROM doc_vecs "
            "WHERE chunk_id IN (SELECT rowid FROM docs WHERE title = ?1);",title)
            && exec_stmt(db,"DELETE FROM docs WHERE title = ?1",title)
            && exec_stmt(db,"DELETE FROM doc_hashes WHERE title = ?1",title);
    }

    bool insert_doc(
        const std::string& title, 
        const std::string& content,
        const std::string& domain) 
    {
        if (!db || content.empty()) return false;
        auto emb = LLamaEmb::getInst().compute(content);
        if (emb.size() != 384) return false;
        
        if (sqlite3_exec(db,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr) != SQLITE_OK) 
            return false;

        auto rollback = [&]{
            sqlite3_exec(db,"ROLLBACK",nullptr,nullptr,nullptr);
            return false;
        };

        const auto hash = std::to_string(std::hash<std::string>{}(content));
        // Delete document on conflict
        bool hash_exists = exec_stmt(db, 
            "INSERT INTO doc_hashes(title,hash) VALUES (?1,?2);", title,hash) == false;
        if (hash_exists) delete_doc(title);
        
        if (!exec_stmt(db, "INSERT INTO docs(title, content, domain) VALUES (?1, ?2, ?3);",                                                                                                                                                                          
                    title, content, domain)){
            return rollback();
        }
        
        
        const auto chunk_id = sqlite3_last_insert_rowid(db);
        sqlite3_stmt* stmt = nullptr;
        

        // insert in vector table
        if (sqlite3_prepare_v2(
            db, "INSERT INTO doc_vecs(chunk_id, embedding) VALUES (?1, ?2);", 
            -1, &stmt, nullptr) != SQLITE_OK){
            return rollback();
        };
        
        
        bool ok = 
            sqlite3_bind_int64(stmt, 1, chunk_id)  == SQLITE_OK &&
            sqlite3_bind_blob(stmt, 2, emb.data(), 
                static_cast<int>(emb.size() * sizeof(float)), 
                SQLITE_STATIC) == SQLITE_OK &&
            sqlite3_step(stmt) == SQLITE_DONE;

        sqlite3_finalize(stmt);
        if (!ok) return rollback();
        if (sqlite3_exec(db,"COMMIT",nullptr,nullptr,nullptr) != SQLITE_OK)
            return rollback();
        return true;
    }

    json search(const std::string& query, int limit,const std::string mode) {
        json results = json::array();
        if (limit < 0) return results;

        SearchMode smode = SearchMode::HYBRID;
        if (mode == "BM25") smode = SearchMode::BM25;
        if (mode == "VECTOR") smode = SearchMode::VECTOR;

        std::vector<float> query_emb;
        if (smode != SearchMode::BM25) {
            query_emb = LLamaEmb::getInst().compute(query);
        }
        auto mathches  = hybrid_search(db,query,query_emb,limit,smode);
        
        for (auto & it : mathches){
            results.push_back({
                {"title",it.title},
                {"domain",it.domain},
                {"content",it.content},
                {"score",it.rrf_score},
            });
        }
        return results;
    }
};

json handle_req(const json& req,DocEngine& engine){
    std::string method = req.value("method","");
    json id = req.contains("id") ? req["id"] : nullptr;

    json res = {
        {"jsonrpc","2.0"},
        {"id",id}
    };

    if (method == "initialize"){
        res["result"] = {
            {"protocolVersion","2024-11-05"},
            {"capabilities", {{"tools", json::object()}}},
            {"serverInfo",{{"name","vex-rag"},{"version","1.0.0"}}}
        };
    } else if (method == "tools/list") {
        Tool search_tool = Tool {
            .name = "search_docs",
            .description = "Run FTS5 text search over indexed documents",
            .required = {"query"},
            .params = {
                Param{.name = "query",.desc = "Search query term"},
                Param{
                    .name ="mode",.desc="search mode: VECTOR | BM25 | HYBRID",
                    .enum_list = {"VECTOR", "BM25", "HYBRID"}
                },
                Param{.name="limit",.desc="Count of returned chunks",.type="integer"}
            }
        };
        Tool insert_tool = Tool {
            .name = "insert_doc",
            .description = "Insert a raw document snippet or chunk into FTS5 index",
            .required = {"title", "path","domain"},
            .params = {
                Param{.name = "title",.desc = "Document title"},
                Param{.name ="path",.desc="Filepath for a document"},
                Param{.name ="domain",.desc="Category or tag",}
            }
        };
        Tool delete_doc = Tool {
            .name = "delete_doc",
            .description = "Delete document by its title",
            .required = {"title"},
            .params = {
                Param{.name = "title",.desc = "Document title"},
            }
        };
        
        // add del_docs later
        res["result"] = {
            {"tools", json::array({
                to_mcp_json(search_tool),
                to_mcp_json(insert_tool),
            })}
        };

    } else if (method == "tools/call") {
        std::string tool_name = req["params"].value("name","");
        json args = req["params"].value("arguments",json::object());

        if (tool_name == "search_docs") {
            json matches = engine.search(args.value("query",""),args.value("limit",5),args.value("mode","HYBRID"));
            res["result"] = {
                {"content",json::array({
                    {
                        {"type","text"},
                        {"text",matches.dump(2)}
                    }
                })}
            };
        } else if (tool_name == "insert_doc") {
            std::vector<std::string> chunks = TextChunker::chunk_file(
                args.value("path","").c_str(),
                200,
                30
            );
            std::string path = args.value("path", "");
            std::string title = args.value("title", "");
            std::string domain = args.value("domain", "general");
            
            int added = 0;
            for (size_t i = 0; i < chunks.size(); i++){
                if (engine.insert_doc(title, chunks[i],domain))
                    added++;
            };
            res["result"] = {
                {"content",json::array({
                    {
                        {"type","text"},
                        {"text", "Indexed " + std::to_string(added) + " chunks from " + title}
                    }
                })}
            };
        } else {
            res["error"] = {{"code",-32601},{"message","Tool not found"}};
        }
    } else {
        res["error"] = {{"code",-32601},{"message","Method not found"}};
    }
    return res;
}


int main() {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    cstring db_path = get_path("VEX_RAG_DB",{"./.vex/session.db"});
    if (db_path == nullptr || db_path[0] == '\0')
        db_path = "./memory.db";
    cerr << "Selected db_path " << db_path << endl;
    DocEngine engine(db_path);
    LLamaEmb::getInst();

    

    std::string line;

    while (std::getline(std::cin,line)){
        if (line.empty()) continue;
        json req = json::parse(line,nullptr,false);

        if (req.is_discarded()){
            json err_resp = json::object({
                {"jsonrpc", "2.0"},
                {"id", nullptr},
                {"error", json::object({
                    {"code", -32700},
                    {"message", "Parse error: Invalid JSON format"}
                })}
            });
            cerr << err_resp.dump() << "\n" << std::flush;
        }
            
        if (!req.contains("id")) continue;
        json res = handle_req(req,engine);
        std::cout << res.dump() << "\n" << std::flush;
    }
    return 0;
}