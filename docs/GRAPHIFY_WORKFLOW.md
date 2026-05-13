# Graphify Workflow

This project keeps a persistent knowledge graph so future agents can recover context fast.

## Run Graphify On This Repo

From repository root:

```bash
/graphify .
```

Outputs are written to:

- `graphify-out/graph.json`
- `graphify-out/GRAPH_REPORT.md`
- `graphify-out/graph.html`
- `graphify-out/obsidian/`

## Incremental Updates

After code/docs changes:

```bash
/graphify . --update
```

## Why This Matters Here

Terminal projects evolve across multiple layers (frontend widget, parser/emulator, PTY backend, platform adapters). Graphify gives agents durable memory of:

- module responsibilities
- unresolved architecture decisions
- cross-file dependencies
- roadmap progress

That reduces repeated re-discovery every session.
