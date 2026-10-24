# Vex RAG MCP

Local stdio MCP server for indexing files and searching with vector, BM25, or hybrid retrieval.

## Requirements

- 384-dimensional `llama.cpp` embedding model (default search paths include `models/bge-small-en-v1.5.gguf`).
- SQLite with FTS5 and `sqlite-vec`.
- `pdftotext` for PDF ingestion.

Run from the project directory; relative paths use the working directory.

## Configuration

- `VEX_EMB_MODEL`: embedding model path. If unset, the server checks its configured model paths. A model is required at startup, including for BM25 searches.
- `VEX_RAG_DB`: database path. If unset, an existing `./.vex/session.db` is used; otherwise the server falls back to `./memory.db`. The database's parent directory must already exist.

## Tools

- `insert_doc`: indexes a file at `path` under `title` and `domain`. Chunks are up to 200 words with 30-word overlap; PDFs are extracted with `pdftotext`.
- `search_docs`: searches `query` with `HYBRID` (default), `VECTOR`, or `BM25`. `limit` defaults to 5; each retrieval method considers up to 20 candidates.
- `delete_doc` is advertised, but its tool-call handler is not implemented.

Re-indexing may leave stale or incomplete chunks. The server provides no document-level access control; restrict MCP and filesystem access in the host.