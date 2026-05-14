#pragma once

#include <QAbstractScrollArea>
#include <QTimer>

#include "nordterminal/TerminalEmulator.h"
#include "nordterminal/TerminalProfile.h"
#include "nordterminal/TerminalScrollback.h"
#include "nordterminal/TerminalSelection.h"
#include "nordterminal/TerminalSession.h"
#include "nordterminal/TerminalTheme.h"

namespace nord::terminal {

class TerminalWidget : public QAbstractScrollArea {
    Q_OBJECT

public:
    explicit TerminalWidget(QWidget *parent = nullptr);

    void setTheme(const TerminalTheme &theme);
    [[nodiscard]] TerminalTheme theme() const;
    bool loadThemeFromFile(const QString &path);

    bool startShell(const TerminalProfile &profile = TerminalProfile::defaultForHost());
    void stopShell();

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    bool focusNextPrevChild(bool next) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;

private:
    void recalculateGrid();
    void consumeSessionOutput(const QByteArray &data);
    void flushPendingSessionOutput();
    void resetCursorBlink();
    [[nodiscard]] int visibleStartLine() const;
    [[nodiscard]] int totalLines() const;
    [[nodiscard]] QPoint toViewportCell(const QPoint &pixelPos) const;
    [[nodiscard]] QPoint toAbsoluteCell(const QPoint &pixelPos) const;
    [[nodiscard]] QString lineTextAtAbsolute(int absoluteLine) const;
    [[nodiscard]] QString selectedText() const;
    void maybeSendMouseReport(QMouseEvent *event, bool release);

    TerminalTheme m_theme = TerminalTheme::nordDark();
    TerminalEmulator m_emulator;
    TerminalSession m_session;
    TerminalScrollback m_scrollback;
    TerminalSelection m_selection;

    int m_cellWidth = 0;
    int m_cellHeight = 0;
    int m_ascent = 0;
    int m_scrollOffset = 0;
    int m_wheelRemainder = 0;
    QTimer m_cursorBlinkTimer;
    bool m_cursorBlinkVisible = true;
    QByteArray m_pendingSessionOutput;
    bool m_outputFlushQueued = false;
};

} // namespace nord::terminal
