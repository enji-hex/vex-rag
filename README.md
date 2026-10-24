# Vex RAG MCP

*Local stdio MCP server for indexing files and searching with vector, BM25, or hybrid retrieval.*

<img src="./docs/example.png" width="80%">

## Requirements

- 384-dimensional `llama.cpp` embedding model (default search paths include `models/bge-small-en-v1.5.gguf`).
- SQLite with FTS5 and `sqlite-vec`.
- `pdftotext` for PDF ingestion.

Run from the project directory; relative paths use the working directory.

## Configuration

- `VEX_EMB_MODEL`: embedding model path. If unset, the server checks its [configured model paths](./src/llm.cpp#L13). A model is required at startup, including for BM25 searches.
- `VEX_RAG_DB`: database path. If unset, an existing `./.vex/session.db` is used; otherwise the server falls back to `./memory.db`. The database's parent directory must already exist.

## Tools

- `insert_doc`: indexes a file at `path` under `title` and `domain`. Replaces document on-conflict! Chunks are up to 200 words with 30-word overlap; PDFs are extracted with `pdftotext`.
- `search_docs`: searches `query` with specified `limit`(default 5). Available modes `HYBRID` (default), `VECTOR`, `BM25`. Each retrieval method considers up to 20 candidates.
- `delete_doc` [TODO] Deletes document from RAG database.
