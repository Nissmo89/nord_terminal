# Roadmap

## M0: Bootstrap (done in this commit)

- CMake + Qt6 library scaffold
- Public headers and module boundaries
- Basic widget rendering, key mapping, and theme loading
- Sample JSON theme

## M1: Real PTY Session

- Linux/macOS backend (openpty/forkpty) implemented
- Introduce `IPtyBackend` interface
- Windows backend (ConPTY via Pty-Qt/libptyqt)
- Wire resize and raw byte streaming through PTY

## M2: libvterm Emulator Core

- Wrap `VTerm` and `VTermScreen`
- Replace temporary ANSI parser
- Dirty-line tracking and cursor model from `libvterm`

## M3: Feature-Complete Interaction

- Scrollback policy and performance tuning
- Copy/paste semantics aligned with major terminals
- Alternate screen handling (`vim`, `nvim`, `htop`, `less`, `nano`) baseline implemented
- Selection improvements (double-click word, line selection)

## M4: NORD_C Integration

- IDE panel embedding API
- Terminal profile management (shell presets, env vars, cwd)
- JSON theme synchronization with NORD_C theme engine
- Multi-terminal tabs and split views
