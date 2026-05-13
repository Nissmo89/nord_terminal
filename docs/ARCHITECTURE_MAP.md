# Architecture Map

This file maps `Architecture.md` to concrete code in this repo.

## Modules

1. `TerminalWidget` (`include/nordterminal/TerminalWidget.h`, `src/terminal/TerminalWidget.cpp`)
2. `TerminalEmulator` (`include/nordterminal/TerminalEmulator.h`, `src/terminal/TerminalEmulator.cpp`)
3. `TerminalSession` (`include/nordterminal/TerminalSession.h`, `src/terminal/TerminalSession.cpp`)
4. `TerminalKeyMapper` (`include/nordterminal/TerminalKeyMapper.h`, `src/terminal/TerminalKeyMapper.cpp`)
5. `TerminalScrollback` (`include/nordterminal/TerminalScrollback.h`, `src/terminal/TerminalScrollback.cpp`)
6. `TerminalSelection` (`include/nordterminal/TerminalSelection.h`, `src/terminal/TerminalSelection.cpp`)
7. `TerminalTheme` + loader (`include/nordterminal/TerminalTheme*.h`, `src/terminal/TerminalTheme*.cpp`)

## Known Bootstrap Gaps

- Windows ConPTY backend is not implemented yet (Unix PTY is implemented).
- ANSI coverage is improved (alternate screen, cursor modes, UTF-8 text, 256/truecolor), but still incomplete vs full VT standards.
- No `libvterm` integration yet.

## Next Critical Integration

1. Add a PTY abstraction (`IPtyBackend`) with Linux/macOS + Windows ConPTY implementations.
2. Replace `TerminalEmulator` parser with `libvterm` wrapper.
3. Keep the widget/theming APIs stable so host IDE integration remains unchanged.
