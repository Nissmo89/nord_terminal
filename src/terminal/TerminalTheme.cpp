#include "nordterminal/TerminalTheme.h"

#include <QFontDatabase>
#include <QFontInfo>

namespace nord::terminal {

QFont TerminalTheme::defaultTerminalFont()
{
    QFont font(QStringLiteral("JetBrains Mono"), 11);
    if (!QFontInfo(font).fixedPitch()) {
        font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        font.setPointSize(11);
    }
    return font;
}

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
