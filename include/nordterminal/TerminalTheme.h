#pragma once

#include <QColor>
#include <QFont>
#include <QString>
#include <array>

#include "nordterminal/TerminalCell.h"

namespace nord::terminal {

struct TerminalTheme {
    QString id = QStringLiteral("nord-dark");
    QString name = QStringLiteral("Nord Dark");

    QColor background = QColor("#2E3440");
    QColor foreground = QColor("#D8DEE9");
    QColor cursor = QColor("#ECEFF4");
    QColor selection = QColor("#4C566A");

    std::array<QColor, 16> ansi = {
        QColor("#3B4252"), QColor("#BF616A"), QColor("#A3BE8C"), QColor("#EBCB8B"),
        QColor("#81A1C1"), QColor("#B48EAD"), QColor("#88C0D0"), QColor("#E5E9F0"),
        QColor("#4C566A"), QColor("#BF616A"), QColor("#A3BE8C"), QColor("#EBCB8B"),
        QColor("#81A1C1"), QColor("#B48EAD"), QColor("#8FBCBB"), QColor("#ECEFF4")
    };

    static QFont defaultTerminalFont();
    QFont font = defaultTerminalFont();

    [[nodiscard]] QColor resolveForeground(TerminalColorIndex index) const;
    [[nodiscard]] QColor resolveBackground(TerminalColorIndex index) const;

    static TerminalTheme nordDark();
};

} // namespace nord::terminal
