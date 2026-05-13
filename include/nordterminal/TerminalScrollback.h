#pragma once

#include <QString>
#include <deque>
#include <vector>

namespace nord::terminal {

class TerminalScrollback {
public:
    explicit TerminalScrollback(int maxLines = 10000);

    void pushLine(const QString &line);
    [[nodiscard]] int size() const;
    [[nodiscard]] int maxLines() const;
    void setMaxLines(int maxLines);
    [[nodiscard]] std::vector<QString> slice(int start, int count) const;

private:
    int m_maxLines = 10000;
    std::deque<QString> m_lines;
};

} // namespace nord::terminal
