#include "nordterminal/TerminalScrollback.h"

#include <algorithm>
#include <utility>

namespace nord::terminal {

TerminalScrollback::TerminalScrollback(int maxLines)
    : m_maxLines(std::max(1, maxLines))
{
}

void TerminalScrollback::pushLine(const Line &line)
{
    m_lines.push_back(line);
    while (static_cast<int>(m_lines.size()) > m_maxLines) {
        m_lines.pop_front();
    }
}

void TerminalScrollback::pushLine(Line &&line)
{
    m_lines.push_back(std::move(line));
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

std::vector<TerminalScrollback::Line> TerminalScrollback::slice(int start, int count) const
{
    std::vector<Line> output;
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

const TerminalScrollback::Line &TerminalScrollback::lineAt(int index) const
{
    static const Line empty;
    if (index < 0 || index >= static_cast<int>(m_lines.size())) {
        return empty;
    }
    return m_lines[static_cast<std::size_t>(index)];
}

QString TerminalScrollback::lineText(const Line &line)
{
    QString text;
    text.reserve(static_cast<qsizetype>(line.size()) * 2);
    for (const TerminalCell &cell : line) {
        if (cell.wideContinuation) {
            text.append(QLatin1Char(' '));
            continue;
        }
        text.append(cell.character);
    }
    return text;
}

} // namespace nord::terminal
