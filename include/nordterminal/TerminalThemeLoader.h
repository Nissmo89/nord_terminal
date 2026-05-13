#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

#include "nordterminal/TerminalTheme.h"

namespace nord::terminal {

struct ThemeLoadResult {
    bool ok = false;
    TerminalTheme theme;
    QString error;
};

class TerminalThemeLoader {
public:
    static ThemeLoadResult loadFromFile(const QString &path);
    static ThemeLoadResult loadFromJson(const QByteArray &jsonData);
    static QJsonObject toJson(const TerminalTheme &theme);
};

} // namespace nord::terminal
