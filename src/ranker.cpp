#include "inc/base.hpp"

using std::map;
using std::pair;
using std::string;
using std::vector;

constexpr double kRRF = 60.0;

template<typename T>
T
get_col(sqlite3_stmt* stmt, int col);

template<>
inline int64_t
get_col<int64_t>(sqlite3_stmt* stmt, int col)
{
    return sqlite3_column_int64(stmt, col);
}

template<>
inline string
get_col<string>(sqlite3_stmt* stmt, int col)
{
    const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
    return val ? std::string(val) : std::string("");
}



vector<int64_t>
search_semantic([[maybe_unused]] sqlite3* db,
                sqlite3_stmt* stmt,
                map<int64_t, ScoredChunk>& chunk_map)
{
    vector<int64_t> ids;
    // execute query
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        int64_t cid = get_col<int64_t>(stmt, 0);
        chunk_map[cid] = { get_col<string>(stmt, 1), // title
                           get_col<string>(stmt, 3), // domain (col 3)
                           get_col<string>(stmt, 2), // content (col 2)
                           0.0 };
        ids.push_back(cid);
    }
    return ids;
}

vector<int64_t>
search_bm25(sqlite3* db, const std::string& query, map<int64_t, ScoredChunk>& chunk_map)
{
    sqlite3_stmt* stmt = nullptr;
    vector<int64_t> bm25_ids;
    if (query.empty())
        return bm25_ids;

    // compile query, set limit globally later
    bool ok = (sqlite3_prepare_v2(
                 db,
                 R"(SELECT rowid, title, content, domain 
                 FROM docs WHERE docs MATCH ?1 
                 ORDER BY bm25(docs) ASC LIMIT 20;)",
                 -1,
                 &stmt,
                 nullptr) == SQLITE_OK);
    if (!ok)
        return bm25_ids;

    sqlite3_bind_text(stmt, 1, query.c_str(), -1, SQLITE_STATIC);
    auto ids= search_semantic(db, stmt, chunk_map);
    sqlite3_finalize(stmt);
    return ids;
}

vector<int64_t>
search_by_chunk_id(sqlite3* db, vector<int64_t> chunk_ids, map<int64_t, ScoredChunk>& chunk_map)
{
    sqlite3_stmt* stmt = nullptr;
    vector<int64_t> ids;
    bool ok =(sqlite3_prepare_v2(db,
                          "SELECT rowid, title, content, domain FROM docs WHERE rowid = ?1;",
                          -1,
                          &stmt,
                          nullptr) == SQLITE_OK);
    if (!ok)
        return ids;

    for (int64_t cid : chunk_ids) {
        if (chunk_map.find(cid) == chunk_map.end()) {
            sqlite3_bind_int64(stmt, 1, cid);
            auto res = search_semantic(db, stmt, chunk_map);
            ids.insert(ids.end(), res.begin(), res.end());
            sqlite3_reset(stmt);
        }
    }
    sqlite3_finalize(stmt);
    return ids;
}

vector<int64_t>
search_vector(sqlite3* db, const vector<float>& query)
{
    vector<int64_t> vector_ids;
    sqlite3_stmt* stmt = nullptr;

    bool ok =
      sqlite3_prepare_v2(
        db,
        R"(SELECT chunk_id FROM doc_vecs 
        WHERE embedding MATCH vec_f32(?1) 
        ORDER BY distance LIMIT 20;)",
        -1,
        &stmt,
        nullptr) == SQLITE_OK;
    if (!ok)
        return vector_ids;

    sqlite3_bind_blob(stmt, 1, query.data(), query.size() * sizeof(float), SQLITE_STATIC);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        vector_ids.push_back(get_col<int64_t>(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return vector_ids;
}

std::vector<ScoredChunk>
hybrid_search(sqlite3* db,
              const std::string& query,
              const std::vector<float>& query_embedding,
              int limit,
              SearchMode smode)
{
    map<int64_t, ScoredChunk> chunk_map;
    map<int64_t, double> rrf_score;

    // lambda function, which CAN use scope variables captured as references (rrf_score)
    auto add_rank_scores = [&](const vector<int64_t>& results) {
        int rank = 1;
        for (int64_t chunk_id : results) {
            rrf_score[chunk_id] += 1.0 / (kRRF + rank);
            rank++;
        }
    };

    // in Both modes we populates chunk_map
    if (smode != SearchMode::VECTOR) {
        // all cases except VECTOR
        vector<int64_t> bm25_ids = search_bm25(db, query, chunk_map);
        add_rank_scores(bm25_ids);
    }
    if (smode != SearchMode::BM25){
        vector<int64_t> vec_ids  = search_vector(db, query_embedding);
        add_rank_scores(vec_ids);
        // collect data for vector results
        search_by_chunk_id(db, vec_ids, chunk_map);
    }

    vector<ScoredChunk> final_res;
    for (auto& [id, score] : rrf_score) {
        if (chunk_map.count(id)) {
            ScoredChunk sc = chunk_map[id];
            sc.rrf_score = score;
            final_res.push_back(sc);
        }
    }

    std::sort(final_res.begin(), final_res.end(), [](const auto& a, const auto& b) {
        return a.rrf_score > b.rrf_score;
    });
    if (final_res.size() > static_cast<size_t>(limit)) {
        final_res.resize(limit);
    }

    return final_res;
}