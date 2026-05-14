#include "nordterminal/TerminalKeyMapper.h"

#include <QKeyEvent>
#include <Qt>

namespace nord::terminal {

QByteArray TerminalKeyMapper::mapKeyEvent(const QKeyEvent *event, bool applicationCursorKeys)
{
    if (!event) {
        return {};
    }

    QByteArray output;
    const Qt::KeyboardModifiers modifiers = event->modifiers();

    if (modifiers.testFlag(Qt::ControlModifier) && !modifiers.testFlag(Qt::AltModifier)) {
        if (event->key() >= Qt::Key_A && event->key() <= Qt::Key_Z) {
            output = QByteArray(1, static_cast<char>(event->key() - Qt::Key_A + 1));
        } else {
            switch (event->key()) {
            case Qt::Key_Backslash:
                output = QByteArray(1, '\x1c');
                break;
            case Qt::Key_BracketRight:
                output = QByteArray(1, '\x1d');
                break;
            case Qt::Key_AsciiCircum:
            case Qt::Key_6:
                output = QByteArray(1, '\x1e');
                break;
            default:
                break;
            }
        }
    }

    if (output.isEmpty()) {
        switch (event->key()) {
        case Qt::Key_Return:
        case Qt::Key_Enter:
            output = QByteArray(1, '\r');
            break;
        case Qt::Key_Backspace:
            output = QByteArray(1, '\x7f');
            break;
        case Qt::Key_Tab:
            output = QByteArray(1, '\t');
            break;
        case Qt::Key_Escape:
            output = QByteArray(1, '\x1b');
            break;
        case Qt::Key_Up:
            output = applicationCursorKeys ? QByteArray("\x1bOA") : QByteArray("\x1b[A");
            break;
        case Qt::Key_Down:
            output = applicationCursorKeys ? QByteArray("\x1bOB") : QByteArray("\x1b[B");
            break;
        case Qt::Key_Right:
            output = applicationCursorKeys ? QByteArray("\x1bOC") : QByteArray("\x1b[C");
            break;
        case Qt::Key_Left:
            output = applicationCursorKeys ? QByteArray("\x1bOD") : QByteArray("\x1b[D");
            break;
        case Qt::Key_Home:
            output = applicationCursorKeys ? QByteArray("\x1bOH") : QByteArray("\x1b[H");
            break;
        case Qt::Key_End:
            output = applicationCursorKeys ? QByteArray("\x1bOF") : QByteArray("\x1b[F");
            break;
        case Qt::Key_Delete:
            output = QByteArray("\x1b[3~");
            break;
        case Qt::Key_Insert:
            output = QByteArray("\x1b[2~");
            break;
        case Qt::Key_PageUp:
            output = QByteArray("\x1b[5~");
            break;
        case Qt::Key_PageDown:
            output = QByteArray("\x1b[6~");
            break;
        case Qt::Key_F1:
            output = QByteArray("\x1bOP");
            break;
        case Qt::Key_F2:
            output = QByteArray("\x1bOQ");
            break;
        case Qt::Key_F3:
            output = QByteArray("\x1bOR");
            break;
        case Qt::Key_F4:
            output = QByteArray("\x1bOS");
            break;
        case Qt::Key_F5:
            output = QByteArray("\x1b[15~");
            break;
        case Qt::Key_F6:
            output = QByteArray("\x1b[17~");
            break;
        case Qt::Key_F7:
            output = QByteArray("\x1b[18~");
            break;
        case Qt::Key_F8:
            output = QByteArray("\x1b[19~");
            break;
        case Qt::Key_F9:
            output = QByteArray("\x1b[20~");
            break;
        case Qt::Key_F10:
            output = QByteArray("\x1b[21~");
            break;
        case Qt::Key_F11:
            output = QByteArray("\x1b[23~");
            break;
        case Qt::Key_F12:
            output = QByteArray("\x1b[24~");
            break;
        default:
            break;
        }
    }

    if (output.isEmpty()) {
        const QString text = event->text();
        if (text.isEmpty()) {
            return {};
        }
        output = text.toUtf8();
    }

    if (modifiers.testFlag(Qt::AltModifier) && !modifiers.testFlag(Qt::ControlModifier)) {
        output.prepend('\x1b');
    }

    return output;
}

} // namespace nord::terminal
