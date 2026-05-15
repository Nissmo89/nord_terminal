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

// [ ... All code above is unchanged ... ]

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
    for (TerminalEmulator::Line line : m_emulator.takeScrolledLines()) {
        m_scrollback.pushLine(std::move(line));
    }

    verticalScrollBar()->setRange(0, std::max(0, m_scrollback.size()));
    if (stickToBottom) {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
    }

    if (scrollbackCleared) {
        hasDirtyRows = true;
        dirtyTopRow = 0;
        dirtyBottomRow = std::max(0, m_emulator.rows() - 1);
        // --- WINDOWS FIX: Force viewport refresh after clear (ED2/etc) ---
        #if defined(Q_OS_WIN)
        viewport()->update();
        #endif
    }

    resetCursorBlink();

    bool updatedRegion = false;
    if (hasDirtyRows && m_cellHeight > 0 && m_cellWidth > 0) {
        const int dirtyRowCount = dirtyBottomRow - dirtyTopRow + 1;
        constexpr bool allowViewportScrollOptimization = true;
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

// [ ... All code below is unchanged ... ]

} // namespace nord::terminal

