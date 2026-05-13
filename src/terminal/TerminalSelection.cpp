#include "nordterminal/TerminalSelection.h"

namespace nord::terminal {

void TerminalSelection::begin(const QPoint &cellPosition)
{
    m_active = true;
    m_start = cellPosition;
    m_end = cellPosition;
}

void TerminalSelection::update(const QPoint &cellPosition)
{
    if (!m_active) {
        return;
    }
    m_end = cellPosition;
}

void TerminalSelection::clear()
{
    m_active = false;
    m_start = QPoint();
    m_end = QPoint();
}

bool TerminalSelection::isActive() const
{
    return m_active;
}

QPoint TerminalSelection::start() const
{
    return m_start;
}

QPoint TerminalSelection::end() const
{
    return m_end;
}

} // namespace nord::terminal
