# Windows Fixes Report

Date: 2026-05-15  
Source report reviewed: `WINDOWS_BUGS_REPORT.md`

## What Was Fixed

1. **LNM default is now standards-compatible (`off`) on all platforms**
- Changed `defaultLineFeedNewLineMode()` to always return `false`.
- File: `src/terminal/TerminalEmulator.cpp`
- Why: `xterm-256color` behavior expects LNM to be explicitly enabled with `CSI 20 h`, not enabled by default.

2. **`ED2`/`ED3` clear no longer leaks same-batch scrolled lines back into scrollback**
- Added `m_scrolledLines.clear()` for both mode 2 and mode 3 paths in `eraseInDisplay`.
- File: `src/terminal/TerminalEmulator.cpp`
- Why: avoids re-introducing pre-clear lines into scrollback after a clear in the same output batch.

3. **Removed dead `clearScreen()` code path**
- Removed declaration and implementation of `clearScreen()`.
- Files:
  - `include/nordterminal/TerminalEmulator.h`
  - `src/terminal/TerminalEmulator.cpp`
- Why: parser clear behavior is routed through `eraseInDisplay`; dead code was misleading for future maintenance.

4. **Re-enabled viewport scroll optimization on Windows**
- `allowViewportScrollOptimization` is now enabled unconditionally.
- File: `src/terminal/TerminalWidget.cpp`
- Why: keeps scroll handling behavior aligned with Linux path and prevents full-viewport repaint on every scroll frame.

5. **Scrollback clear now hard-resets viewport scroll state**
- On scrollback-clear request, we now force `m_scrollOffset = 0`.
- File: `src/terminal/TerminalWidget.cpp`
- Why: prevents stale viewport offset state after clear operations.

6. **Damage/repaint fallback is now robust**
- If a targeted dirty update cannot be computed/applied, we now always call `viewport()->update()`.
- File: `src/terminal/TerminalWidget.cpp`
- Why: avoids missed paint scenarios after scrollback/viewport transitions.

7. **Resize path now guarantees repaint**
- `recalculateGrid()` now ends with `viewport()->update()`.
- File: `src/terminal/TerminalWidget.cpp`
- Why: ensures redraw after resize-driven geometry and scrollbar updates.

## External Reference Findings

1. Microsoft Terminal keeps **LNM as an explicit mode** (`CSI 20 h/l`) and tests that behavior directly:
- https://github.com/microsoft/terminal/blob/main/src/host/ut_host/ScreenBufferTests.cpp
- https://github.com/microsoft/terminal/blob/main/src/terminal/adapter/adaptDispatch.cpp

2. Microsoft Terminal treats **screen clear** and **scrollback clear** as distinct operations (`ED2` vs `ED3`-style behavior):
- https://github.com/microsoft/terminal/blob/main/src/terminal/adapter/adaptDispatch.cpp

3. Zed uses `alacritty_terminal` (pinned in workspace), where:
- LNM (`LINE_FEED_NEW_LINE`) is a mode bit, not default-on.
- Clear handling separates normal screen clear and saved-history clear.
- Windows PTY is ConPTY-based with `conpty.dll` support and API fallback.
- https://github.com/zed-industries/zed/blob/main/Cargo.toml
- https://github.com/zed-industries/alacritty/blob/9d9640d4e56d67a09d049f9c0a300aae08d4f61e/alacritty_terminal/src/term/mod.rs
- https://github.com/zed-industries/alacritty/blob/9d9640d4e56d67a09d049f9c0a300aae08d4f61e/alacritty_terminal/src/tty/windows/conpty.rs

## Validation Status

1. Code changes applied successfully.
2. Local compile/test could not be run in this environment because `cmake` is unavailable in the current shell.
3. Windows runtime validation should be executed with `docs/WINDOWS_LAZYVIM_CLEAR_CHECKLIST.md`.
