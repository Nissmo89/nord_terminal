#pragma once

#include <QByteArray>
#include <QObject>

#include "nordterminal/TerminalProfile.h"

class QProcess;
class QSocketNotifier;
class QTimer;

namespace nord::terminal {

class TerminalSession : public QObject {
    Q_OBJECT

public:
    explicit TerminalSession(QObject *parent = nullptr);
    ~TerminalSession() override;

    bool start(const TerminalProfile &profile = TerminalProfile::defaultForHost());
    bool isRunning() const;
    void writeInput(const QByteArray &data);
    void resizePty(int rows, int cols);
    void terminate();
    [[nodiscard]] TerminalProfile activeProfile() const;

signals:
    void outputReceived(const QByteArray &data);
    void processExited(int exitCode);
    void sessionError(const QString &message);

private:
#if defined(Q_OS_UNIX)
    void onMasterPtyReadyRead();
    void onChildExitCheck();
    void closeMasterPty();

    int m_masterFd = -1;
    qint64 m_childPid = -1;
    bool m_exitEmitted = false;
    QSocketNotifier *m_readNotifier = nullptr;
    QTimer *m_exitCheckTimer = nullptr;
#else
    QProcess *m_process = nullptr;
#endif
    TerminalProfile m_profile;
};

} // namespace nord::terminal
