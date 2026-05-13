#include "nordterminal/TerminalKeyMapper.h"

#include <QKeyEvent>
#include <Qt>

namespace nord::terminal {

QByteArray TerminalKeyMapper::mapKeyEvent(const QKeyEvent *event, bool applicationCursorKeys)
{
    if (!event) {
        return {};
    }

    if (event->modifiers().testFlag(Qt::ControlModifier)) {
        switch (event->key()) {
        case Qt::Key_C:
            return QByteArray(1, '\x03');
        case Qt::Key_D:
            return QByteArray(1, '\x04');
        case Qt::Key_L:
            return QByteArray(1, '\x0c');
        case Qt::Key_Z:
            return QByteArray(1, '\x1a');
        default:
            break;
        }
    }

    switch (event->key()) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return QByteArray(1, '\r');
    case Qt::Key_Backspace:
        return QByteArray(1, '\x7f');
    case Qt::Key_Tab:
        return QByteArray(1, '\t');
    case Qt::Key_Escape:
        return QByteArray(1, '\x1b');
    case Qt::Key_Up:
        return applicationCursorKeys ? QByteArray("\x1bOA") : QByteArray("\x1b[A");
    case Qt::Key_Down:
        return applicationCursorKeys ? QByteArray("\x1bOB") : QByteArray("\x1b[B");
    case Qt::Key_Right:
        return applicationCursorKeys ? QByteArray("\x1bOC") : QByteArray("\x1b[C");
    case Qt::Key_Left:
        return applicationCursorKeys ? QByteArray("\x1bOD") : QByteArray("\x1b[D");
    case Qt::Key_Home:
        return applicationCursorKeys ? QByteArray("\x1bOH") : QByteArray("\x1b[H");
    case Qt::Key_End:
        return applicationCursorKeys ? QByteArray("\x1bOF") : QByteArray("\x1b[F");
    case Qt::Key_Delete:
        return QByteArray("\x1b[3~");
    case Qt::Key_Insert:
        return QByteArray("\x1b[2~");
    case Qt::Key_PageUp:
        return QByteArray("\x1b[5~");
    case Qt::Key_PageDown:
        return QByteArray("\x1b[6~");
    default:
        break;
    }

    const QString text = event->text();
    if (text.isEmpty()) {
        return {};
    }
    return text.toUtf8();
}

} // namespace nord::terminal
