#pragma once

#include <QChar>
#include <QColor>

namespace nord::terminal {

enum class TerminalColorIndex : int {
    Default = -1,
    Black = 0,
    Red = 1,
    Green = 2,
    Yellow = 3,
    Blue = 4,
    Magenta = 5,
    Cyan = 6,
    White = 7,
    BrightBlack = 8,
    BrightRed = 9,
    BrightGreen = 10,
    BrightYellow = 11,
    BrightBlue = 12,
    BrightMagenta = 13,
    BrightCyan = 14,
    BrightWhite = 15
};

struct TerminalCell {
    QChar character = QChar(' ');
    TerminalColorIndex foreground = TerminalColorIndex::Default;
    TerminalColorIndex background = TerminalColorIndex::Default;
    bool hasForegroundRgb = false;
    bool hasBackgroundRgb = false;
    QColor foregroundRgb = QColor();
    QColor backgroundRgb = QColor();
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool inverse = false;
};

} // namespace nord::terminal
