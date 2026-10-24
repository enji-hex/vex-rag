#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <sqlite3.h>
#include <mutex>
#include <unistd.h>

#include "lib/nlohmann/json.hpp"
#include "lib/llama.cpp/include/llama.h"

enum class SearchMode {
    VECTOR,
    BM25,
    HYBRID
};


// Shared Struct
struct ScoredChunk {
    std::string title;
    std::string domain;
    std::string content;
    double rrf_score;
};

class TextChunker {
public:
    static std::vector<std::string> chunk_string(const std::string& text, size_t max_words = 300, size_t overlap = 50);
    static std::vector<std::string> parse_and_chunk_pdf(const std::string& filepath, size_t max_words, size_t overlap);
    static std::vector<std::string> chunk_file(const std::string& path, size_t max_words, size_t overlap);
};

class LLamaEmb {
private:
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    size_t ctx_size = 512;
    std::mutex mtx;
    const int emb_dim = 384;

    LLamaEmb();

public:
    ~LLamaEmb();
    LLamaEmb(const LLamaEmb&) = delete;
    LLamaEmb& operator=(const LLamaEmb&) = delete;

    static LLamaEmb& getInst();
    std::vector<float> compute(const std::string& text);
};

// Function Prototypes (Declarations only, no bodies here!)
std::vector<int64_t> search_bm25(sqlite3* db, const std::string& query, std::map<int64_t, ScoredChunk>& chunk_map);
std::vector<int64_t> search_vector(sqlite3* db, const std::vector<float>& query_embedding);
std::vector<int64_t> search_by_chunk_id(sqlite3* db, std::vector<int64_t> chunk_ids, std::map<int64_t, ScoredChunk>& chunk_map);
std::vector<ScoredChunk> hybrid_search(sqlite3* db, const std::string& query, const std::vector<float>& query_embedding, int limit,SearchMode smode);


// -- helpers
typedef const char* cstring; // const string (const char*)

// ENV variable TOP priority 
// checkes all provided pathes vector by priority
inline const char* get_path(
    cstring env_name,
    std::vector<cstring> pathes
){
    cstring env_path = std::getenv(env_name);
    if (env_path != nullptr && env_path[0] != '\0'){
        return env_path;
    }
    for (auto &p : pathes){
        if (access(p,F_OK) == 0)
            return p;
    }
    return "";
}