# Windows Fixes Report

Date: 2026-05-15  
Source report reviewed: `WINDOWS_BUGS_REPORT.md`

## Latest Fixes (2026-05-15 - Renderer Crash on Clear)

### Issue: Terminal Renderer Crashes on Windows with `ls` + `clear`

**Symptoms:**
- Running `ls` followed by `clear` (or `cls`) causes renderer crash
- Cell data mismatch between emulator, scrollback, and widget
- Viewport becomes desynchronized after clear operations

**Root Causes Identified:**
1. **Race condition** between ConPTY output batching and screen clear operations
2. **Scrollback repopulation** after clear: `m_scrolledLines` from before the clear were being added to scrollback after `eraseInDisplay` cleared the screen
3. **Viewport scroll optimization** causing rendering artifacts when ConPTY batches output containing clear sequences
4. **Cursor position** not reset to home after ED2/ED3 clear (non-standard behavior)

**Fixes Applied:**

1. **Reset cursor to home position after clear (ED2/ED3)**
   - File: `src/terminal/TerminalEmulator.cpp`
   - Added `m_cursorRow = 0; m_cursorCol = 0;` after clearing screen in `eraseInDisplay`
   - Why: Standard VT behavior expects cursor at (0,0) after full screen clear

2. **Prevent scrollback repopulation after clear**
   - File: `src/terminal/TerminalWidget.cpp` in `flushPendingSessionOutput()`
   - Moved `takeScrolledLines()` processing AFTER scrollback clear check
   - Discard scrolled lines if scrollback was just cleared
   - Why: Prevents stale lines from before clear being added back to scrollback

3. **Disable viewport scroll optimization for full-screen clears on Windows**
   - File: `src/terminal/TerminalWidget.cpp` in `flushPendingSessionOutput()`
   - Added Windows-specific check: `const bool isFullScreenClear = (dirtyRowCount >= m_emulator.rows());`
   - Disable optimization when `isFullScreenClear` is true on Windows
   - Why: ConPTY batches output differently than Unix PTY; scroll optimization causes artifacts during clear sequences

4. **Force full viewport repaint after scrollback clear**
   - File: `src/terminal/TerminalWidget.cpp` in `flushPendingSessionOutput()`
   - Added early return with `viewport()->update()` after scrollback clear
   - Ensures scrollbar and viewport are fully synchronized
   - Why: Prevents partial repaints that can leave stale cell data visible

**Testing Recommendations:**
- Run all tests in `docs/WINDOWS_LAZYVIM_CLEAR_CHECKLIST.md`
- Focus on sections A1-A4 (Core Clear + Cursor Tests)
- Verify `ls` + `clear` + `ls` sequence works correctly
- Test with PowerShell, cmd.exe, and Git Bash
- Verify LazyVim `:term` buffer clear behavior (section B4)

## Previous Fixes (Original Report)

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
