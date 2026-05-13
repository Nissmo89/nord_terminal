# NordTerminal

`NordTerminal` is an open-source Qt 6 terminal widget project for **NORD_C** and any other IDE/editor that needs an embeddable, cross-platform terminal.

It follows a strict module split:

- `TerminalSession`: shell process + PTY backend boundary
- `TerminalEmulator`: terminal state machine and screen model
- `TerminalWidget`: Qt paint/input frontend

This repository is intentionally in early-stage scaffold mode. The current code compiles as a library and demonstrates the integration flow while we incrementally replace placeholders with a full PTY + `libvterm` backend.

## Current State

- Qt 6 widget library target: `NordTerminal`
- Basic demo app: `nord_terminal_demo`
- POSIX PTY session backend (`forkpty`) for Linux/macOS
- UTF-8 text rendering and extended SGR color support (16/256/truecolor)
- JSON theme loader with a Nord theme sample
- Keyboard mapping, screen model, scrollback, selection skeleton
- Architecture and roadmap docs for contributors

## Build

```bash
cmake -S . -B build
cmake --build build -j
```

Run demo:

```bash
./build/nord_terminal_demo
```

## Theme JSON Format

Theme files live in `themes/` and follow this shape:

```json
{
  "id": "nord-dark",
  "name": "Nord Dark",
  "font": { "family": "JetBrains Mono", "size": 11 },
  "colors": {
    "background": "#2E3440",
    "foreground": "#D8DEE9",
    "cursor": "#ECEFF4",
    "selection": "#4C566A",
    "ansi": ["#3B4252", "... 16 items total ..."]
  }
}
```

## Documentation

- [Architecture.md](/home/nord/code_base/nord_terminal/Architecture.md)
- [Architecture Map](/home/nord/code_base/nord_terminal/docs/ARCHITECTURE_MAP.md)
- [Roadmap](/home/nord/code_base/nord_terminal/docs/ROADMAP.md)
- [Graphify Workflow](/home/nord/code_base/nord_terminal/docs/GRAPHIFY_WORKFLOW.md)
- [Contributing](/home/nord/code_base/nord_terminal/CONTRIBUTING.md)

## Important Note

`TerminalSession` now uses a real PTY backend on Unix-like systems, which is required for interactive programs (`vim`, `nvim`, `htop`, etc.). Terminal emulation is still a custom ANSI parser and remains the next major upgrade target (`libvterm`) for full VT compatibility.
