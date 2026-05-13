#include "nordterminal/TerminalSession.h"

#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSocketNotifier>
#include <QTimer>

#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <vector>

#if defined(Q_OS_UNIX)
#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#if defined(Q_OS_MACOS) || defined(Q_OS_FREEBSD) || defined(Q_OS_OPENBSD) || defined(Q_OS_NETBSD)
#include <util.h>
#else
#include <pty.h>
#endif
#endif

namespace nord::terminal {

TerminalSession::TerminalSession(QObject *parent)
    : QObject(parent)
{
#if defined(Q_OS_UNIX)
    m_exitCheckTimer = new QTimer(this);
    m_exitCheckTimer->setInterval(80);
    connect(m_exitCheckTimer, &QTimer::timeout, this, &TerminalSession::onChildExitCheck);
#else
    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        emit outputReceived(m_process->readAllStandardOutput());
    });
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        emit outputReceived(m_process->readAllStandardError());
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        emit sessionError(QStringLiteral("QProcess error: %1").arg(static_cast<int>(error)));
    });
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int exitCode, QProcess::ExitStatus) { emit processExited(exitCode); });
#endif
}

TerminalSession::~TerminalSession()
{
    terminate();
}

bool TerminalSession::start(const TerminalProfile &profile)
{
    if (profile.shellPath.isEmpty()) {
        emit sessionError(QStringLiteral("Shell path is empty."));
        return false;
    }
    const QFileInfo shellPathInfo(profile.shellPath);
    if (!shellPathInfo.exists()) {
        emit sessionError(QStringLiteral("Shell not found: %1").arg(profile.shellPath));
        return false;
    }
    if (!shellPathInfo.isExecutable()) {
        emit sessionError(QStringLiteral("Shell is not executable: %1").arg(profile.shellPath));
        return false;
    }

    if (isRunning()) {
        terminate();
    }

    m_profile = profile;

#if defined(Q_OS_UNIX)
    QStringList args = profile.arguments;
    const QFileInfo shellInfo(profile.shellPath);
    const QString shellName = shellInfo.fileName().toLower();
    if (args.isEmpty() && (shellName == QStringLiteral("bash") || shellName == QStringLiteral("zsh"))) {
        args << QStringLiteral("-i");
    }

    const QByteArray program = QFile::encodeName(profile.shellPath);
    QList<QByteArray> argStorage;
    argStorage.reserve(args.size() + 1);
    argStorage.append(program);
    for (const QString &arg : args) {
        argStorage.append(QFile::encodeName(arg));
    }

    std::vector<char *> argv;
    argv.reserve(static_cast<std::size_t>(argStorage.size() + 1));
    for (QByteArray &arg : argStorage) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    int masterFd = -1;
    const pid_t pid = ::forkpty(&masterFd, nullptr, nullptr, nullptr);
    if (pid < 0) {
        emit sessionError(QStringLiteral("forkpty failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return false;
    }

    if (pid == 0) {
        if (!profile.workingDirectory.isEmpty()) {
            const QByteArray workingDir = QFile::encodeName(profile.workingDirectory);
            ::chdir(workingDir.constData());
        }

        for (auto it = profile.environment.constBegin(); it != profile.environment.constEnd(); ++it) {
            const QByteArray key = it.key().toLocal8Bit();
            const QByteArray value = it.value().toLocal8Bit();
            ::setenv(key.constData(), value.constData(), 1);
        }
        if (::getenv("TERM") == nullptr) {
            ::setenv("TERM", "xterm-256color", 1);
        }
        if (::getenv("COLORTERM") == nullptr) {
            ::setenv("COLORTERM", "truecolor", 1);
        }

        ::execvp(program.constData(), argv.data());
        const char *msg = "nordterminal: failed to exec shell\r\n";
        ::write(STDERR_FILENO, msg, std::strlen(msg));
        ::_exit(127);
    }

    m_masterFd = masterFd;
    m_childPid = static_cast<qint64>(pid);
    m_exitEmitted = false;

    const int flags = ::fcntl(m_masterFd, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(m_masterFd, F_SETFL, flags | O_NONBLOCK);
    }

    if (m_readNotifier) {
        delete m_readNotifier;
        m_readNotifier = nullptr;
    }
    m_readNotifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this, [this](auto, auto, auto) {
        onMasterPtyReadyRead();
    });

    if (m_exitCheckTimer) {
        m_exitCheckTimer->start();
    }
    return true;
#else
    m_process->setProgram(profile.shellPath);
    m_process->setArguments(profile.arguments);

    if (!profile.workingDirectory.isEmpty()) {
        m_process->setWorkingDirectory(profile.workingDirectory);
    }

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    for (auto it = profile.environment.constBegin(); it != profile.environment.constEnd(); ++it) {
        environment.insert(it.key(), it.value());
    }
    if (!environment.contains(QStringLiteral("TERM"))) {
        environment.insert(QStringLiteral("TERM"), QStringLiteral("xterm-256color"));
    }
    if (!environment.contains(QStringLiteral("COLORTERM"))) {
        environment.insert(QStringLiteral("COLORTERM"), QStringLiteral("truecolor"));
    }
    m_process->setProcessEnvironment(environment);

    m_process->start();
    const bool started = m_process->waitForStarted(3000);
    if (!started) {
        emit sessionError(QStringLiteral("Failed to start shell '%1': %2").arg(profile.shellPath, m_process->errorString()));
    }
    return started;
#endif
}

bool TerminalSession::isRunning() const
{
#if defined(Q_OS_UNIX)
    return m_childPid > 0;
#else
    return m_process && m_process->state() == QProcess::Running;
#endif
}

void TerminalSession::writeInput(const QByteArray &data)
{
    if (data.isEmpty() || !isRunning()) {
        return;
    }

#if defined(Q_OS_UNIX)
    if (m_masterFd < 0) {
        return;
    }

    qsizetype offset = 0;
    while (offset < data.size()) {
        const ssize_t written = ::write(m_masterFd, data.constData() + offset, static_cast<size_t>(data.size() - offset));
        if (written > 0) {
            offset += written;
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        if (written < 0) {
            emit sessionError(QStringLiteral("PTY write failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        }
        break;
    }
#else
    m_process->write(data);
#endif
}

void TerminalSession::resizePty(int rows, int cols)
{
#if defined(Q_OS_UNIX)
    if (m_masterFd < 0) {
        return;
    }
    struct winsize ws {};
    ws.ws_row = static_cast<unsigned short>(std::max(1, rows));
    ws.ws_col = static_cast<unsigned short>(std::max(1, cols));
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    ::ioctl(m_masterFd, TIOCSWINSZ, &ws);
#else
    Q_UNUSED(rows);
    Q_UNUSED(cols);
#endif
}

void TerminalSession::terminate()
{
#if defined(Q_OS_UNIX)
    if (m_exitCheckTimer) {
        m_exitCheckTimer->stop();
    }

    const qint64 pid = m_childPid;
    if (pid <= 0) {
        closeMasterPty();
        return;
    }

    closeMasterPty();
    ::kill(static_cast<pid_t>(pid), SIGHUP);

    int status = 0;
    for (int i = 0; i < 25; ++i) {
        const pid_t result = ::waitpid(static_cast<pid_t>(pid), &status, WNOHANG);
        if (result == static_cast<pid_t>(pid)) {
            m_childPid = -1;
            const int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 0;
            if (!m_exitEmitted) {
                m_exitEmitted = true;
                emit processExited(exitCode);
            }
            return;
        }
        if (result < 0 && errno == ECHILD) {
            m_childPid = -1;
            if (!m_exitEmitted) {
                m_exitEmitted = true;
                emit processExited(0);
            }
            return;
        }
        ::usleep(20000);
    }

    ::kill(static_cast<pid_t>(pid), SIGKILL);
    ::waitpid(static_cast<pid_t>(pid), &status, 0);
    m_childPid = -1;
    if (!m_exitEmitted) {
        m_exitEmitted = true;
        emit processExited(WIFEXITED(status) ? WEXITSTATUS(status) : 0);
    }
#else
    if (!m_process || m_process->state() == QProcess::NotRunning) {
        return;
    }
    m_process->terminate();
    if (!m_process->waitForFinished(1000)) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
#endif
}

TerminalProfile TerminalSession::activeProfile() const
{
    return m_profile;
}

#if defined(Q_OS_UNIX)
void TerminalSession::onMasterPtyReadyRead()
{
    if (m_masterFd < 0) {
        return;
    }

    char buffer[8192];
    constexpr int maxChunksPerActivation = 64;
    int chunksRead = 0;
    for (;;) {
        const ssize_t n = ::read(m_masterFd, buffer, sizeof(buffer));
        if (n > 0) {
            emit outputReceived(QByteArray(buffer, static_cast<int>(n)));
            ++chunksRead;
            if (chunksRead >= maxChunksPerActivation) {
                // Yield back to the Qt event loop to prevent UI starvation under heavy TUI redraws.
                return;
            }
            continue;
        }
        if (n == 0) {
            closeMasterPty();
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return;
        }
        emit sessionError(QStringLiteral("PTY read failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return;
    }
}

void TerminalSession::onChildExitCheck()
{
    if (m_childPid <= 0) {
        return;
    }

    int status = 0;
    const pid_t result = ::waitpid(static_cast<pid_t>(m_childPid), &status, WNOHANG);
    if (result == 0) {
        return;
    }

    if (result < 0) {
        if (errno == ECHILD) {
            m_childPid = -1;
            closeMasterPty();
            if (!m_exitEmitted) {
                m_exitEmitted = true;
                emit processExited(0);
            }
            if (m_exitCheckTimer) {
                m_exitCheckTimer->stop();
            }
            return;
        }
        emit sessionError(QStringLiteral("waitpid failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return;
    }

    const int exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : 0;
    m_childPid = -1;
    closeMasterPty();
    if (m_exitCheckTimer) {
        m_exitCheckTimer->stop();
    }
    if (!m_exitEmitted) {
        m_exitEmitted = true;
        emit processExited(exitCode);
    }
}

void TerminalSession::closeMasterPty()
{
    if (m_readNotifier) {
        m_readNotifier->setEnabled(false);
        delete m_readNotifier;
        m_readNotifier = nullptr;
    }
    if (m_masterFd >= 0) {
        ::close(m_masterFd);
        m_masterFd = -1;
    }
}
#endif

} // namespace nord::terminal
