#pragma once

#include <QString>
#include <deque>
#include <vector>

#include "nordterminal/TerminalCell.h"

namespace nord::terminal {

class TerminalScrollback {
public:
    using Line = std::vector<TerminalCell>;

    explicit TerminalScrollback(int maxLines = 10000);

    void pushLine(const Line &line);
    void pushLine(Line &&line);
    [[nodiscard]] int size() const;
    [[nodiscard]] int maxLines() const;
    void setMaxLines(int maxLines);
    [[nodiscard]] std::vector<Line> slice(int start, int count) const;
    [[nodiscard]] const Line &lineAt(int index) const;
    [[nodiscard]] static QString lineText(const Line &line);

private:
    int m_maxLines = 10000;
    std::deque<Line> m_lines;
};

} // namespace nord::terminal
