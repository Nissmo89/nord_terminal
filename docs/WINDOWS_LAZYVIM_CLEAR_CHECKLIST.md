# Windows LazyVim + Clear Validation Checklist

Use this checklist before every Windows push when renderer/parser/session code changes.

## Scope

- `clear` / `cls` correctness
- cursor placement stability after screen clears
- color/highlight stability after repeated redraws
- LazyVim UI behavior (splits, floating windows, terminal buffer)
- isolation from Linux behavior (no cross-platform regression indicators)

## Test Environment

- OS: Windows 10/11 (ConPTY-capable)
- Shells: PowerShell, `cmd.exe`, Git Bash
- Neovim: `nvim --version` (recommended `0.10+`)
- LazyVim installed and launching with `nvim`
- Terminal profile exports `TERM=xterm-256color`

Record versions in test notes:

```text
Windows build:
Shell:
Neovim:
LazyVim commit/date:
NordTerminal commit:
```

## Pass/Fail Rules

Mark the run failed if any of the following appears:

- clear only wipes part of the viewport
- prompt appears at wrong row/column after clear
- cursor drifts horizontally after repeated commands
- old lines lose color/highlight unexpectedly without being redrawn
- UI corruption in LazyVim after `:redraw!`, `:term`, or split operations

## A) Core Clear + Cursor Tests (Outside Neovim)

Run each block in PowerShell, `cmd.exe`, and Git Bash.

### A1. Basic Clear

1. Fill the screen with output.
2. Run clear command.
3. Run one command immediately after.

PowerShell:

```powershell
1..200 | ForEach-Object { "line $_" }
Clear-Host
"after clear"
```

`cmd.exe`:

```bat
for /L %i in (1,1,200) do @echo line %i
cls
echo after clear
```

Git Bash:

```bash
seq 1 200
clear
echo "after clear"
```

Expected:

- full viewport clears
- prompt and `after clear` render at expected top area
- no half-screen artifacts

### A2. VT Clear Sequence (Parser Path)

Git Bash:

```bash
printf 'before\n'
printf '\x1b[2J\x1b[H'
echo "after csi clear"
```

PowerShell:

```powershell
Write-Output "before"
Write-Host "`e[2J`e[H" -NoNewline
Write-Output "after csi clear"
```

Expected:

- behavior matches `clear`/`cls`
- no cursor displacement

### A3. Repeated Stress Clear

Git Bash:

```bash
for i in {1..25}; do
  seq 1 120 > /dev/null
  printf "cycle %02d pre\n" "$i"
  clear
  printf "cycle %02d post\n" "$i"
done
```

PowerShell:

```powershell
1..25 | ForEach-Object {
  "cycle $($_) pre"
  Clear-Host
  "cycle $($_) post"
}
```

Expected:

- no cumulative cursor drift
- no increasing paint corruption over cycles

### A4. Resize + Clear

1. Resize terminal window smaller/larger 3-4 times.
2. Run clear after each resize.

Expected:

- clear still wipes full viewport
- prompt position remains correct
- no stale rows at top/bottom

## B) LazyVim-Focused Validation

### B1. Startup and Base Render

```bash
nvim
```

Inside Neovim:

- `:checkhealth`
- `:Lazy`
- close with `q`

Expected:

- no broken boxes in borders/statusline
- no missing highlight regions

### B2. Redraw and Split Stability

Inside Neovim:

```vim
:e $MYVIMRC
:vsplit
:split
:redraw!
```

Expected:

- split separators stay aligned
- cursor remains on expected cell after `:redraw!`
- no shifted content bands

### B3. Scroll + Return

Inside Neovim:

- hold `j` to scroll down
- hold `k` to scroll up
- jump with `gg` and `G`

Expected:

- no smear/ghosting
- line numbers and text columns remain aligned

### B4. Terminal Buffer Clear Path

Inside Neovim:

```vim
:term
```

Then in terminal-mode shell:

```bash
seq 1 200
clear
echo ok
```

Return to normal mode and exit terminal buffer.

Expected:

- same clear correctness inside `:term`
- no broken screen after leaving terminal buffer

### B5. Plugin UI Surfaces

Inside Neovim:

- `:Telescope find_files` (if installed)
- `:Mason` (if installed)
- `:Lazy`

Expected:

- floating borders render correctly
- highlights persist after closing/reopening UI

## C) Regression Isolation Checks (Linux Safety)

Run quick smoke tests on Linux build after Windows changes:

```bash
printf "\033[31mred\033[0m\n"
clear
nvim +"q"
```

Expected:

- no behavior change in clear semantics
- no cursor drift newly introduced on Linux

## D) Result Template

```text
[ ] A1 Basic Clear
[ ] A2 VT Clear Sequence
[ ] A3 Repeated Stress Clear
[ ] A4 Resize + Clear
[ ] B1 Startup and Base Render
[ ] B2 Redraw and Split Stability
[ ] B3 Scroll + Return
[ ] B4 Terminal Buffer Clear Path
[ ] B5 Plugin UI Surfaces
[ ] C Linux Safety Smoke

Overall: PASS / FAIL
Blocking issues:
```

