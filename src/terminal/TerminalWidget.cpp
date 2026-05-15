#include "nordterminal/TerminalWidget.h"

#include "nordterminal/TerminalKeyMapper.h"
#include "nordterminal/TerminalThemeLoader.h"

#include <QApplication>
#include <QClipboard>
#include <QFocusEvent>
#include <QFont>
#include <QFontDatabase>
#if defined(Q_OS_WIN)
#include <QFontInfo>
#endif
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QWheelEvent>

#include <algorithm>
#include <utility>

namespace nord::terminal {

namespace {

QString scalarFromCodepoint(uint codepoint)
{
    const char32_t scalar = static_cast<char32_t>(codepoint);
    return QString::fromUcs4(&scalar, 1);
}

bool sameTextStyle(const TerminalCell &lhs, const TerminalCell &rhs)
{
    return lhs.bold == rhs.bold
        && lhs.italic == rhs.italic
        && lhs.underline == rhs.underline
        && lhs.strikethrough == rhs.strikethrough
        && lhs.dim == rhs.dim
        && lhs.inverse == rhs.inverse
        && lhs.foreground == rhs.foreground
        && lhs.background == rhs.background
        && lhs.hasForegroundRgb == rhs.hasForegroundRgb
        && lhs.hasBackgroundRgb == rhs.hasBackgroundRgb
        && lhs.foregroundRgb == rhs.foregroundRgb
        && lhs.backgroundRgb == rhs.backgroundRgb;
}

QFont withSymbolFallbacks(const QFont &baseFont)
{
    QFont font(baseFont);

    QStringList families = font.families();
    if (families.isEmpty()) {
        families << font.family();
    }

#if defined(Q_OS_WIN)
    const QStringList preferredFallbackFamilies = {
        QStringLiteral("Cascadia Mono"),
        QStringLiteral("Cascadia Code"),
        QStringLiteral("Consolas"),
        QStringLiteral("Lucida Console"),
        QStringLiteral("JetBrainsMono Nerd Font"),
        QStringLiteral("JetBrainsMono Nerd Font Mono"),
        QStringLiteral("JetBrains Mono Nerd Font"),
        QStringLiteral("JetBrains Mono Nerd Font Mono"),
        QStringLiteral("CaskaydiaCove Nerd Font"),
        QStringLiteral("CaskaydiaCove Nerd Font Mono"),
        QStringLiteral("CaskaydiaMono Nerd Font"),
        QStringLiteral("CaskaydiaMono Nerd Font Mono"),
        QStringLiteral("FiraCode Nerd Font"),
        QStringLiteral("FiraCode Nerd Font Mono"),
        QStringLiteral("MesloLGS Nerd Font"),
        QStringLiteral("MesloLGS Nerd Font Mono"),
        QStringLiteral("Hack Nerd Font"),
        QStringLiteral("Hack Nerd Font Mono"),
        QStringLiteral("Symbols Nerd Font Mono")
    };
#else
    const QStringList preferredFallbackFamilies = {
        QStringLiteral("JetBrainsMono Nerd Font"),
        QStringLiteral("JetBrainsMono Nerd Font Mono"),
        QStringLiteral("JetBrains Mono Nerd Font"),
        QStringLiteral("JetBrains Mono Nerd Font Mono"),
        QStringLiteral("CaskaydiaCove Nerd Font"),
        QStringLiteral("CaskaydiaCove Nerd Font Mono"),
        QStringLiteral("FiraCode Nerd Font"),
        QStringLiteral("FiraCode Nerd Font Mono"),
        QStringLiteral("MesloLGS Nerd Font"),
        QStringLiteral("MesloLGS Nerd Font Mono"),
        QStringLiteral("Hack Nerd Font"),
        QStringLiteral("Hack Nerd Font Mono"),
        QStringLiteral("Symbols Nerd Font Mono"),
        QStringLiteral("Symbols Nerd Font"),
        QStringLiteral("Noto Sans Symbols2"),
        QStringLiteral("Noto Color Emoji")
    };
#endif

    const QFontDatabase database;
    const QStringList availableFamilies = database.families();
    auto familyAvailable = [&availableFamilies](const QString &family) {
        return std::any_of(availableFamilies.begin(), availableFamilies.end(), [&family](const QString &candidate) {
            return QString::compare(candidate, family, Qt::CaseInsensitive) == 0;
        });
    };

    for (const QString &family : preferredFallbackFamilies) {
#if defined(Q_OS_WIN)
        if (familyAvailable(family) && database.isFixedPitch(family)) {
#else
        if (familyAvailable(family)) {
#endif
            families << family;
        }
    }

    const QString fixedFamily = QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
    if (!fixedFamily.isEmpty()) {
        families << fixedFamily;
    }

    QStringList deduplicatedFamilies;
    for (const QString &family : families) {
        if (family.isEmpty()) {
            continue;
        }
        const bool duplicate = std::any_of(deduplicatedFamilies.begin(), deduplicatedFamilies.end(), [&family](const QString &existing) {
            return QString::compare(existing, family, Qt::CaseInsensitive) == 0;
        });
        if (!duplicate) {
            deduplicatedFamilies << family;
        }
    }
    if (!deduplicatedFamilies.isEmpty()) {
        font.setFamilies(deduplicatedFamilies);
    }

#if defined(Q_OS_WIN)
    font.setKerning(false);
    font.setHintingPreference(QFont::PreferFullHinting);
    font.setStyleHint(QFont::Monospace, QFont::PreferDefault);
    font.setFixedPitch(true);

    if (!QFontInfo(font).fixedPitch()) {
        QFont fixedFont = QFontDatabase::systemFont(QFontDatabase::FixedFont);
        if (font.pointSize() > 0) {
            fixedFont.setPointSize(font.pointSize());
        }
        fixedFont.setKerning(false);
        fixedFont.setHintingPreference(QFont::PreferFullHinting);
        fixedFont.setStyleHint(QFont::Monospace, QFont::PreferDefault);
        fixedFont.setFixedPitch(true);
        return fixedFont;
    }
#else
    font.setKerning(false);
    font.setHintingPreference(QFont::PreferFullHinting);
    font.setStyleHint(QFont::Monospace, QFont::PreferDefault);
#endif

    return font;
}

} // namespace

TerminalWidget::TerminalWidget(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    m_theme.font = withSymbolFallbacks(m_theme.font);
    setFont(m_theme.font);
    rebuildFontCache();
    viewport()->setAutoFillBackground(false);
    viewport()->setAttribute(Qt::WA_OpaquePaintEvent, true);
    viewport()->setAttribute(Qt::WA_NoSystemBackground, true);

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
        const QRect cursorRect = cursorViewportRect();
        if (!hasFocus() || !m_emulator.cursorVisible() || m_scrollOffset != 0) {
            if (!m_cursorBlinkVisible) {
                m_cursorBlinkVisible = true;
                if (cursorRect.isValid()) {
                    viewport()->update(cursorRect);
                }
            }
            return;
        }
        m_cursorBlinkVisible = !m_cursorBlinkVisible;
        if (cursorRect.isValid()) {
            viewport()->update(cursorRect);
        }
    });
    m_cursorBlinkTimer.start();

    recalculateGrid();
}

void TerminalWidget::setTheme(const TerminalTheme &theme)
{
    m_theme = theme;
    m_theme.font = withSymbolFallbacks(m_theme.font);
    setFont(m_theme.font);
    rebuildFontCache();
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

    const int scrollbackSize = m_scrollback.size();
    const int totalLineCount = scrollbackSize + rows;
    const int startLine = std::clamp(totalLineCount - rows - m_scrollOffset, 0, std::max(0, totalLineCount - rows));
    const int firstRow = std::clamp(dirtyRect.top() / std::max(1, m_cellHeight), 0, rows - 1);
    const int lastRow = std::clamp(dirtyRect.bottom() / std::max(1, m_cellHeight), 0, rows - 1);

    bool selectionActive = false;
    int selectionStartRow = 0;
    int selectionEndRow = -1;
    int selectionStartCol = 0;
    int selectionEndCol = -1;
    if (m_selection.isActive() && totalLineCount > 0) {
        const int maxAbsoluteLine = totalLineCount - 1;
        QPoint start = m_selection.start();
        QPoint end = m_selection.end();
        start.setY(std::clamp(start.y(), 0, maxAbsoluteLine));
        end.setY(std::clamp(end.y(), 0, maxAbsoluteLine));
        if (start.y() > end.y() || (start.y() == end.y() && start.x() > end.x())) {
            std::swap(start, end);
        }
        selectionStartRow = start.y();
        selectionEndRow = end.y();
        selectionStartCol = start.x();
        selectionEndCol = end.x();
        selectionActive = selectionStartRow <= selectionEndRow;
    }

    QFont styledFont = m_fontRegular;
    bool fontInitialized = false;
    bool lastBold = false;
    bool lastItalic = false;
    bool lastUnderline = false;
    bool lastStrike = false;
    const QString spaceGlyph = QStringLiteral(" ");
    const TerminalCell emptyCell {};

    for (int row = firstRow; row <= lastRow; ++row) {
        const int absoluteLine = startLine + row;
        const int y = row * m_cellHeight;

        int selectedStartCol = -1;
        int selectedEndCol = -1;
        if (selectionActive && absoluteLine >= selectionStartRow && absoluteLine <= selectionEndRow) {
            if (selectionStartRow == selectionEndRow) {
                selectedStartCol = selectionStartCol;
                selectedEndCol = selectionEndCol;
            } else if (absoluteLine == selectionStartRow) {
                selectedStartCol = selectionStartCol;
                selectedEndCol = cols - 1;
            } else if (absoluteLine == selectionEndRow) {
                selectedStartCol = 0;
                selectedEndCol = selectionEndCol;
            } else {
                selectedStartCol = 0;
                selectedEndCol = cols - 1;
            }
            selectedStartCol = std::clamp(selectedStartCol, 0, cols - 1);
            selectedEndCol = std::clamp(selectedEndCol, 0, cols - 1);
            if (selectedStartCol > selectedEndCol) {
                selectedStartCol = -1;
                selectedEndCol = -1;
            }
        }
        const bool rowHasSelection = selectedStartCol >= 0;

        if (absoluteLine < scrollbackSize) {
            const TerminalScrollback::Line &rowCells = m_scrollback.lineAt(absoluteLine);

            // Pass 1: paint backgrounds.
            for (int col = 0; col < cols; ++col) {
                const TerminalCell &cell = col < static_cast<int>(rowCells.size()) ? rowCells[static_cast<std::size_t>(col)] : emptyCell;
                QColor background = cell.hasBackgroundRgb ? cell.backgroundRgb : m_theme.resolveBackground(cell.background);
                if (cell.inverse) {
                    background = cell.hasForegroundRgb ? cell.foregroundRgb : m_theme.resolveForeground(cell.foreground);
                }
                if (rowHasSelection && col >= selectedStartCol && col <= selectedEndCol) {
                    background = m_theme.selection;
                }

                const int widthCells = (cell.wide && col + 1 < cols) ? 2 : 1;
                const int x = col * m_cellWidth;
                painter.fillRect(x, y, m_cellWidth * widthCells, m_cellHeight, background);
            }

            // Pass 2: draw glyphs.
            for (int col = 0; col < cols;) {
                const TerminalCell &cell = col < static_cast<int>(rowCells.size()) ? rowCells[static_cast<std::size_t>(col)] : emptyCell;
                if (cell.wideContinuation) {
                    ++col;
                    continue;
                }
                if (cell.character.isEmpty()) {
                    ++col;
                    continue;
                }

                QColor foreground = cell.hasForegroundRgb ? cell.foregroundRgb : m_theme.resolveForeground(cell.foreground);
                if (cell.inverse) {
                    foreground = cell.hasBackgroundRgb ? cell.backgroundRgb : m_theme.resolveBackground(cell.background);
                }
                if (cell.dim) {
                    foreground.setAlphaF(0.7);
                }

                if (!fontInitialized || cell.bold != lastBold || cell.italic != lastItalic
                    || cell.underline != lastUnderline || cell.strikethrough != lastStrike) {
                    styledFont = cachedFont(cell.bold, cell.italic);
                    styledFont.setUnderline(cell.underline);
                    styledFont.setStrikeOut(cell.strikethrough);
                    painter.setFont(styledFont);

                    fontInitialized = true;
                    lastBold = cell.bold;
                    lastItalic = cell.italic;
                    lastUnderline = cell.underline;
                    lastStrike = cell.strikethrough;
                }

                if (cell.wide || cell.character.size() > 1) {
                    if (cell.character != spaceGlyph) {
                        painter.setPen(foreground);
                        painter.drawText(col * m_cellWidth, y + m_ascent, cell.character);
                    }
                    col += cell.wide ? 2 : 1;
                    continue;
                }

                if (cell.character == spaceGlyph) {
                    ++col;
                    continue;
                }

                const int runStart = col;
                QString runText = cell.character;
                int runEnd = col + 1;

                while (runEnd < cols) {
                    const TerminalCell &next = runEnd < static_cast<int>(rowCells.size()) ? rowCells[static_cast<std::size_t>(runEnd)] : emptyCell;
                    if (next.wide || next.wideContinuation || next.character.isEmpty() || next.character.size() > 1
                        || !sameTextStyle(cell, next)) {
                        break;
                    }
                    runText.append(next.character);
                    ++runEnd;
                }

                painter.setPen(foreground);
                painter.drawText(runStart * m_cellWidth, y + m_ascent, runText);
                col = runEnd;
            }
            continue;
        }

        const int emulatorRow = absoluteLine - scrollbackSize;
        const TerminalCell *rowCells = m_emulator.rowData(emulatorRow);
        if (!rowCells) {
            continue;
        }

        // Pass 1: paint all cell backgrounds to avoid clipping glyphs by adjacent background fills.
        for (int col = 0; col < cols; ++col) {
            const TerminalCell &cell = rowCells[static_cast<std::size_t>(col)];
            QColor background = cell.hasBackgroundRgb ? cell.backgroundRgb : m_theme.resolveBackground(cell.background);
            if (cell.inverse) {
                background = cell.hasForegroundRgb ? cell.foregroundRgb : m_theme.resolveForeground(cell.foreground);
            }
            if (rowHasSelection && col >= selectedStartCol && col <= selectedEndCol) {
                background = m_theme.selection;
            }

            const int widthCells = (cell.wide && col + 1 < cols) ? 2 : 1;
            const int x = col * m_cellWidth;
            painter.fillRect(x, y, m_cellWidth * widthCells, m_cellHeight, background);
        }

        // Pass 2: draw glyphs.
        for (int col = 0; col < cols;) {
            const TerminalCell &cell = rowCells[static_cast<std::size_t>(col)];
            if (cell.wideContinuation) {
                ++col;
                continue;
            }
            if (cell.character.isEmpty()) {
                ++col;
                continue;
            }

            QColor foreground = cell.hasForegroundRgb ? cell.foregroundRgb : m_theme.resolveForeground(cell.foreground);
            if (cell.inverse) {
                foreground = cell.hasBackgroundRgb ? cell.backgroundRgb : m_theme.resolveBackground(cell.background);
            }
            if (cell.dim) {
                foreground.setAlphaF(0.7);
            }

            if (!fontInitialized || cell.bold != lastBold || cell.italic != lastItalic
                || cell.underline != lastUnderline || cell.strikethrough != lastStrike) {
                styledFont = cachedFont(cell.bold, cell.italic);
                styledFont.setUnderline(cell.underline);
                styledFont.setStrikeOut(cell.strikethrough);
                painter.setFont(styledFont);

                fontInitialized = true;
                lastBold = cell.bold;
                lastItalic = cell.italic;
                lastUnderline = cell.underline;
                lastStrike = cell.strikethrough;
            }

            // Wide and complex cells are rendered individually to preserve cell alignment.
            if (cell.wide || cell.character.size() > 1) {
                if (cell.character != spaceGlyph) {
                    painter.setPen(foreground);
                    painter.drawText(col * m_cellWidth, y + m_ascent, cell.character);
                }
                col += cell.wide ? 2 : 1;
                continue;
            }

            if (cell.character == spaceGlyph) {
                ++col;
                continue;
            }

            const int runStart = col;
            QString runText = cell.character;
            int runEnd = col + 1;

            while (runEnd < cols) {
                const TerminalCell &next = rowCells[static_cast<std::size_t>(runEnd)];
                if (next.wide || next.wideContinuation || next.character.isEmpty() || next.character.size() > 1
                    || !sameTextStyle(cell, next)) {
                    break;
                }
                runText.append(next.character);
                ++runEnd;
            }

            painter.setPen(foreground);
            painter.drawText(runStart * m_cellWidth, y + m_ascent, runText);
            col = runEnd;
        }
    }

    if (hasFocus() && m_scrollOffset == 0 && m_emulator.cursorVisible() && m_cursorBlinkVisible) {
        const QPoint cursor = m_emulator.cursorPosition();
        const QRect cursorRect(cursor.x() * m_cellWidth, cursor.y() * m_cellHeight, m_cellWidth, m_cellHeight);
        const TerminalCell &cursorCell = m_emulator.cellAt(cursor.y(), cursor.x());
        painter.fillRect(cursorRect, m_theme.cursor);
        QFont cursorFont = cachedFont(cursorCell.bold, cursorCell.italic);
        cursorFont.setUnderline(cursorCell.underline);
        cursorFont.setStrikeOut(cursorCell.strikethrough);
        const QString cursorChar = cursorCell.character;
        painter.setPen(m_theme.background);
        painter.setFont(cursorFont);
        painter.drawText(cursorRect.x(), cursorRect.y() + m_ascent, cursorChar.isEmpty() ? QStringLiteral(" ") : cursorChar);
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
    const QRect cursorRect = cursorViewportRect();
    if (cursorRect.isValid()) {
        viewport()->update(cursorRect);
    } else {
        viewport()->update();
    }
}

void TerminalWidget::focusOutEvent(QFocusEvent *event)
{
    QAbstractScrollArea::focusOutEvent(event);
    m_cursorBlinkVisible = true;
    const QRect cursorRect = cursorViewportRect();
    if (cursorRect.isValid()) {
        viewport()->update(cursorRect);
    } else {
        viewport()->update();
    }
}

void TerminalWidget::recalculateGrid()
{
    QFontMetrics metrics(m_theme.font);
    const int advanceM = metrics.horizontalAdvance(QStringLiteral("M"));
    const int advanceW = metrics.horizontalAdvance(QStringLiteral("W"));
    m_cellWidth = std::max({1, metrics.averageCharWidth(), advanceM, advanceW});
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
    viewport()->update();
}

void TerminalWidget::rebuildFontCache()
{
    m_fontRegular = m_theme.font;
    m_fontBold = m_theme.font;
    m_fontBold.setBold(true);
    m_fontItalic = m_theme.font;
    m_fontItalic.setItalic(true);
    m_fontBoldItalic = m_theme.font;
    m_fontBoldItalic.setBold(true);
    m_fontBoldItalic.setItalic(true);
}

const QFont &TerminalWidget::cachedFont(bool bold, bool italic) const
{
    if (bold) {
        return italic ? m_fontBoldItalic : m_fontBold;
    }
    return italic ? m_fontItalic : m_fontRegular;
}

QRect TerminalWidget::cursorViewportRect() const
{
    if (m_cellWidth <= 0 || m_cellHeight <= 0 || m_scrollOffset != 0 || m_emulator.rows() <= 0 || m_emulator.cols() <= 0) {
        return {};
    }
    const QPoint cursor = m_emulator.cursorPosition();
    if (cursor.x() < 0 || cursor.x() >= m_emulator.cols() || cursor.y() < 0 || cursor.y() >= m_emulator.rows()) {
        return {};
    }
    return QRect(cursor.x() * m_cellWidth, cursor.y() * m_cellHeight, m_cellWidth, m_cellHeight);
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
    const int flushDelayMs = m_pendingSessionOutput.size() >= 16 * 1024 ? 4 : 0;
    QTimer::singleShot(flushDelayMs, this, [this]() {
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
    const QPoint oldCursor = m_emulator.cursorPosition();
    const bool oldCursorVisible = m_emulator.cursorVisible();
    QByteArray chunk = std::move(m_pendingSessionOutput);
    m_pendingSessionOutput.clear();

    m_emulator.feedOutput(chunk);
    int dirtyTopRow = 0;
    int dirtyBottomRow = -1;
    bool hasDirtyRows = m_emulator.takeDirtyRowSpan(dirtyTopRow, dirtyBottomRow);
    const int viewportScrollLines = m_emulator.takePendingViewportScrollLines();
    const QPoint newCursor = m_emulator.cursorPosition();
    const bool newCursorVisible = m_emulator.cursorVisible();

    auto includeDirtyRow = [&hasDirtyRows, &dirtyTopRow, &dirtyBottomRow, this](int row) {
        const int clampedRow = std::clamp(row, 0, std::max(0, m_emulator.rows() - 1));
        if (!hasDirtyRows) {
            hasDirtyRows = true;
            dirtyTopRow = clampedRow;
            dirtyBottomRow = clampedRow;
            return;
        }
        dirtyTopRow = std::min(dirtyTopRow, clampedRow);
        dirtyBottomRow = std::max(dirtyBottomRow, clampedRow);
    };

    if (oldCursorVisible) {
        includeDirtyRow(oldCursor.y());
    }
    if (newCursorVisible) {
        includeDirtyRow(newCursor.y());
    }

    bool scrollbackCleared = false;
    if (m_emulator.takeScrollbackClearRequested()) {
        m_scrollback = TerminalScrollback(m_scrollback.maxLines());
        m_selection.clear();
        m_scrollOffset = 0;
        scrollbackCleared = true;
    }

    const QByteArray terminalReply = m_emulator.takePendingResponse();
    if (!terminalReply.isEmpty()) {
        m_session.writeInput(terminalReply);
    }
    
    // Process scrolled lines AFTER checking for scrollback clear to prevent
    // race condition where cleared scrollback gets repopulated with stale lines.
    if (!scrollbackCleared) {
        for (TerminalEmulator::Line line : m_emulator.takeScrolledLines()) {
            m_scrollback.pushLine(std::move(line));
        }
    } else {
        // Discard any scrolled lines if scrollback was just cleared.
        m_emulator.takeScrolledLines();
    }

    verticalScrollBar()->setRange(0, std::max(0, m_scrollback.size()));
    if (stickToBottom) {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    }

    if (scrollbackCleared) {
        // Force full viewport repaint after scrollback clear to ensure renderer state is synchronized.
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        m_scrollOffset = 0;
        viewport()->update();
        return;
    }

    resetCursorBlink();

    bool updatedRegion = false;
    if (hasDirtyRows && m_cellHeight > 0 && m_cellWidth > 0) {
        const int dirtyRowCount = dirtyBottomRow - dirtyTopRow + 1;
        
        // On Windows, viewport scroll optimization can cause rendering artifacts when ConPTY
        // batches output containing clear sequences. Disable optimization for full-screen clears.
        const bool isFullScreenClear = (dirtyRowCount >= m_emulator.rows());
#if defined(Q_OS_WIN)
        const bool allowViewportScrollOptimization = !isFullScreenClear;
#else
        constexpr bool allowViewportScrollOptimization = true;
#endif
        
        if (allowViewportScrollOptimization && stickToBottom && m_scrollOffset == 0 && viewportScrollLines != 0
            && viewportScrollLines > -m_emulator.rows() && viewportScrollLines < m_emulator.rows()
            && dirtyRowCount < m_emulator.rows()) {
            viewport()->scroll(0, viewportScrollLines * m_cellHeight);
        }

        const int startLine = visibleStartLine();
        const int scrollbackSize = m_scrollback.size();
        const int firstVisibleRow = std::max(0, scrollbackSize + dirtyTopRow - startLine);
        const int lastVisibleRow = std::min(m_emulator.rows() - 1, scrollbackSize + dirtyBottomRow - startLine);
        if (firstVisibleRow <= lastVisibleRow) {
            const QRect dirtyRect(0, firstVisibleRow * m_cellHeight, viewport()->width(),
                (lastVisibleRow - firstVisibleRow + 1) * m_cellHeight);
            viewport()->update(dirtyRect);
            updatedRegion = true;
        }
    }
    if (!updatedRegion) {
        viewport()->update();
    }

    if (!m_pendingSessionOutput.isEmpty() && !m_outputFlushQueued) {
        m_outputFlushQueued = true;
        const int flushDelayMs = m_pendingSessionOutput.size() >= 16 * 1024 ? 4 : 0;
        QTimer::singleShot(flushDelayMs, this, [this]() {
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
        return TerminalScrollback::lineText(m_scrollback.lineAt(absoluteLine));
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
        const auto codepoints = line.toUcs4();
        const int startCol = (row == start.y()) ? std::clamp(start.x(), 0, maxCol) : 0;
        const int endCol = (row == end.y()) ? std::clamp(end.x(), 0, maxCol) : maxCol;

        QString segment;
        for (int col = startCol; col <= endCol; ++col) {
            const QString glyph = col < codepoints.size() ? scalarFromCodepoint(codepoints[col]) : QString();
            segment.append(glyph.isEmpty() ? QStringLiteral(" ") : glyph);
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

void TerminalWidget::maybeSendMouseReport(QMouseEvent *event, bool release)
{
    if (!m_emulator.mouseTrackingEnabled()) {
        return;
    }

    const bool isMoveEvent = event->type() == QEvent::MouseMove;
    int code = -1;
    int sgrReleaseCode = -1;
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
        sgrReleaseCode = buttonToCode(event->button());
        if (sgrReleaseCode < 0) {
            sgrReleaseCode = 0;
        }
    } else {
        code = buttonToCode(event->button());
        if (code < 0) {
            return;
        }
        sgrReleaseCode = code;
    }

    const QPoint cell = toViewportCell(event->pos());
    const int x = cell.x() + 1;
    const int y = cell.y() + 1;
    QByteArray report;
    if (m_emulator.mouseSgrMode()) {
        const int reportCode = (release && !isMoveEvent) ? sgrReleaseCode : code;
        report = "\x1b[<" + QByteArray::number(reportCode) + ";" + QByteArray::number(x) + ";" + QByteArray::number(y)
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
