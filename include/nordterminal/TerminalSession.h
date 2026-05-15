#pragma once

#include <atomic>

#include <QByteArray>
#include <QObject>

#include "nordterminal/TerminalProfile.h"

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
    void onMasterPtyWritable();
    void onChildExitCheck();
    void flushPendingWriteBuffer();
    void finalizeChildExit(int status, bool hasExitStatus);
    void closeMasterPty();

    int m_masterFd = -1;
    qint64 m_childPid = -1;
    bool m_exitEmitted = false;
    QSocketNotifier *m_readNotifier = nullptr;
    QSocketNotifier *m_writeNotifier = nullptr;
    QTimer *m_exitCheckTimer = nullptr;
    QByteArray m_writeBuffer;
    bool m_terminationRequested = false;
    bool m_forceKillSent = false;
    qint64 m_terminationStartMs = 0;
#else
    void terminateWindows(bool blockUntilStopped);
    struct ConPtyState;
    ConPtyState *m_conPty = nullptr;
#endif
    std::atomic_uint64_t m_generation {0};
    TerminalProfile m_profile;
};

} // namespace nord::terminal
