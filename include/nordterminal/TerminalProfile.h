#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

namespace nord::terminal {

struct TerminalProfile {
    QString name;
    QString shellPath;
    QStringList arguments;
    QString workingDirectory;
    QMap<QString, QString> environment;

    static TerminalProfile defaultForHost();
};

} // namespace nord::terminal
