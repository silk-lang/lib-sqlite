# Development

Use Silk's toolchain and native manifest support for builds, formatting, tests, and documentation.
Use build hooks when the manifest cannot express a build step. Do not use Python scripts unless
the Silk toolchain and hooks are insufficient. Windows SDK preparation lives in
`scripts/prepare-windows.ps1`.

```sh
silk use
silk format --check
silk check
silk test --root src/tests.silk
silk build
silk doc -o build/doc.json
```

Keep the `db-sqlite` package name and `sqlite` / `sys` exports compatible with existing consumers.
SQLite's source URL, SHA-256, and feature flags are pinned in `silk.toml`.

<!-- codebase-memory-mcp:start -->
# Codebase Knowledge Graph (codebase-memory-mcp)

Prefer MCP graph tools over grep/glob for code discovery: `search_graph`, `trace_path`,
`get_code_snippet`, `query_graph`, then `get_architecture`. Fall back to direct source searches
for literals, configuration and non-code files, or when graph coverage is insufficient.
<!-- codebase-memory-mcp:end -->
