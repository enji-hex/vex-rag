#include "inc/base.hpp"

using std::string;
using std::cerr;
using std::endl;

LLamaEmb::LLamaEmb() {
    llama_backend_init();
    llama_model_params mparams = llama_model_default_params();
    mparams.n_gpu_layers = 99;

    std::vector<cstring> model_pathes = {
        "models/bge-small-en-v1.5.gguf",
        "/usr/share/models/bge-small-en-v1.5.gguf",
        "/usr/local/share/models/bge-small-en-v1.5.gguf"
    };
    cstring model_path = get_path("VEX_EMB_MODEL",model_pathes);
    cerr << "Selected model path " << model_path << endl;
    model = llama_model_load_from_file(model_path, mparams);
    if (!model) {
        throw std::runtime_error("failed to load llama.cpp");
    }

    llama_context_params cparams = llama_context_default_params();
    
    cparams.n_ctx       = ctx_size;
    cparams.n_batch     = ctx_size;
    cparams.n_ubatch    = ctx_size;
    cparams.embeddings = true;
    cparams.pooling_type = LLAMA_POOLING_TYPE_MEAN;

    ctx = llama_init_from_model(model, cparams);
    if (!ctx) {
        llama_model_free(model);
        throw std::runtime_error("failed to create llama.cpp context");
    }
}

LLamaEmb::~LLamaEmb() {
    if (ctx) llama_free(ctx);
    if (model) llama_model_free(model);
    llama_backend_free();
}

LLamaEmb& LLamaEmb::getInst() {
    static LLamaEmb inst;
    return inst;
}

std::vector<float> LLamaEmb::compute(const std::string& text) {
    std::scoped_lock<std::mutex> lock(mtx);
    const llama_vocab* vocab = llama_model_get_vocab(model);
    if (!vocab) {
        throw std::runtime_error("failed to get llama vocab");
    }

    // Allocate a vector large enough to hold text.size() * 2 + 1 to prevent overflow
    int max_tokens = text.size() + 128;
    std::vector<llama_token> tokens(max_tokens);

    // llama_tokenize returns the number of tokens written (or required if buffer is too small)
    int n_tokens = llama_tokenize(vocab, text.c_str(), text.size(), tokens.data(), tokens.size(), true, true);
    
    if (n_tokens < 0) {
        // If it returned negative, it means our buffer was too small; resize and retry
        tokens.resize(-n_tokens);
        n_tokens = llama_tokenize(vocab, text.c_str(), text.size(), tokens.data(), tokens.size(), true, true);
        if (n_tokens < 0) {
            throw std::runtime_error("failed tokenize text for embedding: " + std::to_string(n_tokens));
        }
    }

    tokens.resize(n_tokens);

    // Safety truncate for bge-small-en-v1.5 (max 512 tokens)
    if (tokens.size() > ctx_size) {
        tokens.resize(ctx_size);
        n_tokens = ctx_size;
    }

    llama_batch_ext* batch = llama_batch_ext_init(ctx);
    llama_batch_ext_clear(batch);

    for (int32_t i = 0; i < n_tokens; ++i) {
        int32_t idx = llama_batch_ext_add_token(batch, 0, tokens[i]);
        if (idx < 0) {
            llama_batch_ext_free(batch);
            throw std::runtime_error("Failed to add token to batch_ext. Error code: " + std::to_string(idx));
        }

        llama_pos pos = i;
        llama_batch_ext_set_pos(batch, idx, &pos);
        llama_batch_ext_set_output_logits(batch, idx, true);
    }

    if (llama_process(ctx, LLAMA_PROCESS_TYPE_ENCODE, batch) != 0) {
        llama_batch_ext_free(batch);
        throw std::runtime_error("llama_process failed during embedding encoding.");
    }

    const float* emb_data = llama_get_embeddings_seq(ctx, 0);
    if (emb_data == nullptr) {
        emb_data = llama_get_embeddings(ctx);
    }

    int n_emb = llama_model_n_embd(model);
    std::vector<float> result(emb_data, emb_data + n_emb);
    llama_batch_ext_free(batch);
    return result;
}