# Contributing

Thanks for contributing to NordTerminal.

## Before You Start

- Read [Architecture.md](/home/nord/code_base/nord_terminal/Architecture.md)
- Read [docs/ARCHITECTURE_MAP.md](/home/nord/code_base/nord_terminal/docs/ARCHITECTURE_MAP.md)
- Keep separation strict:
  - `TerminalSession` talks to shell/PTY
  - `TerminalEmulator` owns terminal state interpretation
  - `TerminalWidget` handles rendering and input

## Local Workflow

1. Keep changes scoped to one module when possible.
2. Add or update docs when module responsibilities change.
3. Run graph refresh after meaningful changes:
   - `/graphify . --update`

## Coding Notes

- Qt 6 and C++20
- Public headers in `include/nordterminal/`
- Implementation in `src/terminal/`
- Theme compatibility should remain JSON-first for NORD_C integration.
