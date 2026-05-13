#pragma once

#include <QByteArray>

class QKeyEvent;

namespace nord::terminal {

class TerminalKeyMapper {
public:
    static QByteArray mapKeyEvent(const QKeyEvent *event, bool applicationCursorKeys = false);
};

} // namespace nord::terminal
