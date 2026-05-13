#pragma once

#include <QPoint>

namespace nord::terminal {

class TerminalSelection {
public:
    void begin(const QPoint &cellPosition);
    void update(const QPoint &cellPosition);
    void clear();

    [[nodiscard]] bool isActive() const;
    [[nodiscard]] QPoint start() const;
    [[nodiscard]] QPoint end() const;

private:
    bool m_active = false;
    QPoint m_start;
    QPoint m_end;
};

} // namespace nord::terminal
