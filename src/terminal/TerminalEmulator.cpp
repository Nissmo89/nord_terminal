#include "nordterminal/TerminalEmulator.h"

#include <QByteArray>
#include <QApplication>
#include <QList>

#include <algorithm>
#include <cstddef>

namespace nord::terminal {

namespace {

bool isFinalCsiByte(unsigned char ch)
{
    return ch >= 0x40 && ch <= 0x7e;
}

} // namespace

TerminalEmulator::TerminalEmulator(int rows, int cols)
    : m_rows(std::max(1, rows))
    , m_cols(std::max(1, cols))
    , m_scrollBottom(m_rows - 1)
    , m_mainCells(static_cast<std::size_t>(m_rows * m_cols))
    , m_altCells(static_cast<std::size_t>(m_rows * m_cols))
{
}

void TerminalEmulator::resize(int rows, int cols)
{
    const int newRows = std::max(1, rows);
    const int newCols = std::max(1, cols);
    if (newRows == m_rows && newCols == m_cols) {
        return;
    }

    auto resizeBuffer = [this, newRows, newCols](const std::vector<TerminalCell> &source) {
        std::vector<TerminalCell> resized(static_cast<std::size_t>(newRows * newCols));
        const int copyRows = std::min(m_rows, newRows);
        const int copyCols = std::min(m_cols, newCols);
        for (int row = 0; row < copyRows; ++row) {
            for (int col = 0; col < copyCols; ++col) {
                resized[static_cast<std::size_t>(row * newCols + col)] = source[static_cast<std::size_t>(row * m_cols + col)];
            }
        }
        return resized;
    };

    m_mainCells = resizeBuffer(m_mainCells);
    m_altCells = resizeBuffer(m_altCells);

    m_rows = newRows;
    m_cols = newCols;
    m_cursorRow = std::clamp(m_cursorRow, 0, m_rows - 1);
    m_cursorCol = std::clamp(m_cursorCol, 0, m_cols - 1);
    m_savedCursorRowMain = std::clamp(m_savedCursorRowMain, 0, m_rows - 1);
    m_savedCursorColMain = std::clamp(m_savedCursorColMain, 0, m_cols - 1);
    m_savedCursorRowAlt = std::clamp(m_savedCursorRowAlt, 0, m_rows - 1);
    m_savedCursorColAlt = std::clamp(m_savedCursorColAlt, 0, m_cols - 1);
    m_scrollTop = std::clamp(m_scrollTop, 0, m_rows - 1);
    m_scrollBottom = std::clamp(m_scrollBottom, m_scrollTop, m_rows - 1);
}

void TerminalEmulator::reset()
{
    std::fill(m_mainCells.begin(), m_mainCells.end(), TerminalCell{});
    std::fill(m_altCells.begin(), m_altCells.end(), TerminalCell{});
    m_cursorRow = 0;
    m_cursorCol = 0;
    m_savedCursorRowMain = 0;
    m_savedCursorColMain = 0;
    m_savedCursorRowAlt = 0;
    m_savedCursorColAlt = 0;
    m_cursorVisible = true;
    m_applicationCursorKeys = false;
    m_bracketedPasteMode = false;
    m_mouseTrackingMode = MouseTrackingMode::Disabled;
    m_mouseSgrMode = false;
    m_synchronizedOutputMode = false;
    m_inAltBuffer = false;
    resetScrollRegion();
    m_parserState = ParserState::Ground;
    m_csiParams.clear();
    m_oscData.clear();
    m_pendingResponse.clear();
    m_escapeIntermediates.clear();
    m_utf8Pending.clear();
    m_utf8ExpectedBytes = 0;
    m_charsetG0 = Charset::Ascii;
    m_charsetG1 = Charset::Ascii;
    m_useG1 = false;
    m_style = {};
    m_scrolledLines.clear();
    m_scrollbackClearRequested = false;
}

void TerminalEmulator::feedOutput(const QByteArray &data)
{
    for (unsigned char ch : data) {
        feedByte(ch);
    }
}

QByteArray TerminalEmulator::takePendingResponse()
{
    QByteArray response = std::move(m_pendingResponse);
    m_pendingResponse.clear();
    return response;
}

int TerminalEmulator::rows() const
{
    return m_rows;
}

int TerminalEmulator::cols() const
{
    return m_cols;
}

QPoint TerminalEmulator::cursorPosition() const
{
    return {m_cursorCol, m_cursorRow};
}

bool TerminalEmulator::cursorVisible() const
{
    return m_cursorVisible;
}

bool TerminalEmulator::applicationCursorKeys() const
{
    return m_applicationCursorKeys;
}

bool TerminalEmulator::bracketedPasteMode() const
{
    return m_bracketedPasteMode;
}

bool TerminalEmulator::mouseTrackingEnabled() const
{
    return m_mouseTrackingMode != MouseTrackingMode::Disabled;
}

bool TerminalEmulator::mouseButtonTrackingEnabled() const
{
    return m_mouseTrackingMode == MouseTrackingMode::Button || m_mouseTrackingMode == MouseTrackingMode::Any;
}

bool TerminalEmulator::mouseAnyTrackingEnabled() const
{
    return m_mouseTrackingMode == MouseTrackingMode::Any;
}

bool TerminalEmulator::mouseSgrMode() const
{
    return m_mouseSgrMode;
}

TerminalCell TerminalEmulator::cellAt(int row, int col) const
{
    if (row < 0 || col < 0 || row >= m_rows || col >= m_cols) {
        return {};
    }
    return activeCells()[static_cast<std::size_t>(index(row, col))];
}

QString TerminalEmulator::lineText(int row) const
{
    if (row < 0 || row >= m_rows) {
        return {};
    }

    QString text;
    text.reserve(m_cols);
    for (int col = 0; col < m_cols; ++col) {
        const TerminalCell cell = cellAt(row, col);
        if (!cell.wideContinuation) {
            text.append(cell.character);
        }
    }
    return text;
}

std::vector<QString> TerminalEmulator::takeScrolledLines()
{
    std::vector<QString> lines = std::move(m_scrolledLines);
    m_scrolledLines.clear();
    return lines;
}

bool TerminalEmulator::takeScrollbackClearRequested()
{
    const bool requested = m_scrollbackClearRequested;
    m_scrollbackClearRequested = false;
    return requested;
}

void TerminalEmulator::feedByte(unsigned char ch)
{
    switch (m_parserState) {
    case ParserState::Ground:
        if (ch == 0x9b) { // 8-bit CSI
            flushIncompleteUtf8();
            m_csiParams.clear();
            m_parserState = ParserState::Csi;
            return;
        }
        if (ch == 0x9d) { // 8-bit OSC
            flushIncompleteUtf8();
            m_oscData.clear();
            m_parserState = ParserState::Osc;
            return;
        }
        if (ch == 0x9c) { // 8-bit ST outside OSC/DCS: ignore
            flushIncompleteUtf8();
            return;
        }
        if (ch == 0x1b) {
            flushIncompleteUtf8();
            m_parserState = ParserState::Escape;
            return;
        }
        if (ch == 0x0e) {
            flushIncompleteUtf8();
            m_useG1 = true;
            return;
        }
        if (ch == 0x0f) {
            flushIncompleteUtf8();
            m_useG1 = false;
            return;
        }
        if (ch == '\n') {
            flushIncompleteUtf8();
            newline();
            return;
        }
        if (ch == '\r') {
            flushIncompleteUtf8();
            m_cursorCol = 0;
            return;
        }
        if (ch == '\b') {
            flushIncompleteUtf8();
            if (m_cursorCol > 0) {
                --m_cursorCol;
            }
            return;
        }
        if (ch == '\t') {
            flushIncompleteUtf8();
            const int target = ((m_cursorCol / 8) + 1) * 8;
            while (m_cursorCol < target) {
                putCharacter(QChar(' '));
            }
            return;
        }
        if (ch == 0x07) {
            flushIncompleteUtf8();
            if (QApplication::instance()) {
                QApplication::beep();
            }
            return;
        }
        if (ch >= 0x80 && ch <= 0x9f) {
            // Other C1 controls are currently unsupported; do not render placeholders for them.
            flushIncompleteUtf8();
            return;
        }
        if (ch < 0x20 || ch == 0x7f) {
            flushIncompleteUtf8();
            return;
        }
        if (ch < 0x80 && activeCharset() == Charset::DecSpecialGraphics) {
            flushIncompleteUtf8();
            putCharacter(mapDecSpecialGraphicsChar(ch));
            return;
        }
        feedUtf8Byte(ch);
        return;

    case ParserState::Escape:
        m_parserState = ParserState::Ground;
        if (ch == '[') {
            m_csiParams.clear();
            m_parserState = ParserState::Csi;
            return;
        }
        if (ch == ']') {
            m_oscData.clear();
            m_parserState = ParserState::Osc;
            return;
        }
        if (ch >= 0x20 && ch <= 0x2f) {
            m_escapeIntermediates.clear();
            m_escapeIntermediates.append(static_cast<char>(ch));
            m_parserState = ParserState::EscapeIntermediate;
            return;
        }
        if (ch == '7') {
            if (m_inAltBuffer) {
                m_savedCursorRowAlt = m_cursorRow;
                m_savedCursorColAlt = m_cursorCol;
            } else {
                m_savedCursorRowMain = m_cursorRow;
                m_savedCursorColMain = m_cursorCol;
            }
            return;
        }
        if (ch == '8') {
            if (m_inAltBuffer) {
                moveCursor(m_savedCursorRowAlt, m_savedCursorColAlt);
            } else {
                moveCursor(m_savedCursorRowMain, m_savedCursorColMain);
            }
            return;
        }
        if (ch == 'D') {
            newline();
            return;
        }
        if (ch == 'E') {
            newline();
            m_cursorCol = 0;
            return;
        }
        if (ch == 'M') {
            if (m_cursorRow == m_scrollTop) {
                scrollDown(m_scrollTop, m_scrollBottom);
            } else if (m_cursorRow > 0) {
                --m_cursorRow;
            }
            return;
        }
        if (ch == 'c') {
            reset();
            return;
        }
        return;

    case ParserState::EscapeIntermediate:
        if (ch >= 0x20 && ch <= 0x2f) {
            if (m_escapeIntermediates.size() < 8) {
                m_escapeIntermediates.append(static_cast<char>(ch));
            }
            return;
        }
        if (ch >= 0x30 && ch <= 0x7e) {
            handleEscapeIntermediateFinal(ch);
            m_escapeIntermediates.clear();
            m_parserState = ParserState::Ground;
            return;
        }
        m_escapeIntermediates.clear();
        m_parserState = ParserState::Ground;
        return;

    case ParserState::Csi:
        if (isFinalCsiByte(ch)) {
            handleCsi(static_cast<char>(ch), m_csiParams);
            m_csiParams.clear();
            m_parserState = ParserState::Ground;
            return;
        }
        if (m_csiParams.size() < 256) {
            m_csiParams.append(static_cast<char>(ch));
            return;
        }
        m_csiParams.clear();
        m_parserState = ParserState::Ground;
        return;

    case ParserState::Osc:
        if (ch == 0x07) {
            handleOsc(m_oscData);
            m_oscData.clear();
            m_parserState = ParserState::Ground;
            return;
        }
        if (ch == 0x9c) { // 8-bit ST
            handleOsc(m_oscData);
            m_oscData.clear();
            m_parserState = ParserState::Ground;
            return;
        }
        if (ch == 0x1b) {
            m_parserState = ParserState::OscEscape;
            return;
        }
        if (m_oscData.size() < 4096) {
            m_oscData.append(static_cast<char>(ch));
        }
        return;

    case ParserState::OscEscape:
        if (ch == '\\') {
            handleOsc(m_oscData);
            m_oscData.clear();
            m_parserState = ParserState::Ground;
            return;
        }
        if (m_oscData.size() < 4096) {
            m_oscData.append('\x1b');
            m_oscData.append(static_cast<char>(ch));
        }
        m_parserState = ParserState::Osc;
        return;
    }
}

void TerminalEmulator::feedUtf8Byte(unsigned char ch)
{
    if (m_utf8ExpectedBytes == 0) {
        if (ch < 0x80) {
            putCharacter(QChar::fromLatin1(static_cast<char>(ch)));
            return;
        }
        if ((ch & 0xE0) == 0xC0) {
            m_utf8ExpectedBytes = 2;
            m_utf8Pending = QByteArray(1, static_cast<char>(ch));
            return;
        }
        if ((ch & 0xF0) == 0xE0) {
            m_utf8ExpectedBytes = 3;
            m_utf8Pending = QByteArray(1, static_cast<char>(ch));
            return;
        }
        if ((ch & 0xF8) == 0xF0) {
            m_utf8ExpectedBytes = 4;
            m_utf8Pending = QByteArray(1, static_cast<char>(ch));
            return;
        }
        putCharacter(QChar(QChar::ReplacementCharacter));
        return;
    }

    if ((ch & 0xC0) != 0x80) {
        putCharacter(QChar(QChar::ReplacementCharacter));
        m_utf8Pending.clear();
        m_utf8ExpectedBytes = 0;
        // Re-run through the full state machine so control bytes (e.g. ESC) are not rendered as text.
        feedByte(ch);
        return;
    }

    m_utf8Pending.append(static_cast<char>(ch));
    if (m_utf8Pending.size() < m_utf8ExpectedBytes) {
        return;
    }

    const QString decoded = QString::fromUtf8(m_utf8Pending.constData(), m_utf8Pending.size());
    m_utf8Pending.clear();
    m_utf8ExpectedBytes = 0;

    if (decoded.isEmpty()) {
        putCharacter(QChar(QChar::ReplacementCharacter));
    } else {
        for (QChar character : decoded) {
            const ushort code = character.unicode();
            if (code >= 0x80 && code <= 0x9f) {
                // Map UTF-8 encoded C1 controls (e.g. U+009B CSI) back to terminal control bytes.
                feedByte(static_cast<unsigned char>(code));
                continue;
            }
            putCharacter(character);
        }
    }
}

void TerminalEmulator::flushIncompleteUtf8()
{
    if (m_utf8ExpectedBytes == 0) {
        return;
    }
    putCharacter(QChar(QChar::ReplacementCharacter));
    m_utf8Pending.clear();
    m_utf8ExpectedBytes = 0;
}

QColor TerminalEmulator::ansi256ToColor(int index)
{
    if (index < 0) {
        return QColor();
    }
    if (index < 16) {
        // fallback palette similar to common xterm defaults
        static const QColor base[16] = {
            QColor("#000000"), QColor("#800000"), QColor("#008000"), QColor("#808000"),
            QColor("#000080"), QColor("#800080"), QColor("#008080"), QColor("#c0c0c0"),
            QColor("#808080"), QColor("#ff0000"), QColor("#00ff00"), QColor("#ffff00"),
            QColor("#0000ff"), QColor("#ff00ff"), QColor("#00ffff"), QColor("#ffffff")
        };
        return base[index];
    }
    if (index >= 16 && index <= 231) {
        const int cube = index - 16;
        const int r = cube / 36;
        const int g = (cube / 6) % 6;
        const int b = cube % 6;
        static const int levels[6] = {0, 95, 135, 175, 215, 255};
        return QColor(levels[r], levels[g], levels[b]);
    }
    if (index >= 232 && index <= 255) {
        const int gray = 8 + (index - 232) * 10;
        return QColor(gray, gray, gray);
    }
    return QColor();
}

QChar TerminalEmulator::mapDecSpecialGraphicsChar(unsigned char ch) const
{
    switch (ch) {
    case '_':
        return QChar(' ');
    case '`':
        return QChar(0x25C6); // ◆
    case 'a':
        return QChar(0x2592); // ▒
    case 'b':
        return QChar(0x2409); // ␉
    case 'c':
        return QChar(0x240C); // ␌
    case 'd':
        return QChar(0x240D); // ␍
    case 'e':
        return QChar(0x240A); // ␊
    case 'f':
        return QChar(0x00B0); // °
    case 'g':
        return QChar(0x00B1); // ±
    case 'h':
        return QChar(0x2424); // ␤
    case 'i':
        return QChar(0x240B); // ␋
    case 'j':
        return QChar(0x2518); // ┘
    case 'k':
        return QChar(0x2510); // ┐
    case 'l':
        return QChar(0x250C); // ┌
    case 'm':
        return QChar(0x2514); // └
    case 'n':
        return QChar(0x253C); // ┼
    case 'o':
        return QChar(0x23BA); // ⎺
    case 'p':
        return QChar(0x23BB); // ⎻
    case 'q':
        return QChar(0x2500); // ─
    case 'r':
        return QChar(0x23BC); // ⎼
    case 's':
        return QChar(0x23BD); // ⎽
    case 't':
        return QChar(0x251C); // ├
    case 'u':
        return QChar(0x2524); // ┤
    case 'v':
        return QChar(0x2534); // ┴
    case 'w':
        return QChar(0x252C); // ┬
    case 'x':
        return QChar(0x2502); // │
    case 'y':
        return QChar(0x2264); // ≤
    case 'z':
        return QChar(0x2265); // ≥
    case '{':
        return QChar(0x03C0); // π
    case '|':
        return QChar(0x2260); // ≠
    case '}':
        return QChar(0x00A3); // £
    case '~':
        return QChar(0x00B7); // ·
    default:
        return QChar::fromLatin1(static_cast<char>(ch));
    }
}

bool TerminalEmulator::isWideCharacter(QChar ch)
{
    const ushort u = ch.unicode();
    return (u >= 0x1100 && u <= 0x115f)
        || (u >= 0x2329 && u <= 0x232a)
        || (u >= 0x2e80 && u <= 0xa4cf)
        || (u >= 0xac00 && u <= 0xd7a3)
        || (u >= 0xf900 && u <= 0xfaff)
        || (u >= 0xfe10 && u <= 0xfe19)
        || (u >= 0xfe30 && u <= 0xfe6f)
        || (u >= 0xff00 && u <= 0xff60)
        || (u >= 0xffe0 && u <= 0xffe6);
}

TerminalEmulator::Charset TerminalEmulator::activeCharset() const
{
    return m_useG1 ? m_charsetG1 : m_charsetG0;
}

void TerminalEmulator::handleEscapeIntermediateFinal(unsigned char finalByte)
{
    if (m_escapeIntermediates.isEmpty()) {
        return;
    }

    const char firstIntermediate = m_escapeIntermediates[0];
    Charset targetCharset = Charset::Ascii;
    if (finalByte == '0') {
        targetCharset = Charset::DecSpecialGraphics;
    } else if (finalByte == 'B') {
        targetCharset = Charset::Ascii;
    } else {
        return;
    }

    if (firstIntermediate == '(') {
        m_charsetG0 = targetCharset;
    } else if (firstIntermediate == ')') {
        m_charsetG1 = targetCharset;
    }
}

void TerminalEmulator::putCharacter(QChar ch)
{
    if (m_cursorCol >= m_cols) {
        // LF no longer implies CR; explicit autowrap must move to column 0.
        newline();
        m_cursorCol = 0;
    }

    const int charWidth = isWideCharacter(ch) ? 2 : 1;
    if (charWidth == 2 && m_cursorCol == m_cols - 1) {
        newline();
        m_cursorCol = 0;
    }

    TerminalCell cell;
    cell.character = ch;
    cell.foreground = m_style.foreground;
    cell.background = m_style.background;
    cell.hasForegroundRgb = m_style.hasForegroundRgb;
    cell.hasBackgroundRgb = m_style.hasBackgroundRgb;
    cell.foregroundRgb = m_style.foregroundRgb;
    cell.backgroundRgb = m_style.backgroundRgb;
    cell.bold = m_style.bold;
    cell.dim = m_style.dim;
    cell.italic = m_style.italic;
    cell.underline = m_style.underline;
    cell.strikethrough = m_style.strikethrough;
    cell.inverse = m_style.inverse;
    cell.wide = charWidth == 2;
    cell.wideContinuation = false;

    auto &cells = activeCells();
    cells[static_cast<std::size_t>(index(m_cursorRow, m_cursorCol))] = cell;
    if (charWidth == 2 && m_cursorCol + 1 < m_cols) {
        TerminalCell continuation = makeEraseCell();
        continuation.wideContinuation = true;
        cells[static_cast<std::size_t>(index(m_cursorRow, m_cursorCol + 1))] = continuation;
    }
    m_cursorCol += charWidth;
}

TerminalCell TerminalEmulator::makeEraseCell() const
{
    TerminalCell cell;
    cell.character = QChar(' ');
    cell.foreground = m_style.foreground;
    cell.background = m_style.background;
    cell.hasForegroundRgb = m_style.hasForegroundRgb;
    cell.hasBackgroundRgb = m_style.hasBackgroundRgb;
    cell.foregroundRgb = m_style.foregroundRgb;
    cell.backgroundRgb = m_style.backgroundRgb;
    cell.bold = false;
    cell.dim = false;
    cell.italic = false;
    cell.underline = false;
    cell.strikethrough = false;
    cell.inverse = false;
    cell.wide = false;
    cell.wideContinuation = false;
    return cell;
}

void TerminalEmulator::scrollUp(int topRow, int bottomRow)
{
    const int top = std::clamp(topRow, 0, m_rows - 1);
    const int bottom = std::clamp(bottomRow, top, m_rows - 1);
    if (top >= bottom) {
        return;
    }

    if (!m_inAltBuffer && top == 0) {
        m_scrolledLines.push_back(lineText(0));
    }

    const TerminalCell eraseCell = makeEraseCell();
    auto &cells = activeCells();
    const std::size_t sourceStart = static_cast<std::size_t>((top + 1) * m_cols);
    const std::size_t sourceEnd = static_cast<std::size_t>((bottom + 1) * m_cols);
    const std::size_t targetStart = static_cast<std::size_t>(top * m_cols);
    std::move(cells.begin() + static_cast<std::ptrdiff_t>(sourceStart),
        cells.begin() + static_cast<std::ptrdiff_t>(sourceEnd),
        cells.begin() + static_cast<std::ptrdiff_t>(targetStart));

    for (int col = 0; col < m_cols; ++col) {
        cells[static_cast<std::size_t>(bottom * m_cols + col)] = eraseCell;
    }
}

void TerminalEmulator::scrollDown(int topRow, int bottomRow)
{
    const int top = std::clamp(topRow, 0, m_rows - 1);
    const int bottom = std::clamp(bottomRow, top, m_rows - 1);
    if (top >= bottom) {
        return;
    }

    const TerminalCell eraseCell = makeEraseCell();
    auto &cells = activeCells();
    for (int row = bottom; row > top; --row) {
        for (int col = 0; col < m_cols; ++col) {
            cells[static_cast<std::size_t>(row * m_cols + col)] = cells[static_cast<std::size_t>((row - 1) * m_cols + col)];
        }
    }
    for (int col = 0; col < m_cols; ++col) {
        cells[static_cast<std::size_t>(top * m_cols + col)] = eraseCell;
    }
}

void TerminalEmulator::newline()
{
    if (m_cursorRow == m_scrollBottom) {
        scrollUp(m_scrollTop, m_scrollBottom);
    } else {
        m_cursorRow = std::min(m_rows - 1, m_cursorRow + 1);
    }
}

void TerminalEmulator::applySgr(const std::vector<int> &codes)
{
    for (std::size_t i = 0; i < codes.size(); ++i) {
        const int code = codes[i];
        if (code == 0) {
            m_style = {};
            continue;
        }
        if (code == 1) {
            m_style.bold = true;
            continue;
        }
        if (code == 2) {
            m_style.dim = true;
            continue;
        }
        if (code == 3) {
            m_style.italic = true;
            continue;
        }
        if (code == 4) {
            m_style.underline = true;
            continue;
        }
        if (code == 9) {
            m_style.strikethrough = true;
            continue;
        }
        if (code == 7) {
            m_style.inverse = true;
            continue;
        }
        if (code == 22) {
            m_style.bold = false;
            m_style.dim = false;
            continue;
        }
        if (code == 23) {
            m_style.italic = false;
            continue;
        }
        if (code == 24) {
            m_style.underline = false;
            continue;
        }
        if (code == 29) {
            m_style.strikethrough = false;
            continue;
        }
        if (code == 27) {
            m_style.inverse = false;
            continue;
        }
        if (code == 39) {
            m_style.foreground = TerminalColorIndex::Default;
            m_style.hasForegroundRgb = false;
            m_style.foregroundRgb = QColor();
            continue;
        }
        if (code == 49) {
            m_style.background = TerminalColorIndex::Default;
            m_style.hasBackgroundRgb = false;
            m_style.backgroundRgb = QColor();
            continue;
        }
        if (code >= 30 && code <= 37) {
            m_style.foreground = static_cast<TerminalColorIndex>(code - 30);
            m_style.hasForegroundRgb = false;
            m_style.foregroundRgb = QColor();
            continue;
        }
        if (code >= 40 && code <= 47) {
            m_style.background = static_cast<TerminalColorIndex>(code - 40);
            m_style.hasBackgroundRgb = false;
            m_style.backgroundRgb = QColor();
            continue;
        }
        if (code >= 90 && code <= 97) {
            m_style.foreground = static_cast<TerminalColorIndex>(8 + (code - 90));
            m_style.hasForegroundRgb = false;
            m_style.foregroundRgb = QColor();
            continue;
        }
        if (code >= 100 && code <= 107) {
            m_style.background = static_cast<TerminalColorIndex>(8 + (code - 100));
            m_style.hasBackgroundRgb = false;
            m_style.backgroundRgb = QColor();
            continue;
        }
        if ((code == 38 || code == 48) && (i + 1) < codes.size()) {
            const bool foreground = (code == 38);
            const int mode = codes[i + 1];
            if (mode == 5 && (i + 2) < codes.size()) {
                const int paletteIndex = std::clamp(codes[i + 2], 0, 255);
                const QColor rgb = ansi256ToColor(paletteIndex);
                if (foreground) {
                    m_style.hasForegroundRgb = true;
                    m_style.foregroundRgb = rgb;
                } else {
                    m_style.hasBackgroundRgb = true;
                    m_style.backgroundRgb = rgb;
                }
                i += 2;
                continue;
            }
            if (mode == 2 && (i + 4) < codes.size()) {
                const int r = std::clamp(codes[i + 2], 0, 255);
                const int g = std::clamp(codes[i + 3], 0, 255);
                const int b = std::clamp(codes[i + 4], 0, 255);
                const QColor rgb(r, g, b);
                if (foreground) {
                    m_style.hasForegroundRgb = true;
                    m_style.foregroundRgb = rgb;
                } else {
                    m_style.hasBackgroundRgb = true;
                    m_style.backgroundRgb = rgb;
                }
                i += 4;
                continue;
            }
            if (foreground) {
                m_style.hasForegroundRgb = false;
                m_style.foregroundRgb = QColor();
            } else {
                m_style.hasBackgroundRgb = false;
                m_style.backgroundRgb = QColor();
            }
            continue;
        }
    }
}

void TerminalEmulator::clearScreen()
{
    const TerminalCell eraseCell = makeEraseCell();
    std::fill(activeCells().begin(), activeCells().end(), eraseCell);
    m_cursorRow = 0;
    m_cursorCol = 0;
}

void TerminalEmulator::clearLine(int row, int startCol, int endCol)
{
    if (row < 0 || row >= m_rows) {
        return;
    }
    const int from = std::clamp(startCol, 0, m_cols - 1);
    const int to = std::clamp(endCol, 0, m_cols - 1);
    if (from > to) {
        return;
    }
    const TerminalCell eraseCell = makeEraseCell();
    auto &cells = activeCells();
    for (int col = from; col <= to; ++col) {
        cells[static_cast<std::size_t>(index(row, col))] = eraseCell;
    }
}

void TerminalEmulator::eraseInDisplay(int mode)
{
    if (mode == 2 || mode == 3) {
        const TerminalCell eraseCell = makeEraseCell();
        std::fill(activeCells().begin(), activeCells().end(), eraseCell);
        if (mode == 3) {
            m_scrolledLines.clear();
            m_scrollbackClearRequested = true;
        }
        return;
    }

    if (mode == 0) {
        clearLine(m_cursorRow, m_cursorCol, m_cols - 1);
        for (int row = m_cursorRow + 1; row < m_rows; ++row) {
            clearLine(row, 0, m_cols - 1);
        }
        return;
    }

    if (mode == 1) {
        clearLine(m_cursorRow, 0, m_cursorCol);
        for (int row = 0; row < m_cursorRow; ++row) {
            clearLine(row, 0, m_cols - 1);
        }
    }
}

void TerminalEmulator::eraseInLine(int mode)
{
    if (mode == 2) {
        clearLine(m_cursorRow, 0, m_cols - 1);
        return;
    }
    if (mode == 1) {
        clearLine(m_cursorRow, 0, m_cursorCol);
        return;
    }
    clearLine(m_cursorRow, m_cursorCol, m_cols - 1);
}

bool TerminalEmulator::tryHandleDecrqm(char prefix, char finalChar, const QByteArray &params)
{
    if (prefix != '?' || finalChar != 'p' || params.isEmpty() || params.back() != '$') {
        return false;
    }

    QByteArray modeBytes = params;
    modeBytes.chop(1);
    bool ok = false;
    const int mode = modeBytes.toInt(&ok);

    int state = 0;
    if (ok) {
        switch (mode) {
        case 1:
            state = m_applicationCursorKeys ? 1 : 2;
            break;
        case 25:
            state = m_cursorVisible ? 1 : 2;
            break;
        case 47:
        case 1047:
        case 1049:
            state = m_inAltBuffer ? 1 : 2;
            break;
        case 1000:
            state = m_mouseTrackingMode == MouseTrackingMode::Normal ? 1 : 2;
            break;
        case 1002:
            state = m_mouseTrackingMode == MouseTrackingMode::Button ? 1 : 2;
            break;
        case 1003:
            state = m_mouseTrackingMode == MouseTrackingMode::Any ? 1 : 2;
            break;
        case 1006:
            state = m_mouseSgrMode ? 1 : 2;
            break;
        case 2004:
            state = m_bracketedPasteMode ? 1 : 2;
            break;
        case 2026:
            state = m_synchronizedOutputMode ? 1 : 2;
            break;
        default:
            state = 0;
            break;
        }
    }

    m_pendingResponse.append("\x1b[?" + QByteArray::number(mode) + ";" + QByteArray::number(state) + "$y");
    return true;
}

void TerminalEmulator::handleCsi(char finalChar, QByteArray params)
{
    char prefix = '\0';
    if (!params.isEmpty() && (params[0] == '?' || params[0] == '>')) {
        prefix = params[0];
        params.remove(0, 1);
    }

    const std::vector<int> parsed = parseCsiParameters(params);

    if (prefix == '?' && (finalChar == 'h' || finalChar == 'l')) {
        const bool enabled = finalChar == 'h';
        if (parsed.empty()) {
            return;
        }
        for (int mode : parsed) {
            setPrivateMode(mode, enabled);
        }
        return;
    }

    if (tryHandleDecrqm(prefix, finalChar, params)) {
        return;
    }

    switch (finalChar) {
    case 'A':
        moveCursor(m_cursorRow - effectiveParam(parsed, 0, 1), m_cursorCol);
        return;
    case 'B':
        moveCursor(m_cursorRow + effectiveParam(parsed, 0, 1), m_cursorCol);
        return;
    case 'C':
        moveCursor(m_cursorRow, m_cursorCol + effectiveParam(parsed, 0, 1));
        return;
    case 'D':
        moveCursor(m_cursorRow, m_cursorCol - effectiveParam(parsed, 0, 1));
        return;
    case 'E':
        moveCursor(m_cursorRow + effectiveParam(parsed, 0, 1), 0);
        return;
    case 'F':
        moveCursor(m_cursorRow - effectiveParam(parsed, 0, 1), 0);
        return;
    case 'G':
        moveCursor(m_cursorRow, effectiveParam(parsed, 0, 1) - 1);
        return;
    case 'H':
    case 'f':
        moveCursor(effectiveParam(parsed, 0, 1) - 1, effectiveParam(parsed, 1, 1) - 1);
        return;
    case 'd':
        moveCursor(effectiveParam(parsed, 0, 1) - 1, m_cursorCol);
        return;
    case 'J':
        eraseInDisplay(effectiveParam(parsed, 0, 0));
        return;
    case 'K':
        eraseInLine(effectiveParam(parsed, 0, 0));
        return;
    case 'm':
        applySgr(parsed.empty() ? std::vector<int>{0} : parsed);
        return;
    case 'r':
        if (prefix != '\0') {
            return;
        }
        if (parsed.empty()) {
            resetScrollRegion();
            moveCursor(0, 0);
            return;
        }
        {
            int top = effectiveParam(parsed, 0, 1);
            int bottom = effectiveParam(parsed, 1, m_rows);
            if (top == 0) {
                top = 1;
            }
            if (bottom == 0) {
                bottom = m_rows;
            }
            if (top >= 1 && bottom <= m_rows && top < bottom) {
                setScrollRegion(top - 1, bottom - 1);
                moveCursor(0, 0);
            }
        }
        return;
    case 's':
        if (m_inAltBuffer) {
            m_savedCursorRowAlt = m_cursorRow;
            m_savedCursorColAlt = m_cursorCol;
        } else {
            m_savedCursorRowMain = m_cursorRow;
            m_savedCursorColMain = m_cursorCol;
        }
        return;
    case 'u':
        if (m_inAltBuffer) {
            moveCursor(m_savedCursorRowAlt, m_savedCursorColAlt);
        } else {
            moveCursor(m_savedCursorRowMain, m_savedCursorColMain);
        }
        return;
    case 'n': {
        const int requestType = effectiveParam(parsed, 0, 0);
        if (prefix == '\0' && requestType == 6) {
            m_pendingResponse.append("\x1b[" + QByteArray::number(m_cursorRow + 1) + ";" + QByteArray::number(m_cursorCol + 1) + "R");
        } else if (prefix == '?' && requestType == 6) {
            m_pendingResponse.append("\x1b[?" + QByteArray::number(m_cursorRow + 1) + ";" + QByteArray::number(m_cursorCol + 1) + "R");
        }
        return;
    }
    case 'c':
        if (prefix == '>') {
            m_pendingResponse.append("\x1b[>0;136;0c");
        } else {
            m_pendingResponse.append("\x1b[?1;2c");
        }
        return;
    default:
        return;
    }
}

void TerminalEmulator::handleOsc(const QByteArray &data)
{
    const int separator = data.indexOf(';');
    if (separator <= 0) {
        return;
    }

    const QByteArray code = data.left(separator);
    const QByteArray payload = data.mid(separator + 1);

    if (payload == "?") {
        if (code == "10") {
            m_pendingResponse.append("\x1b]10;rgb:ffff/ffff/ffff\x07");
        } else if (code == "11") {
            m_pendingResponse.append("\x1b]11;rgb:0000/0000/0000\x07");
        }
        return;
    }

    if (code == "4") {
        const QList<QByteArray> parts = payload.split(';');
        if (parts.size() == 2 && parts[1] == "?") {
            m_pendingResponse.append("\x1b]4;" + parts[0] + ";rgb:8080/8080/8080\x07");
        }
    }
}

void TerminalEmulator::setPrivateMode(int mode, bool enabled)
{
    if (mode == 1) {
        m_applicationCursorKeys = enabled;
        return;
    }

    if (mode == 25) {
        m_cursorVisible = enabled;
        return;
    }

    if (mode == 1000) {
        if (enabled) {
            m_mouseTrackingMode = MouseTrackingMode::Normal;
        } else if (m_mouseTrackingMode == MouseTrackingMode::Normal) {
            m_mouseTrackingMode = MouseTrackingMode::Disabled;
        }
        return;
    }

    if (mode == 1002) {
        if (enabled) {
            m_mouseTrackingMode = MouseTrackingMode::Button;
        } else if (m_mouseTrackingMode == MouseTrackingMode::Button) {
            m_mouseTrackingMode = MouseTrackingMode::Disabled;
        }
        return;
    }

    if (mode == 1003) {
        if (enabled) {
            m_mouseTrackingMode = MouseTrackingMode::Any;
        } else if (m_mouseTrackingMode == MouseTrackingMode::Any) {
            m_mouseTrackingMode = MouseTrackingMode::Disabled;
        }
        return;
    }

    if (mode == 1006) {
        m_mouseSgrMode = enabled;
        return;
    }

    if (mode == 2004) {
        m_bracketedPasteMode = enabled;
        return;
    }

    if (mode == 2026) {
        m_synchronizedOutputMode = enabled;
        return;
    }

    if (mode == 47 || mode == 1047 || mode == 1049) {
        if (enabled && !m_inAltBuffer) {
            m_savedCursorRowMain = m_cursorRow;
            m_savedCursorColMain = m_cursorCol;
            std::fill(m_altCells.begin(), m_altCells.end(), TerminalCell{});
            m_inAltBuffer = true;
            m_cursorRow = 0;
            m_cursorCol = 0;
            resetScrollRegion();
            return;
        }
        if (!enabled && m_inAltBuffer) {
            m_inAltBuffer = false;
            moveCursor(m_savedCursorRowMain, m_savedCursorColMain);
            resetScrollRegion();
            return;
        }
    }
}

void TerminalEmulator::resetScrollRegion()
{
    m_scrollTop = 0;
    m_scrollBottom = std::max(0, m_rows - 1);
}

void TerminalEmulator::setScrollRegion(int top, int bottom)
{
    const int clampedTop = std::clamp(top, 0, m_rows - 1);
    const int clampedBottom = std::clamp(bottom, 0, m_rows - 1);
    if (clampedTop >= clampedBottom) {
        return;
    }
    m_scrollTop = clampedTop;
    m_scrollBottom = clampedBottom;
}

std::vector<int> TerminalEmulator::parseCsiParameters(const QByteArray &params) const
{
    std::vector<int> values;
    if (params.isEmpty()) {
        return values;
    }

    QList<QByteArray> parts = params.split(';');
    // Terminfo sgr strings can legally emit trailing ';' before final 'm'.
    // Do not treat that trailing empty field as an implicit SGR 0 reset.
    while (!parts.isEmpty() && parts.back().isEmpty()) {
        parts.removeLast();
    }

    values.reserve(static_cast<std::size_t>(parts.size()));
    for (const QByteArray &part : parts) {
        if (part.isEmpty()) {
            values.push_back(0);
            continue;
        }
        bool ok = false;
        const int value = part.toInt(&ok);
        values.push_back(ok ? value : 0);
    }
    return values;
}

int TerminalEmulator::effectiveParam(const std::vector<int> &params, int index, int fallback) const
{
    if (index < 0 || index >= static_cast<int>(params.size()) || params[static_cast<std::size_t>(index)] == 0) {
        return fallback;
    }
    return params[static_cast<std::size_t>(index)];
}

std::vector<TerminalCell> &TerminalEmulator::activeCells()
{
    return m_inAltBuffer ? m_altCells : m_mainCells;
}

const std::vector<TerminalCell> &TerminalEmulator::activeCells() const
{
    return m_inAltBuffer ? m_altCells : m_mainCells;
}

void TerminalEmulator::moveCursor(int row, int col)
{
    m_cursorRow = std::clamp(row, 0, m_rows - 1);
    m_cursorCol = std::clamp(col, 0, m_cols - 1);
}

int TerminalEmulator::index(int row, int col) const
{
    return row * m_cols + col;
}

} // namespace nord::terminal
