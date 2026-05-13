#include "nordterminal/TerminalWidget.h"

#include "nordterminal/TerminalKeyMapper.h"
#include "nordterminal/TerminalThemeLoader.h"

#include <QApplication>
#include <QClipboard>
#include <QFocusEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
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
    verticalScrollBar()->setRange(0, 0);
    verticalScrollBar()->setValue(0);
    m_scrollOffset = 0;

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
    Q_UNUSED(event);
    QPainter painter(viewport());
    painter.fillRect(viewport()->rect(), m_theme.background);
    painter.setFont(m_theme.font);

    const int rows = m_emulator.rows();
    const int cols = m_emulator.cols();
    const int totalLines = m_scrollback.size() + rows;
    const int startLine = std::clamp(totalLines - rows - m_scrollOffset, 0, std::max(0, totalLines - rows));

    for (int row = 0; row < rows; ++row) {
        const int absoluteLine = startLine + row;
        const int y = row * m_cellHeight;

        if (absoluteLine < m_scrollback.size()) {
            painter.setPen(m_theme.foreground);
            painter.drawText(0, y + m_ascent, m_scrollback.slice(absoluteLine, 1).front());
            continue;
        }

        const int emulatorRow = absoluteLine - m_scrollback.size();
        for (int col = 0; col < cols; ++col) {
            TerminalCell cell = m_emulator.cellAt(emulatorRow, col);
            QColor background = cell.hasBackgroundRgb ? cell.backgroundRgb : m_theme.resolveBackground(cell.background);
            QColor foreground = cell.hasForegroundRgb ? cell.foregroundRgb : m_theme.resolveForeground(cell.foreground);
            if (cell.inverse) {
                std::swap(background, foreground);
            }
            if (cellSelected(emulatorRow, col)) {
                background = m_theme.selection;
            }

            const int x = col * m_cellWidth;
            painter.fillRect(x, y, m_cellWidth, m_cellHeight, background);
            painter.setPen(foreground);
            painter.drawText(x, y + m_ascent, QString(cell.character));
        }
    }

    if (hasFocus() && m_scrollOffset == 0 && m_emulator.cursorVisible()) {
        const QPoint cursor = m_emulator.cursorPosition();
        const QRect cursorRect(cursor.x() * m_cellWidth, cursor.y() * m_cellHeight, m_cellWidth, m_cellHeight);
        painter.fillRect(cursorRect, m_theme.cursor);
        const QChar cursorChar = m_emulator.cellAt(cursor.y(), cursor.x()).character;
        painter.setPen(m_theme.background);
        painter.drawText(cursorRect.x(), cursorRect.y() + m_ascent, QString(cursorChar));
    }
}

void TerminalWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_C) {
        if (!m_selection.isActive()) {
            return;
        }
        const QPoint a = m_selection.start();
        const QPoint b = m_selection.end();
        const int minRow = std::min(a.y(), b.y());
        const int maxRow = std::max(a.y(), b.y());
        const int minCol = std::min(a.x(), b.x());
        const int maxCol = std::max(a.x(), b.x());

        QString text;
        for (int row = minRow; row <= maxRow; ++row) {
            for (int col = minCol; col <= maxCol; ++col) {
                text.append(m_emulator.cellAt(row, col).character);
            }
            if (row != maxRow) {
                text.append('\n');
            }
        }
        QApplication::clipboard()->setText(text);
        return;
    }

    if (event->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier) && event->key() == Qt::Key_V) {
        m_session.writeInput(QApplication::clipboard()->text().toUtf8());
        return;
    }

    const QByteArray mapped = TerminalKeyMapper::mapKeyEvent(event, m_emulator.applicationCursorKeys());
    if (!mapped.isEmpty()) {
        m_session.writeInput(mapped);
    }
}

void TerminalWidget::resizeEvent(QResizeEvent *event)
{
    QAbstractScrollArea::resizeEvent(event);
    recalculateGrid();
}

void TerminalWidget::mousePressEvent(QMouseEvent *event)
{
    setFocus();
    if (event->button() == Qt::LeftButton) {
        m_selection.begin(toCell(event->pos()));
        viewport()->update();
    }
    QAbstractScrollArea::mousePressEvent(event);
}

void TerminalWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (event->buttons().testFlag(Qt::LeftButton)) {
        m_selection.update(toCell(event->pos()));
        viewport()->update();
    }
    QAbstractScrollArea::mouseMoveEvent(event);
}

void TerminalWidget::wheelEvent(QWheelEvent *event)
{
    const int delta = event->angleDelta().y();
    const int steps = (delta == 0) ? 0 : (delta > 0 ? -1 : 1);
    verticalScrollBar()->setValue(verticalScrollBar()->value() + steps);
    event->accept();
}

void TerminalWidget::focusInEvent(QFocusEvent *event)
{
    QAbstractScrollArea::focusInEvent(event);
    viewport()->update();
}

void TerminalWidget::focusOutEvent(QFocusEvent *event)
{
    QAbstractScrollArea::focusOutEvent(event);
    viewport()->update();
}

void TerminalWidget::recalculateGrid()
{
    QFontMetrics metrics(m_theme.font);
    m_cellWidth = std::max(1, metrics.horizontalAdvance(QChar('M')));
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
    const bool stickToBottom = (verticalScrollBar()->value() == verticalScrollBar()->maximum());
    m_emulator.feedOutput(data);
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
    viewport()->update();
}

QPoint TerminalWidget::toCell(const QPoint &pixelPos) const
{
    const int col = std::clamp(pixelPos.x() / std::max(1, m_cellWidth), 0, m_emulator.cols() - 1);
    const int row = std::clamp(pixelPos.y() / std::max(1, m_cellHeight), 0, m_emulator.rows() - 1);
    return {col, row};
}

bool TerminalWidget::cellSelected(int row, int col) const
{
    if (!m_selection.isActive()) {
        return false;
    }
    const QPoint a = m_selection.start();
    const QPoint b = m_selection.end();
    const int minRow = std::min(a.y(), b.y());
    const int maxRow = std::max(a.y(), b.y());
    const int minCol = std::min(a.x(), b.x());
    const int maxCol = std::max(a.x(), b.x());
    return row >= minRow && row <= maxRow && col >= minCol && col <= maxCol;
}

} // namespace nord::terminal
