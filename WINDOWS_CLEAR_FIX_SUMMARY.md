# Windows Clear Crash Fix - Technical Summary

## Problem Statement

The terminal renderer was crashing on Windows when running `ls` followed by `clear` (or `cls`). The crash manifested as:
- Cell data mismatch between TerminalEmulator, TerminalScrollback, and TerminalWidget
- Viewport desynchronization
- Rendering artifacts or complete renderer failure

## Root Cause Analysis

### 1. Scrollback Repopulation Race Condition

**The Issue:**
```cpp
// OLD CODE in flushPendingSessionOutput()
m_emulator.feedOutput(chunk);  // This processes "clear" and sets scrollbackClearRequested
// ...
for (TerminalEmulator::Line line : m_emulator.takeScrolledLines()) {
    m_scrollback.pushLine(std::move(line));  // BUG: Adds lines from BEFORE clear!
}
// ...
if (m_emulator.takeScrollbackClearRequested()) {
    m_scrollback = TerminalScrollback(m_scrollback.maxLines());  // Too late!
}
```

**Timeline of the Bug:**
1. User runs `ls` → output scrolls, lines added to `m_scrolledLines`
2. User runs `clear` → `eraseInDisplay(2)` is called
3. `eraseInDisplay` clears screen cells and sets `m_scrollbackClearRequested = true`
4. `eraseInDisplay` calls `m_scrolledLines.clear()` to prevent repopulation
5. **BUT** if `ls` and `clear` arrive in the same ConPTY batch:
   - `feedOutput` processes both commands
   - `ls` adds lines to `m_scrolledLines`
   - `clear` clears screen but `m_scrolledLines` already has data
   - Widget code adds `m_scrolledLines` to scrollback BEFORE checking clear flag
6. Result: Scrollback contains pre-clear data, viewport is confused

**The Fix:**
```cpp
// NEW CODE in flushPendingSessionOutput()
bool scrollbackCleared = false;
if (m_emulator.takeScrollbackClearRequested()) {
    m_scrollback = TerminalScrollback(m_scrollback.maxLines());
    m_selection.clear();
    m_scrollOffset = 0;
    scrollbackCleared = true;
}

// Process scrolled lines AFTER checking for scrollback clear
if (!scrollbackCleared) {
    for (TerminalEmulator::Line line : m_emulator.takeScrolledLines()) {
        m_scrollback.pushLine(std::move(line));
    }
} else {
    // Discard any scrolled lines if scrollback was just cleared
    m_emulator.takeScrolledLines();
}
```

### 2. Viewport Scroll Optimization on Windows

**The Issue:**
ConPTY batches output differently than Unix PTY. When a full-screen clear happens, the viewport scroll optimization (`viewport()->scroll()`) can cause rendering artifacts because:
- The optimization assumes incremental scrolling
- Full-screen clears invalidate the entire viewport
- ConPTY may deliver clear sequences in the middle of a large output batch

**The Fix:**
```cpp
const bool isFullScreenClear = (dirtyRowCount >= m_emulator.rows());
#if defined(Q_OS_WIN)
const bool allowViewportScrollOptimization = !isFullScreenClear;
#else
constexpr bool allowViewportScrollOptimization = true;
#endif
```

On Windows, disable the scroll optimization when the entire screen is dirty. This forces a full repaint, which is safer for clear operations.

### 3. Cursor Position After Clear

**The Issue:**
Standard VT100/xterm behavior expects the cursor to be at position (0,0) after a full screen clear (ED2/ED3). The old code didn't reset the cursor position.

**The Fix:**
```cpp
void TerminalEmulator::eraseInDisplay(int mode)
{
    if (mode == 2 || mode == 3) {
        const TerminalCell eraseCell = makeEraseCell();
        std::fill(activeCells().begin(), activeCells().end(), eraseCell);
        
        // Reset cursor to home position after clear (standard VT behavior)
        m_cursorRow = 0;
        m_cursorCol = 0;
        
        // ... rest of clear logic
    }
}
```

### 4. Forced Full Repaint After Scrollback Clear

**The Issue:**
After clearing scrollback, the viewport might still have stale rendering state. Partial repaints could leave old cell data visible.

**The Fix:**
```cpp
if (scrollbackCleared) {
    // Force full viewport repaint after scrollback clear
    verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    m_scrollOffset = 0;
    viewport()->update();
    return;  // Early return to skip partial repaint logic
}
```

## Changes Made

### File: `src/terminal/TerminalEmulator.cpp`

**Function: `eraseInDisplay(int mode)`**
- Added cursor reset to (0,0) after ED2/ED3 clear
- Ensures standard VT behavior

### File: `src/terminal/TerminalWidget.cpp`

**Function: `flushPendingSessionOutput()`**
1. Moved scrollback clear check BEFORE processing `takeScrolledLines()`
2. Added conditional logic to discard scrolled lines if scrollback was cleared
3. Added early return with full viewport update after scrollback clear

**Function: `flushPendingSessionOutput()` (dirty region update section)**
1. Added detection of full-screen clears
2. Added Windows-specific conditional to disable viewport scroll optimization for full-screen clears
3. Prevents rendering artifacts during clear operations on Windows

### File: `WINDOWS_FIXES_REPORT.md`

- Documented all fixes with technical details
- Added testing recommendations

## Testing Checklist

Run these tests on Windows 10/11 with PowerShell, cmd.exe, and Git Bash:

### Basic Clear Tests
```powershell
# PowerShell
1..200 | ForEach-Object { "line $_" }
Clear-Host
"after clear"
```

```cmd
REM cmd.exe
for /L %i in (1,1,200) do @echo line %i
cls
echo after clear
```

```bash
# Git Bash
seq 1 200
clear
echo "after clear"
```

### Stress Test
```bash
# Git Bash
for i in {1..25}; do
  seq 1 120
  printf "cycle %02d pre\n" "$i"
  clear
  printf "cycle %02d post\n" "$i"
done
```

### LazyVim Terminal Buffer
```vim
:term
" In terminal mode:
seq 1 200
clear
echo ok
```

## Expected Behavior After Fix

1. ✅ `clear` command fully clears the viewport
2. ✅ Cursor appears at top-left (0,0) after clear
3. ✅ No stale lines from before clear appear in scrollback
4. ✅ Subsequent output renders correctly
5. ✅ No renderer crashes or cell data mismatches
6. ✅ Scrollbar state is correct after clear
7. ✅ Works correctly in LazyVim `:term` buffers

## Technical Notes

### Why ConPTY is Different

Windows ConPTY (Console Pseudo-Terminal) has different batching behavior than Unix PTY:
- **Unix PTY**: Output is delivered in smaller, more frequent chunks
- **ConPTY**: Output is batched more aggressively for performance

This means that on Windows, a single `feedOutput()` call might contain:
```
[ls output with 200 lines] + [clear sequence] + [prompt]
```

All in one batch. The fix ensures that even when clear sequences arrive mid-batch, the renderer state stays synchronized.

### Why Viewport Scroll Optimization Fails

The viewport scroll optimization uses `QWidget::scroll()` to shift pixels rather than repainting. This is efficient for incremental scrolling but breaks when:
1. The entire screen is cleared
2. The scrollback is cleared
3. Cell data is invalidated

On Windows, ConPTY's batching makes this more likely to happen, so we disable the optimization for full-screen clears.

## References

- `docs/WINDOWS_LAZYVIM_CLEAR_CHECKLIST.md` - Comprehensive testing checklist
- `WINDOWS_FIXES_REPORT.md` - Complete fix history
- `Architecture.md` - Terminal architecture overview
- Microsoft Terminal source: https://github.com/microsoft/terminal
