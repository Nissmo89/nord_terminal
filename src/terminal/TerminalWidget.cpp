#include "nordterminal/TerminalWidget.h"

#include "nordterminal/TerminalKeyMapper.h"
#include "nordterminal/TerminalThemeLoader.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
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
#include <QStandardPaths>
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

bool isTraceEnabledFromEnvironment()
{
    const QString value = qEnvironmentVariable("NORD_TERMINAL_TRACE").trimmed().toLower();
    return value == QStringLiteral("1") || value == QStringLiteral("true") || value == QStringLiteral("on")
        || value == QStringLiteral("yes");
}

bool isVerboseTraceRequestedFromEnvironment()
{
    const QString value = qEnvironmentVariable("NORD_TERMINAL_TRACE_MODE").trimmed().toLower();
    return value == QStringLiteral("verbose") || value == QStringLiteral("full") || value == QStringLiteral("2");
}

int outputFlushDelayMs(qsizetype pendingBytes)
{
    constexpr qsizetype largeChunkThreshold = 16 * 1024;
    return pendingBytes >= largeChunkThreshold ? 4 : 2;
}

QString summaryPathForTraceFile(const QString &traceFilePath)
{
    const QFileInfo traceInfo(traceFilePath);
    const QString suffix = traceInfo.suffix().isEmpty() ? QStringLiteral("log") : traceInfo.suffix();
    QString baseName = traceInfo.completeBaseName();
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("nord_terminal_trace");
    }
    if (!baseName.endsWith(QStringLiteral("_summary"))) {
        baseName += QStringLiteral("_summary");
    }
    return traceInfo.dir().filePath(QStringLiteral("%1.%2").arg(baseName, suffix));
}

bool shouldMirrorTraceLineToSummary(const QString &message)
{
    if (message.startsWith(QStringLiteral("trace enabled path='")) || message.startsWith(QStringLiteral("trace summary path='"))
        || message.startsWith(QStringLiteral("startShell requested")) || message.startsWith(QStringLiteral("startShell started="))
        || message.startsWith(QStringLiteral("stopShell requested")) || message.startsWith(QStringLiteral("flushPendingSessionOutput "))
        || message.startsWith(QStringLiteral("damage hasDirty=")) || message.startsWith(QStringLiteral("scrollback cleared by emulator"))) {
        return true;
    }

    const QString lower = message.toLower();
    return lower.contains(QStringLiteral("error")) || lower.contains(QStringLiteral("warn")) || lower.contains(QStringLiteral("fatal"))
        || lower.contains(QStringLiteral("failed")) || lower.contains(QStringLiteral("exception"))
        || lower.contains(QStringLiteral("assert"));
}

QString escapedBytePreview(const QByteArray &data, qsizetype maxBytes)
{
    const qsizetype bytesToRender = std::min(maxBytes, data.size());
    QString text;
    text.reserve(bytesToRender * 4);
    for (qsizetype i = 0; i < bytesToRender; ++i) {
        const unsigned char ch = static_cast<unsigned char>(data.at(i));
        if (ch == '\r') {
            text += QStringLiteral("\\r");
        } else if (ch == '\n') {
            text += QStringLiteral("\\n");
        } else if (ch == '\t') {
            text += QStringLiteral("\\t");
        } else if (ch == '\\') {
            text += QStringLiteral("\\\\");
        } else if (ch >= 0x20 && ch <= 0x7e) {
            text += QLatin1Char(static_cast<char>(ch));
        } else {
            text += QStringLiteral("\\x%1").arg(QString::number(ch, 16).rightJustified(2, QLatin1Char('0')).toUpper());
        }
    }
    if (data.size() > bytesToRender) {
        text += QStringLiteral("...<truncated>");
    }
    return text;
}

} // namespace

TerminalWidget::TerminalWidget(QWidget *parent)
    : QAbstractScrollArea(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    // Keep viewport geometry stable; scrollbar show/hide width changes can desync
    // terminal columns vs PTY and lead to cursor placement drift.
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
    verticalScrollBar()->setSingleStep(1);

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
    connect(verticalScrollBar(), &QScrollBar::rangeChanged, this, [this](int, int) {
        m_scrollOffset = verticalScrollBar()->maximum() - verticalScrollBar()->value();
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

    initializeTraceLogging();
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
    traceLog(QStringLiteral("startShell requested: shell='%1' args='%2' cwd='%3'")
                 .arg(profile.shellPath, profile.arguments.join(QLatin1Char(' ')), profile.workingDirectory));
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

    m_session.resizePty(m_emulator.rows(), m_emulator.cols());
    const bool started = m_session.start(profile);
    if (started) {
        m_session.resizePty(m_emulator.rows(), m_emulator.cols());
        traceLog(QStringLiteral("startShell started=true rows=%1 cols=%2").arg(m_emulator.rows()).arg(m_emulator.cols()));
        traceViewportSnapshot(QStringLiteral("startShell-post-start"));
    } else {
        traceLog(QStringLiteral("startShell started=false"));
    }
    viewport()->update();
    return started;
}

void TerminalWidget::stopShell()
{
    traceLog(QStringLiteral("stopShell requested"));
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

            // Pass 2: draw glyphs anchored to cell boundaries.
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
                const int widthCells = cell.wide ? 2 : 1;
                const QRect cellRect(col * m_cellWidth, y, m_cellWidth * widthCells, m_cellHeight);
                painter.setPen(foreground);
                painter.save();
                painter.setClipRect(cellRect);
                painter.drawText(cellRect.x(), y + m_ascent, cell.character);
                painter.restore();
                col += widthCells;
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

        // Pass 2: draw glyphs anchored to cell boundaries.
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
            const int widthCells = cell.wide ? 2 : 1;
            const QRect cellRect(col * m_cellWidth, y, m_cellWidth * widthCells, m_cellHeight);
            painter.setPen(foreground);
            painter.save();
            painter.setClipRect(cellRect);
            painter.drawText(cellRect.x(), y + m_ascent, cell.character);
            painter.restore();
            col += widthCells;
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
    auto followOutputOnInput = [this]() {
        if (m_scrollOffset == 0) {
            return;
        }
        m_scrollOffset = 0;
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        viewport()->update();
    };

    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const bool hasCtrl = modifiers.testFlag(Qt::ControlModifier);
    const bool hasShift = modifiers.testFlag(Qt::ShiftModifier);
    const bool hasAlt = modifiers.testFlag(Qt::AltModifier);
    const bool hasMeta = modifiers.testFlag(Qt::MetaModifier);

    if (hasCtrl && hasShift && !hasAlt && !hasMeta && event->key() == Qt::Key_C) {
        if (m_selection.isActive()) {
            QApplication::clipboard()->setText(selectedText());
        } else {
            followOutputOnInput();
            traceBytes(QStringLiteral("tx-key-ctrl-c"), QByteArray(1, '\x03'));
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
        followOutputOnInput();
        traceBytes(QStringLiteral("tx-paste"), pasteData);
        m_session.writeInput(pasteData);
        event->accept();
        return;
    }

    if (hasCtrl && !hasShift && !hasAlt && !hasMeta && event->key() == Qt::Key_C) {
        followOutputOnInput();
        traceBytes(QStringLiteral("tx-key-ctrl-c"), QByteArray(1, '\x03'));
        m_session.writeInput(QByteArray(1, '\x03'));
        event->accept();
        return;
    }

    const QByteArray mapped = TerminalKeyMapper::mapKeyEvent(event, m_emulator.applicationCursorKeys());
    if (!mapped.isEmpty()) {
        followOutputOnInput();
        traceLog(QStringLiteral("keyPress key=%1 text='%2' modifiers=0x%3")
                     .arg(event->key())
                     .arg(event->text())
                     .arg(static_cast<int>(event->modifiers()), 0, 16));
        traceBytes(QStringLiteral("tx-key"), mapped);
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
    traceLog(QStringLiteral("recalculateGrid viewport=%1x%2 cell=%3x%4 => rows=%5 cols=%6")
                 .arg(viewport()->width())
                 .arg(viewport()->height())
                 .arg(m_cellWidth)
                 .arg(m_cellHeight)
                 .arg(rows)
                 .arg(cols));

    const int previousRows = m_emulator.rows();
    const int previousCols = m_emulator.cols();
    const bool sizeChanged = rows != previousRows || cols != previousCols;
    if (sizeChanged && !m_pendingSessionOutput.isEmpty()) {
        // Apply pending bytes with the old geometry before resizing to avoid
        // interleaving stale-size output into the new grid.
        flushPendingSessionOutput();
    }

    if (sizeChanged) {
        m_emulator.resize(rows, cols);
        m_session.resizePty(rows, cols);
    }

    verticalScrollBar()->setPageStep(rows);
    const int maxOffset = std::max(0, m_scrollback.size());
    m_scrollOffset = std::clamp(m_scrollOffset, 0, maxOffset);
    verticalScrollBar()->setRange(0, maxOffset);
    if (m_scrollOffset == 0) {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    } else {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum() - m_scrollOffset);
    }

    if (sizeChanged) {
        traceLog(QStringLiteral("grid resize applied old=%1x%2 new=%3x%4")
                     .arg(previousRows)
                     .arg(previousCols)
                     .arg(rows)
                     .arg(cols));
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

    traceBytes(QStringLiteral("rx"), data);
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
    const int flushDelayMs = outputFlushDelayMs(m_pendingSessionOutput.size());
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

    auto schedulePendingFlush = [this]() {
        if (m_pendingSessionOutput.isEmpty() || m_outputFlushQueued) {
            return;
        }
        m_outputFlushQueued = true;
        const int flushDelayMs = outputFlushDelayMs(m_pendingSessionOutput.size());
        QTimer::singleShot(flushDelayMs, this, [this]() {
            m_outputFlushQueued = false;
            flushPendingSessionOutput();
        });
    };

    const bool stickToBottom = (verticalScrollBar()->value() == verticalScrollBar()->maximum());
    const QPoint oldCursor = m_emulator.cursorPosition();
    const bool oldCursorVisible = m_emulator.cursorVisible();
    QByteArray chunk = std::move(m_pendingSessionOutput);
    m_pendingSessionOutput.clear();
    traceLog(QStringLiteral("flushPendingSessionOutput chunkBytes=%1").arg(chunk.size()));

    m_emulator.feedOutput(chunk);
    int dirtyTopRow = 0;
    int dirtyBottomRow = -1;
    bool hasDirtyRows = m_emulator.takeDirtyRowSpan(dirtyTopRow, dirtyBottomRow);
    const int viewportScrollLines = m_emulator.takePendingViewportScrollLines();
    const QPoint newCursor = m_emulator.cursorPosition();
    const bool newCursorVisible = m_emulator.cursorVisible();
    traceLog(QStringLiteral("cursor old=(%1,%2 vis=%3) new=(%4,%5 vis=%6)")
                 .arg(oldCursor.x())
                 .arg(oldCursor.y())
                 .arg(oldCursorVisible ? 1 : 0)
                 .arg(newCursor.x())
                 .arg(newCursor.y())
                 .arg(newCursorVisible ? 1 : 0));

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
    std::vector<TerminalEmulator::Line> scrolledLines = m_emulator.takeScrolledLines();
    if (!scrollbackCleared) {
        for (TerminalEmulator::Line &line : scrolledLines) {
            m_scrollback.pushLine(std::move(line));
        }
    }

    verticalScrollBar()->setRange(0, std::max(0, m_scrollback.size()));
    if (stickToBottom) {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    }

    if (scrollbackCleared) {
        m_wheelRemainder = 0;
        m_scrollOffset = 0;
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        traceLog(QStringLiteral("scrollback cleared by emulator"));
        resetCursorBlink();
        viewport()->update();
        traceViewportSnapshot(QStringLiteral("post-flush"));
        schedulePendingFlush();
        return;
    }

    resetCursorBlink();

    bool updatedRegion = false;
    if (hasDirtyRows && m_cellHeight > 0 && m_cellWidth > 0) {
        const int dirtyRowCount = dirtyBottomRow - dirtyTopRow + 1;
        // Prefer correctness over incremental pixel-scroll optimization.
        // Complex terminal updates (clear/alt-buffer/region scroll) can leave
        // stale painted chunks when only partial rows are invalidated.
        constexpr bool allowViewportScrollOptimization = false;
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

    traceLog(QStringLiteral("damage hasDirty=%1 dirtyTop=%2 dirtyBottom=%3 viewportScrollLines=%4 stickToBottom=%5 scrollOffset=%6")
                 .arg(hasDirtyRows ? 1 : 0)
                 .arg(dirtyTopRow)
                 .arg(dirtyBottomRow)
                 .arg(viewportScrollLines)
                 .arg(stickToBottom ? 1 : 0)
                 .arg(m_scrollOffset));
    traceViewportSnapshot(QStringLiteral("post-flush"));
    schedulePendingFlush();
}

void TerminalWidget::initializeTraceLogging()
{
    const QString requestedPath = qEnvironmentVariable("NORD_TERMINAL_TRACE_FILE").trimmed();
    if (!isTraceEnabledFromEnvironment() && requestedPath.isEmpty()) {
        return;
    }

    m_traceEnabled = true;
    m_traceVerbose = isVerboseTraceRequestedFromEnvironment();
    m_traceSnapshotMaxRows = qEnvironmentVariableIntValue("NORD_TERMINAL_TRACE_MAX_ROWS");
    if (m_traceSnapshotMaxRows <= 0) {
        m_traceSnapshotMaxRows = 60;
    }

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz"));
    QStringList candidatePaths;
    if (!requestedPath.isEmpty()) {
        candidatePaths << requestedPath;
    }
    const QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    candidatePaths << QDir(tempDir).filePath(
        QStringLiteral("nord_terminal_trace_%1_%2.log").arg(QCoreApplication::applicationPid()).arg(stamp));
    candidatePaths << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("nord_terminal_trace.log"));
    candidatePaths << QDir::current().filePath(QStringLiteral("nord_terminal_trace.log"));

    QString openedPath;
    for (const QString &candidate : candidatePaths) {
        if (candidate.isEmpty()) {
            continue;
        }
        const QFileInfo fileInfo(candidate);
        QDir().mkpath(fileInfo.absolutePath());
        m_traceFile.setFileName(candidate);
        if (m_traceFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            openedPath = candidate;
            break;
        }
    }

    if (openedPath.isEmpty()) {
        m_traceEnabled = false;
        return;
    }

    const QString summaryPath = summaryPathForTraceFile(openedPath);
    if (summaryPath != openedPath) {
        m_traceSummaryFile.setFileName(summaryPath);
        if (!m_traceSummaryFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            m_traceSummaryFile.setFileName(QString());
        }
    }

    traceLog(QStringLiteral("trace enabled path='%1'").arg(openedPath));
    if (m_traceSummaryFile.isOpen()) {
        traceLog(QStringLiteral("trace summary path='%1'").arg(summaryPath));
    }
    traceLog(QStringLiteral("trace mode=%1").arg(m_traceVerbose ? QStringLiteral("verbose") : QStringLiteral("basic")));
}

void TerminalWidget::traceLog(const QString &message)
{
    if (!m_traceEnabled || !m_traceFile.isOpen()) {
        return;
    }

    const QString line = QStringLiteral("%1 #%2 %3\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
                             .arg(++m_traceEventId)
                             .arg(message);
    m_traceFile.write(line.toUtf8());
    m_traceFile.flush();

    if (m_traceSummaryFile.isOpen() && shouldMirrorTraceLineToSummary(message)) {
        m_traceSummaryFile.write(line.toUtf8());
        m_traceSummaryFile.flush();
    }
}

void TerminalWidget::traceBytes(const QString &label, const QByteArray &data)
{
    if (!m_traceEnabled) {
        return;
    }

    if (!isVerboseTraceEnabled()) {
        traceLog(QStringLiteral("%1 bytes=%2").arg(label).arg(data.size()));
        return;
    }

    constexpr qsizetype previewLimit = 1024;
    traceLog(QStringLiteral("%1 bytes=%2 text=\"%3\" hex=%4")
                 .arg(label)
                 .arg(data.size())
                 .arg(escapedBytePreview(data, previewLimit))
                 .arg(QString::fromLatin1(data.left(previewLimit).toHex(' '))));
}

void TerminalWidget::traceViewportSnapshot(const QString &label)
{
    if (!m_traceEnabled || !isVerboseTraceEnabled()) {
        return;
    }

    const int rows = m_emulator.rows();
    const int cols = m_emulator.cols();
    if (rows <= 0 || cols <= 0) {
        traceLog(QStringLiteral("%1 snapshot skipped: invalid grid rows=%2 cols=%3").arg(label).arg(rows).arg(cols));
        return;
    }

    const int maxRows = std::min(rows, std::max(1, m_traceSnapshotMaxRows));
    const int startLine = visibleStartLine();
    const int scrollbackSize = m_scrollback.size();
    const QPoint cursor = m_emulator.cursorPosition();

    traceLog(QStringLiteral("%1 snapshot begin rows=%2 cols=%3 visibleStart=%4 scrollback=%5 cursor=(%6,%7) scrollOffset=%8")
                 .arg(label)
                 .arg(rows)
                 .arg(cols)
                 .arg(startLine)
                 .arg(scrollbackSize)
                 .arg(cursor.x())
                 .arg(cursor.y())
                 .arg(m_scrollOffset));

    for (int viewRow = 0; viewRow < maxRows; ++viewRow) {
        const int absoluteLine = startLine + viewRow;
        QString text;
        text.reserve(cols + 4);

        if (absoluteLine < scrollbackSize) {
            const TerminalScrollback::Line &line = m_scrollback.lineAt(absoluteLine);
            for (int col = 0; col < cols; ++col) {
                const TerminalCell *cell = col < static_cast<int>(line.size()) ? &line[static_cast<std::size_t>(col)] : nullptr;
                if (!cell || cell->wideContinuation || cell->character.isEmpty()) {
                    text.append(QLatin1Char(' '));
                } else {
                    text.append(cell->character);
                }
            }
        } else {
            const int emulatorRow = absoluteLine - scrollbackSize;
            const TerminalCell *row = m_emulator.rowData(emulatorRow);
            for (int col = 0; col < cols; ++col) {
                const TerminalCell *cell = row ? &row[static_cast<std::size_t>(col)] : nullptr;
                if (!cell || cell->wideContinuation || cell->character.isEmpty()) {
                    text.append(QLatin1Char(' '));
                } else {
                    text.append(cell->character);
                }
            }
        }

        traceLog(QStringLiteral("%1 row=%2 abs=%3 |%4|").arg(label).arg(viewRow).arg(absoluteLine).arg(text));
    }

    traceLog(QStringLiteral("%1 snapshot end").arg(label));
}

bool TerminalWidget::isVerboseTraceEnabled() const
{
    return m_traceVerbose;
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
