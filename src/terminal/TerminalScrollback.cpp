#include "nordterminal/TerminalScrollback.h"

#include <algorithm>

namespace nord::terminal {

TerminalScrollback::TerminalScrollback(int maxLines)
    : m_maxLines(std::max(1, maxLines))
{
}

void TerminalScrollback::pushLine(const QString &line)
{
    m_lines.push_back(line);
    while (static_cast<int>(m_lines.size()) > m_maxLines) {
        m_lines.pop_front();
    }
}

int TerminalScrollback::size() const
{
    return static_cast<int>(m_lines.size());
}

int TerminalScrollback::maxLines() const
{
    return m_maxLines;
}

void TerminalScrollback::setMaxLines(int maxLines)
{
    m_maxLines = std::max(1, maxLines);
    while (static_cast<int>(m_lines.size()) > m_maxLines) {
        m_lines.pop_front();
    }
}

std::vector<QString> TerminalScrollback::slice(int start, int count) const
{
    std::vector<QString> output;
    if (count <= 0 || m_lines.empty()) {
        return output;
    }

    const int normalizedStart = std::max(0, start);
    const int end = std::min(static_cast<int>(m_lines.size()), normalizedStart + count);
    output.reserve(static_cast<std::size_t>(std::max(0, end - normalizedStart)));
    for (int i = normalizedStart; i < end; ++i) {
        output.push_back(m_lines[static_cast<std::size_t>(i)]);
    }
    return output;
}

const QString &TerminalScrollback::lineAt(int index) const
{
    static const QString empty;
    if (index < 0 || index >= static_cast<int>(m_lines.size())) {
        return empty;
    }
    return m_lines[static_cast<std::size_t>(index)];
}

} // namespace nord::terminal
