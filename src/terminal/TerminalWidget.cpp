#include "nordterminal/TerminalWidget.h"

#include "nordterminal/TerminalKeyMapper.h"
#include "nordterminal/TerminalThemeLoader.h"

#include <QApplication>
#include <QClipboard>
#include <QFocusEvent>
#include <QFont>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>

namespace nord::terminal {

TerminalWidget::TerminalWidget(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setFont(m_theme.font);
    viewport()->setAutoFillBackground(false);

    connect(&m_session, &TerminalSession::outputReceived, this, &TerminalWidget::consumeSessionOutput);
    connect(&m_session, &TerminalSession::sessionError, this, [this](const QString &message) {
        consumeSessionOutput(message.toUtf8() + QByteArray("\r\n"));
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        m_scrollOffset = verticalScrollBar()->maximum() - value;
        viewport()->update();
    });

    m_cursorBlinkTimer.setInterval(500);
    connect(&m_cursorBlinkTimer, &QTimer::timeout, this, [this]() {
        if (!hasFocus() || !m_emulator.cursorVisible() || m_scrollOffset != 0) {
            if (!m_cursorBlinkVisible) {
                m_cursorBlinkVisible = true;
                viewport()->update();
            }
            return;
        }
        m_cursorBlinkVisible = !m_cursorBlinkVisible;
        viewport()->update();
    });
    m_cursorBlinkTimer.start();

    recalculateGrid();
}

void TerminalWidget::setTheme(const TerminalTheme &theme)
{
    m_theme = theme;
    setFont(m_theme.font);
    recalculateGrid();
    viewport()->update();
}

TerminalTheme TerminalWidget::theme() const
{
    return m_theme;
}

bool TerminalWidget::loadThemeFromFile(const QString &path)
{
    const ThemeLoadResult result = TerminalThemeLoader::loadFromFile(path);
    if (!result.ok) {
        return false;
    }
    setTheme(result.theme);
    return true;
}

bool TerminalWidget::startShell(const TerminalProfile &profile)
{
    m_selection.clear();
    m_emulator.reset();
    m_scrollback = TerminalScrollback(m_scrollback.maxLines());
    m_pendingSessionOutput.clear();
    m_outputFlushQueued = false;
    verticalScrollBar()->setRange(0, 0);
    verticalScrollBar()->setValue(0);
    m_scrollOffset = 0;
    m_wheelRemainder = 0;
    resetCursorBlink();

    const bool started = m_session.start(profile);
    if (started) {
        m_session.resizePty(m_emulator.rows(), m_emulator.cols());
    }
    viewport()->update();
    return started;
}

void TerminalWidget::stopShell()
{
    m_session.terminate();
}

void TerminalWidget::paintEvent(QPaintEvent *event)
{
    QPainter painter(viewport());
    painter.setFont(m_theme.font);

    const QRect dirtyRect = event ? event->rect() : viewport()->rect();
    painter.fillRect(dirtyRect, m_theme.background);

    const int rows = m_emulator.rows();
    const int cols = m_emulator.cols();
    if (rows <= 0 || cols <= 0) {
        return;
    }

    const int startLine = visibleStartLine();
    const int firstRow = std::clamp(dirtyRect.top() / std::max(1, m_cellHeight), 0, rows - 1);
    const int lastRow = std::clamp(dirtyRect.bottom() / std::max(1, m_cellHeight), 0, rows - 1);

    QFont styledFont = m_theme.font;
    bool fontInitialized = false;
    bool lastBold = false;
    bool lastItalic = false;
    bool lastUnderline = false;
    bool lastStrike = false;

    for (int row = firstRow; row <= lastRow; ++row) {
        const int absoluteLine = startLine + row;
        const int y = row * m_cellHeight;

        if (absoluteLine < m_scrollback.size()) {
            const QString &line = m_scrollback.lineAt(absoluteLine);
            for (int col = 0; col < cols; ++col) {
                const int x = col * m_cellWidth;
                const bool selected = cellSelected(absoluteLine, col);
                const QColor background = selected ? m_theme.selection : m_theme.background;
                painter.fillRect(x, y, m_cellWidth, m_cellHeight, background);

                const QChar ch = (col < line.size()) ? line[col] : QChar(' ');
                if (ch != QChar(' ')) {
                    painter.setPen(m_theme.foreground);
                    painter.setFont(m_theme.font);
                    painter.drawText(x, y + m_ascent, QString(ch));
                }
            }
            continue;
        }

        const int emulatorRow = absoluteLine - m_scrollback.size();
        for (int col = 0; col < cols; ++col) {
            const TerminalCell cell = m_emulator.cellAt(emulatorRow, col);
            QColor background = cell.hasBackgroundRgb ? cell.backgroundRgb : m_theme.resolveBackground(cell.background);
            QColor foreground = cell.hasForegroundRgb ? cell.foregroundRgb : m_theme.resolveForeground(cell.foreground);
            if (cell.inverse) {
                std::swap(background, foreground);
            }
            if (cell.dim) {
                foreground.setAlphaF(0.7);
            }
            if (cellSelected(absoluteLine, col)) {
                background = m_theme.selection;
            }

            const int widthCells = (cell.wide && col + 1 < cols) ? 2 : 1;
            const int x = col * m_cellWidth;
            painter.fillRect(x, y, m_cellWidth * widthCells, m_cellHeight, background);
            if (cell.wideContinuation) {
                continue;
            }

            if (!fontInitialized || cell.bold != lastBold || cell.italic != lastItalic
                || cell.underline != lastUnderline || cell.strikethrough != lastStrike) {
                styledFont = m_theme.font;
                styledFont.setBold(cell.bold);
                styledFont.setItalic(cell.italic);
                styledFont.setUnderline(cell.underline);
                styledFont.setStrikeOut(cell.strikethrough);
                painter.setFont(styledFont);

                fontInitialized = true;
                lastBold = cell.bold;
                lastItalic = cell.italic;
                lastUnderline = cell.underline;
                lastStrike = cell.strikethrough;
            }

            painter.setPen(foreground);
            painter.drawText(x, y + m_ascent, QString(cell.character));
        }
    }

    if (hasFocus() && m_scrollOffset == 0 && m_emulator.cursorVisible() && m_cursorBlinkVisible) {
        const QPoint cursor = m_emulator.cursorPosition();
        const QRect cursorRect(cursor.x() * m_cellWidth, cursor.y() * m_cellHeight, m_cellWidth, m_cellHeight);
        painter.fillRect(cursorRect, m_theme.cursor);
        const QChar cursorChar = m_emulator.cellAt(cursor.y(), cursor.x()).character;
        painter.setPen(m_theme.background);
        painter.setFont(m_theme.font);
        painter.drawText(cursorRect.x(), cursorRect.y() + m_ascent, QString(cursorChar));
    }
}

void TerminalWidget::keyPressEvent(QKeyEvent *event)
{
    resetCursorBlink();

    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const bool hasCtrl = modifiers.testFlag(Qt::ControlModifier);
    const bool hasShift = modifiers.testFlag(Qt::ShiftModifier);
    const bool hasAlt = modifiers.testFlag(Qt::AltModifier);
    const bool hasMeta = modifiers.testFlag(Qt::MetaModifier);

    if (hasCtrl && hasShift && !hasAlt && !hasMeta && event->key() == Qt::Key_C) {
        if (m_selection.isActive()) {
            QApplication::clipboard()->setText(selectedText());
        } else {
            m_session.writeInput(QByteArray(1, '\x03'));
        }
        event->accept();
        return;
    }

    if (hasCtrl && hasShift && !hasAlt && !hasMeta && event->key() == Qt::Key_V) {
        QByteArray pasteData = QApplication::clipboard()->text().toUtf8();
        if (m_emulator.bracketedPasteMode()) {
            pasteData.prepend("\x1b[200~");
            pasteData.append("\x1b[201~");
        }
        m_session.writeInput(pasteData);
        event->accept();
        return;
    }

    if (hasCtrl && !hasShift && !hasAlt && !hasMeta && event->key() == Qt::Key_C) {
        m_session.writeInput(QByteArray(1, '\x03'));
        event->accept();
        return;
    }

    const QByteArray mapped = TerminalKeyMapper::mapKeyEvent(event, m_emulator.applicationCursorKeys());
    if (!mapped.isEmpty()) {
        m_session.writeInput(mapped);
        event->accept();
        return;
    }

    QAbstractScrollArea::keyPressEvent(event);
}

void TerminalWidget::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    recalculateGrid();
}

bool TerminalWidget::focusNextPrevChild(bool next)
{
    Q_UNUSED(next);
    // Keep Tab/Backtab inside the terminal so it reaches the PTY (autocomplete, etc.).
    return false;
}

void TerminalWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    resetCursorBlink();

    if (m_emulator.mouseTrackingEnabled() && !event->modifiers().testFlag(Qt::ShiftModifier)) {
        maybeSendMouseReport(event, false);
        QAbstractScrollArea::mousePressEvent(event);
        return;
    }

    if (event->button() == Qt::LeftButton) {
        m_selection.begin(toAbsoluteCell(event->pos()));
        viewport()->update();
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void TerminalWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_emulator.mouseTrackingEnabled() && !event->modifiers().testFlag(Qt::ShiftModifier)) {
        maybeSendMouseReport(event, false);
        QAbstractScrollArea::mouseMoveEvent(event);
        return;
    }

    if (event->buttons().testFlag(Qt::LeftButton)) {
        m_selection.update(toAbsoluteCell(event->pos()));
        viewport()->update();
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void TerminalWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_emulator.mouseTrackingEnabled() && !event->modifiers().testFlag(Qt::ShiftModifier)) {
        maybeSendMouseReport(event, true);
        QAbstractScrollArea::mouseReleaseEvent(event);
        return;
    }

    QAbstractScrollArea::mouseReleaseEvent(event);
}

void TerminalWidget::wheelEvent(QWheelEvent *event)
{
    if (m_emulator.mouseTrackingEnabled() && !event->modifiers().testFlag(Qt::ShiftModifier)) {
        const int delta = event->angleDelta().y();
        if (delta != 0) {
            const int buttonCode = delta > 0 ? 64 : 65;
            const QPoint viewportCell = toViewportCell(event->position().toPoint());
            const int x = viewportCell.x() + 1;
            const int y = viewportCell.y() + 1;
            QByteArray report;
            if (m_emulator.mouseSgrMode()) {
                report = "\x1b[<" + QByteArray::number(buttonCode) + ";" + QByteArray::number(x) + ";" + QByteArray::number(y) + "M";
            } else {
                const int legacyX = std::clamp(x, 1, 223);
                const int legacyY = std::clamp(y, 1, 223);
                report = "\x1b[M";
                report.append(static_cast<char>(buttonCode + 32));
                report.append(static_cast<char>(legacyX + 32));
                report.append(static_cast<char>(legacyY + 32));
            }
            m_session.writeInput(report);
        }
        event->accept();
        return;
    }

    m_wheelRemainder += event->angleDelta().y();
    const int notches = m_wheelRemainder / 120;
    if (notches != 0) {
        verticalScrollBar()->setValue(verticalScrollBar()->value() - notches);
        m_wheelRemainder -= notches * 120;
    }
    event->accept();
}

void TerminalWidget::focusInEvent(QFocusEvent *event)
{
    QAbstractScrollArea::focusInEvent(event);
    resetCursorBlink();
    viewport()->update();
}

void TerminalWidget::focusOutEvent(QFocusEvent *event)
{
    QAbstractScrollArea::focusOutEvent(event);
    m_cursorBlinkVisible = true;
    viewport()->update();
}

void TerminalWidget::recalculateGrid()
{
    QFontMetrics metrics(m_theme.font);
    m_cellWidth = std::max(1, metrics.averageCharWidth());
    m_cellHeight = std::max(1, metrics.height());
    m_ascent = metrics.ascent();

    const int rows = std::max(1, viewport()->height() / m_cellHeight);
    const int cols = std::max(1, viewport()->width() / m_cellWidth);

    m_emulator.resize(rows, cols);
    m_session.resizePty(rows, cols);

    verticalScrollBar()->setPageStep(rows);
    verticalScrollBar()->setRange(0, std::max(0, m_scrollback.size()));
    if (verticalScrollBar()->value() == verticalScrollBar()->maximum()) {
        m_scrollOffset = 0;
    }
}

void TerminalWidget::consumeSessionOutput(const QByteArray &data)
{
    if (data.isEmpty()) {
        return;
    }

    m_pendingSessionOutput.append(data);
    constexpr qsizetype immediateFlushThreshold = 256 * 1024;
    if (m_pendingSessionOutput.size() >= immediateFlushThreshold) {
        flushPendingSessionOutput();
        return;
    }
    if (m_outputFlushQueued) {
        return;
    }

    m_outputFlushQueued = true;
    QTimer::singleShot(0, this, [this]() {
        m_outputFlushQueued = false;
        flushPendingSessionOutput();
    });
}

void TerminalWidget::flushPendingSessionOutput()
{
    if (m_pendingSessionOutput.isEmpty()) {
        return;
    }

    const bool stickToBottom = (verticalScrollBar()->value() == verticalScrollBar()->maximum());
    QByteArray chunk = std::move(m_pendingSessionOutput);
    m_pendingSessionOutput.clear();

    m_emulator.feedOutput(chunk);

    if (m_emulator.takeScrollbackClearRequested()) {
        m_scrollback = TerminalScrollback(m_scrollback.maxLines());
        m_selection.clear();
    }

    const QByteArray terminalReply = m_emulator.takePendingResponse();
    if (!terminalReply.isEmpty()) {
        m_session.writeInput(terminalReply);
    }
    for (const QString &line : m_emulator.takeScrolledLines()) {
        m_scrollback.pushLine(line);
    }

    verticalScrollBar()->setRange(0, std::max(0, m_scrollback.size()));
    if (stickToBottom) {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    }

    resetCursorBlink();
    viewport()->update();

    if (!m_pendingSessionOutput.isEmpty() && !m_outputFlushQueued) {
        m_outputFlushQueued = true;
        QTimer::singleShot(0, this, [this]() {
            m_outputFlushQueued = false;
            flushPendingSessionOutput();
        });
    }
}

void TerminalWidget::resetCursorBlink()
{
    m_cursorBlinkVisible = true;
}

int TerminalWidget::visibleStartLine() const
{
    return std::clamp(totalLines() - m_emulator.rows() - m_scrollOffset, 0, std::max(0, totalLines() - m_emulator.rows()));
}

int TerminalWidget::totalLines() const
{
    return m_scrollback.size() + m_emulator.rows();
}

QPoint TerminalWidget::toViewportCell(const QPoint &pixelPos) const
{
    const int col = std::clamp(pixelPos.x() / std::max(1, m_cellWidth), 0, m_emulator.cols() - 1);
    const int row = std::clamp(pixelPos.y() / std::max(1, m_cellHeight), 0, m_emulator.rows() - 1);
    return {col, row};
}

QPoint TerminalWidget::toAbsoluteCell(const QPoint &pixelPos) const
{
    const QPoint viewportCell = toViewportCell(pixelPos);
    return {viewportCell.x(), visibleStartLine() + viewportCell.y()};
}

QString TerminalWidget::lineTextAtAbsolute(int absoluteLine) const
{
    if (absoluteLine < 0 || absoluteLine >= totalLines()) {
        return {};
    }
    if (absoluteLine < m_scrollback.size()) {
        return m_scrollback.lineAt(absoluteLine);
    }
    return m_emulator.lineText(absoluteLine - m_scrollback.size());
}

QString TerminalWidget::selectedText() const
{
    if (!m_selection.isActive()) {
        return {};
    }

    const int maxAbsoluteLine = totalLines() - 1;
    if (maxAbsoluteLine < 0) {
        return {};
    }

    QPoint start = m_selection.start();
    QPoint end = m_selection.end();
    start.setY(std::clamp(start.y(), 0, maxAbsoluteLine));
    end.setY(std::clamp(end.y(), 0, maxAbsoluteLine));
    if (start.y() > end.y() || (start.y() == end.y() && start.x() > end.x())) {
        std::swap(start, end);
    }

    const int maxCol = m_emulator.cols() - 1;
    QString text;
    for (int row = start.y(); row <= end.y(); ++row) {
        const QString line = lineTextAtAbsolute(row);
        const int startCol = (row == start.y()) ? std::clamp(start.x(), 0, maxCol) : 0;
        const int endCol = (row == end.y()) ? std::clamp(end.x(), 0, maxCol) : maxCol;

        QString segment;
        for (int col = startCol; col <= endCol; ++col) {
            segment.append(col < line.size() ? line[col] : QChar(' '));
        }
        while (!segment.isEmpty() && segment.back() == QChar(' ')) {
            segment.chop(1);
        }

        text.append(segment);
        if (row != end.y()) {
            text.append('\n');
        }
    }
    return text;
}

bool TerminalWidget::cellSelected(int absoluteRow, int col) const
{
    if (!m_selection.isActive()) {
        return false;
    }

    const int maxAbsoluteLine = totalLines() - 1;
    if (maxAbsoluteLine < 0) {
        return false;
    }

    QPoint start = m_selection.start();
    QPoint end = m_selection.end();
    start.setY(std::clamp(start.y(), 0, maxAbsoluteLine));
    end.setY(std::clamp(end.y(), 0, maxAbsoluteLine));
    if (start.y() > end.y() || (start.y() == end.y() && start.x() > end.x())) {
        std::swap(start, end);
    }

    if (absoluteRow < start.y() || absoluteRow > end.y()) {
        return false;
    }

    if (start.y() == end.y()) {
        return col >= start.x() && col <= end.x();
    }
    if (absoluteRow == start.y()) {
        return col >= start.x();
    }
    if (absoluteRow == end.y()) {
        return col <= end.x();
    }
    return true;
}

void TerminalWidget::maybeSendMouseReport(QMouseEvent *event, bool release)
{
    if (!m_emulator.mouseTrackingEnabled()) {
        return;
    }

    const bool isMoveEvent = event->type() == QEvent::MouseMove;
    int code = -1;
    bool useReleaseSuffix = release;

    auto buttonToCode = [](Qt::MouseButton button) {
        switch (button) {
        case Qt::LeftButton:
            return 0;
        case Qt::MiddleButton:
            return 1;
        case Qt::RightButton:
            return 2;
        default:
            return -1;
        }
    };

    if (isMoveEvent) {
        if (!m_emulator.mouseAnyTrackingEnabled() && !m_emulator.mouseButtonTrackingEnabled()) {
            return;
        }
        if (!m_emulator.mouseAnyTrackingEnabled() && event->buttons() == Qt::NoButton) {
            return;
        }

        if (event->buttons().testFlag(Qt::LeftButton)) {
            code = 0;
        } else if (event->buttons().testFlag(Qt::MiddleButton)) {
            code = 1;
        } else if (event->buttons().testFlag(Qt::RightButton)) {
            code = 2;
        } else {
            code = 3;
        }
        code += 32;
        useReleaseSuffix = false;
    } else if (release) {
        code = 3;
    } else {
        code = buttonToCode(event->button());
        if (code < 0) {
            return;
        }
    }

    const QPoint cell = toViewportCell(event->pos());
    const int x = cell.x() + 1;
    const int y = cell.y() + 1;
    QByteArray report;
    if (m_emulator.mouseSgrMode()) {
        report = "\x1b[<" + QByteArray::number(code) + ";" + QByteArray::number(x) + ";" + QByteArray::number(y)
            + (useReleaseSuffix ? "m" : "M");
    } else {
        const int legacyX = std::clamp(x, 1, 223);
        const int legacyY = std::clamp(y, 1, 223);
        report = "\x1b[M";
        report.append(static_cast<char>(code + 32));
        report.append(static_cast<char>(legacyX + 32));
        report.append(static_cast<char>(legacyY + 32));
    }

    m_session.writeInput(report);
    event->accept();
}

} // namespace nord::terminal
