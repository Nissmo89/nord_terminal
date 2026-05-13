#include "nordterminal/TerminalTheme.h"

namespace nord::terminal {

QColor TerminalTheme::resolveForeground(TerminalColorIndex index) const
{
    if (index == TerminalColorIndex::Default) {
        return foreground;
    }
    const int paletteIndex = static_cast<int>(index);
    if (paletteIndex >= 0 && paletteIndex < static_cast<int>(ansi.size())) {
        return ansi.at(static_cast<std::size_t>(paletteIndex));
    }
    return foreground;
}

QColor TerminalTheme::resolveBackground(TerminalColorIndex index) const
{
    if (index == TerminalColorIndex::Default) {
        return background;
    }
    const int paletteIndex = static_cast<int>(index);
    if (paletteIndex >= 0 && paletteIndex < static_cast<int>(ansi.size())) {
        return ansi.at(static_cast<std::size_t>(paletteIndex));
    }
    return background;
}

TerminalTheme TerminalTheme::nordDark()
{
    return {};
}

} // namespace nord::terminal
