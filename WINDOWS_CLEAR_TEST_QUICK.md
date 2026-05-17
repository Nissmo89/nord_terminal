# Quick Windows Clear Test Guide

## Quick Smoke Test (5 minutes)

Run these commands in order and verify expected behavior:

### Test 1: Basic Clear (PowerShell)
```powershell
# Fill screen
1..50 | ForEach-Object { "Line $_" }

# Clear
Clear-Host

# Verify
"After clear - cursor should be at top"
```

**Expected:** 
- ✅ Screen is completely clear
- ✅ "After clear" message appears at top-left
- ✅ No old lines visible

### Test 2: ls + clear (Git Bash)
```bash
# This is the exact scenario that was crashing
ls -la
clear
ls -la
```

**Expected:**
- ✅ First `ls` output displays
- ✅ `clear` wipes screen completely
- ✅ Second `ls` output displays at top
- ✅ **NO CRASH** ← This was the bug!

### Test 3: Rapid Clear Cycles (cmd.exe)
```cmd
for /L %i in (1,1,10) do @(echo Cycle %i && cls && timeout /t 1 /nobreak > nul)
```

**Expected:**
- ✅ Each cycle clears properly
- ✅ No cumulative rendering artifacts
- ✅ Cursor stays at top-left after each clear

### Test 4: LazyVim Terminal Buffer
```vim
:term
```

Then in terminal mode:
```bash
seq 1 100
clear
echo "Terminal buffer clear test"
```

**Expected:**
- ✅ Clear works inside Neovim terminal buffer
- ✅ No UI corruption in LazyVim

## What Was Fixed

| Issue | Before | After |
|-------|--------|-------|
| **Crash on `ls` + `clear`** | ❌ Renderer crash | ✅ Works correctly |
| **Scrollback pollution** | ❌ Old lines reappear | ✅ Scrollback properly cleared |
| **Cursor position** | ❌ Random position | ✅ Top-left (0,0) |
| **Viewport sync** | ❌ Desynchronized | ✅ Fully synchronized |

## If You See Issues

### Symptom: Old lines still visible after clear
**Likely cause:** Scrollback repopulation race condition  
**Check:** `flushPendingSessionOutput()` in `TerminalWidget.cpp`

### Symptom: Rendering artifacts or partial clears
**Likely cause:** Viewport scroll optimization  
**Check:** `allowViewportScrollOptimization` logic in `TerminalWidget.cpp`

### Symptom: Cursor in wrong position after clear
**Likely cause:** Cursor not reset in `eraseInDisplay`  
**Check:** `eraseInDisplay()` in `TerminalEmulator.cpp`

## Full Test Suite

For comprehensive testing, run:
```bash
# See docs/WINDOWS_LAZYVIM_CLEAR_CHECKLIST.md
```

## Build and Test

```bash
# Build
cmake -S . -B build
cmake --build build -j

# Run demo
./build/nord_terminal_demo

# Test the fix
# 1. Open terminal
# 2. Run: ls -la
# 3. Run: clear
# 4. Run: ls -la
# 5. Verify no crash and proper rendering
```

## Technical Details

See `WINDOWS_CLEAR_FIX_SUMMARY.md` for:
- Root cause analysis
- Code changes explained
- Architecture details
- ConPTY vs Unix PTY differences
