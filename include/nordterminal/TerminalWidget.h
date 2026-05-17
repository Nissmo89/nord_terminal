#pragma once

#include <QAbstractScrollArea>
#include <QFile>
#include <QFont>
#include <QRect>
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
    void rebuildFontCache();
    [[nodiscard]] const QFont &cachedFont(bool bold, bool italic) const;
    [[nodiscard]] QRect cursorViewportRect() const;
    void consumeSessionOutput(const QByteArray &data);
    void flushPendingSessionOutput();
    void resetCursorBlink();
    void initializeTraceLogging();
    void traceLog(const QString &message);
    void traceBytes(const QString &label, const QByteArray &data);
    void traceViewportSnapshot(const QString &label);
    [[nodiscard]] bool isVerboseTraceEnabled() const;
    [[nodiscard]] int visibleStartLine() const;
    [[nodiscard]] int totalLines() const;
    [[nodiscard]] QPoint toViewportCell(const QPoint &pixelPos) const;
    [[nodiscard]] QPoint toAbsoluteCell(const QPoint &pixelPos) const;
    [[nodiscard]] QString lineTextAtAbsolute(int absoluteLine) const;
    [[nodiscard]] QString selectedText() const;
    void maybeSendMouseReport(QMouseEvent *event, bool release);
    void paintEmulatorRows(QPainter &painter, int firstRow, int lastRow, int rows, int cols);
    void paintCursor(QPainter &painter);

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
    QFont m_fontRegular;
    QFont m_fontBold;
    QFont m_fontItalic;
    QFont m_fontBoldItalic;
    bool m_traceEnabled = false;
    bool m_traceVerbose = false;
    QFile m_traceFile;
    QFile m_traceSummaryFile;
    quint64 m_traceEventId = 0;
    int m_traceSnapshotMaxRows = 0;
};

} // namespace nord::terminal
