#pragma once

#include <QByteArray>
#include <QPoint>
#include <QString>
#include <vector>

#include "nordterminal/TerminalCell.h"

namespace nord::terminal {

class TerminalEmulator {
public:
    using Line = std::vector<TerminalCell>;

    TerminalEmulator(int rows = 24, int cols = 80);

    void resize(int rows, int cols);
    void reset();
    void feedOutput(const QByteArray &data);
    QByteArray takePendingResponse();

    [[nodiscard]] int rows() const;
    [[nodiscard]] int cols() const;
    [[nodiscard]] QPoint cursorPosition() const;
    [[nodiscard]] bool cursorVisible() const;
    [[nodiscard]] bool applicationCursorKeys() const;
    [[nodiscard]] bool bracketedPasteMode() const;
    [[nodiscard]] bool mouseTrackingEnabled() const;
    [[nodiscard]] bool mouseButtonTrackingEnabled() const;
    [[nodiscard]] bool mouseAnyTrackingEnabled() const;
    [[nodiscard]] bool mouseSgrMode() const;
    [[nodiscard]] const TerminalCell &cellAt(int row, int col) const;
    [[nodiscard]] const TerminalCell *rowData(int row) const;
    [[nodiscard]] QString lineText(int row) const;
    std::vector<Line> takeScrolledLines();
    [[nodiscard]] bool takeScrollbackClearRequested();
    [[nodiscard]] bool takeDirtyRowSpan(int &topRow, int &bottomRow);
    [[nodiscard]] int takePendingViewportScrollLines();

private:
    enum class ParserState {
        Ground,
        Escape,
        EscapeIntermediate,
        Csi,
        Osc,
        OscEscape
    };

    enum class Charset {
        Ascii,
        DecSpecialGraphics
    };

    enum class MouseTrackingMode {
        Disabled,
        Normal,
        Button,
        Any
    };

    struct RenderStyle {
        TerminalColorIndex foreground = TerminalColorIndex::Default;
        TerminalColorIndex background = TerminalColorIndex::Default;
        bool hasForegroundRgb = false;
        bool hasBackgroundRgb = false;
        QColor foregroundRgb = QColor();
        QColor backgroundRgb = QColor();
        bool bold = false;
        bool dim = false;
        bool italic = false;
        bool underline = false;
        bool strikethrough = false;
        bool inverse = false;
    };

    void feedByte(unsigned char ch);
    void feedUtf8Byte(unsigned char ch);
    void flushIncompleteUtf8();
    static QColor ansi256ToColor(int index);
    QChar mapDecSpecialGraphicsChar(unsigned char ch) const;
    static int codepointDisplayWidth(char32_t codepoint);
    Charset activeCharset() const;
    void handleEscapeIntermediateFinal(unsigned char finalByte);
    TerminalCell makeEraseCell() const;
    void putCodepoint(char32_t codepoint);
    void scrollUp(int topRow, int bottomRow);
    void scrollDown(int topRow, int bottomRow);
    void newline();
    void applySgr(const std::vector<int> &codes);
    void clearScreen();
    void clearLine(int row, int startCol, int endCol);
    void eraseInDisplay(int mode);
    void eraseInLine(int mode);
    void insertBlankChars(int count);
    void deleteChars(int count);
    void eraseChars(int count);
    void insertLines(int count);
    void deleteLines(int count);
    bool tryHandleDecrqm(char prefix, char finalChar, const QByteArray &params);
    void handleCsi(char finalChar, QByteArray params);
    void handleOsc(const QByteArray &data);
    void setPrivateMode(int mode, bool enabled);
    void resetScrollRegion();
    void setScrollRegion(int top, int bottom);
    [[nodiscard]] std::vector<int> parseCsiParameters(const QByteArray &params) const;
    [[nodiscard]] int effectiveParam(const std::vector<int> &params, int index, int fallback) const;
    [[nodiscard]] std::vector<TerminalCell> &activeCells();
    [[nodiscard]] const std::vector<TerminalCell> &activeCells() const;
    void moveCursor(int row, int col);
    [[nodiscard]] int index(int row, int col) const;
    void markDirtyAll();
    void markDirtyRow(int row);
    void markDirtyRange(int topRow, int bottomRow);
    void recordViewportScroll(int lines);

    int m_rows = 24;
    int m_cols = 80;
    int m_cursorRow = 0;
    int m_cursorCol = 0;
    int m_savedCursorRowMain = 0;
    int m_savedCursorColMain = 0;
    int m_savedCursorRowAlt = 0;
    int m_savedCursorColAlt = 0;
    bool m_cursorVisible = true;
    bool m_applicationCursorKeys = false;
    bool m_bracketedPasteMode = false;
    MouseTrackingMode m_mouseTrackingMode = MouseTrackingMode::Disabled;
    bool m_mouseSgrMode = false;
    bool m_synchronizedOutputMode = false;
    bool m_autoWrapMode = true;
    bool m_insertMode = false;
    bool m_inAltBuffer = false;
    int m_scrollTop = 0;
    int m_scrollBottom = 23;
    ParserState m_parserState = ParserState::Ground;
    QByteArray m_csiParams;
    QByteArray m_oscData;
    QByteArray m_pendingResponse;
    QByteArray m_escapeIntermediates;
    QByteArray m_utf8Pending;
    int m_utf8ExpectedBytes = 0;
    Charset m_charsetG0 = Charset::Ascii;
    Charset m_charsetG1 = Charset::Ascii;
    bool m_useG1 = false;
    RenderStyle m_style;
    std::vector<TerminalCell> m_mainCells;
    std::vector<TerminalCell> m_altCells;
    std::vector<Line> m_scrolledLines;
    bool m_scrollbackClearRequested = false;
    bool m_hasDirtyRows = true;
    int m_dirtyTopRow = 0;
    int m_dirtyBottomRow = 0;
    int m_pendingViewportScrollLines = 0;
};

} // namespace nord::terminal
