#include "nordterminal/TerminalSession.h"

#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QProcessEnvironment>
#include <QSocketNotifier>
#include <QTimer>
#include <QDateTime>
#include <QMetaObject>
#include <QPointer>
#include <QStandardPaths>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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

#if defined(Q_OS_WIN)
#if !defined(NOMINMAX)
#define NOMINMAX
#endif
#include <windows.h>

#ifndef HPCON
typedef HANDLE HPCON;
#endif

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE 0x00020016
#endif
#endif

namespace nord::terminal {

#if defined(Q_OS_WIN)
namespace {

using CreatePseudoConsoleFn = HRESULT(WINAPI *)(COORD, HANDLE, HANDLE, DWORD, HPCON *);
using ResizePseudoConsoleFn = HRESULT(WINAPI *)(HPCON, COORD);
using ClosePseudoConsoleFn = void(WINAPI *)(HPCON);

bool isValidHandle(HANDLE handle)
{
    return handle != nullptr && handle != INVALID_HANDLE_VALUE;
}

void closeHandleSafely(HANDLE &handle)
{
    if (isValidHandle(handle)) {
        ::CloseHandle(handle);
    }
    handle = INVALID_HANDLE_VALUE;
}

QString formatWin32Error(DWORD code)
{
    LPWSTR buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = ::FormatMessageW(flags, nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    QString message;
    if (length > 0 && buffer) {
        message = QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed();
    } else {
        message = QStringLiteral("Win32 error %1").arg(code);
    }
    if (buffer) {
        ::LocalFree(buffer);
    }
    return message;
}

QString formatHResultError(HRESULT hr)
{
    const DWORD asWin32 = HRESULT_FACILITY(hr) == FACILITY_WIN32 ? HRESULT_CODE(hr) : static_cast<DWORD>(hr);
    return QStringLiteral("HRESULT 0x%1 (%2)")
        .arg(QString::number(static_cast<quint32>(hr), 16).rightJustified(8, QLatin1Char('0')).toUpper(),
            formatWin32Error(asWin32));
}

QString quoteWindowsCommandArg(const QString &arg)
{
    if (arg.isEmpty()) {
        return QStringLiteral("\"\"");
    }

    bool needsQuotes = false;
    for (const QChar ch : arg) {
        if (ch.isSpace() || ch == QLatin1Char('\t') || ch == QLatin1Char('"')) {
            needsQuotes = true;
            break;
        }
    }
    if (!needsQuotes) {
        return arg;
    }

    QString quoted;
    quoted.reserve(arg.size() + 2);
    quoted.append(QLatin1Char('"'));
    int backslashCount = 0;
    for (const QChar ch : arg) {
        if (ch == QLatin1Char('\\')) {
            ++backslashCount;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            quoted.append(QString(backslashCount * 2 + 1, QLatin1Char('\\')));
            quoted.append(QLatin1Char('"'));
            backslashCount = 0;
            continue;
        }
        if (backslashCount > 0) {
            quoted.append(QString(backslashCount, QLatin1Char('\\')));
            backslashCount = 0;
        }
        quoted.append(ch);
    }
    if (backslashCount > 0) {
        quoted.append(QString(backslashCount * 2, QLatin1Char('\\')));
    }
    quoted.append(QLatin1Char('"'));
    return quoted;
}

std::vector<wchar_t> buildWindowsEnvironmentBlock(const QProcessEnvironment &environment)
{
    QStringList keys = environment.keys();
    std::sort(keys.begin(), keys.end(), [](const QString &lhs, const QString &rhs) {
        return QString::compare(lhs, rhs, Qt::CaseInsensitive) < 0;
    });

    std::vector<wchar_t> block;
    for (const QString &key : keys) {
        const QString entry = key + QLatin1Char('=') + environment.value(key);
        const std::wstring wide = entry.toStdWString();
        block.insert(block.end(), wide.begin(), wide.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

} // namespace

struct TerminalSession::ConPtyState {
    CreatePseudoConsoleFn createPseudoConsole = nullptr;
    ResizePseudoConsoleFn resizePseudoConsole = nullptr;
    ClosePseudoConsoleFn closePseudoConsole = nullptr;

    HPCON pseudoConsole = nullptr;
    HANDLE ptyInputRead = INVALID_HANDLE_VALUE;
    HANDLE ptyInputWrite = INVALID_HANDLE_VALUE;
    HANDLE ptyOutputRead = INVALID_HANDLE_VALUE;
    HANDLE ptyOutputWrite = INVALID_HANDLE_VALUE;

    PROCESS_INFORMATION processInfo {};
    std::thread readerThread;
    std::thread writerThread;
    std::thread waitThread;
    std::mutex writeMutex;
    std::condition_variable writeCv;
    QByteArray writeQueue;
    std::atomic_bool running {false};
    std::atomic_bool stopRequested {false};
    std::atomic_bool exitEmitted {false};
    quint64 generation = 0;

    ~ConPtyState()
    {
        if (closePseudoConsole && pseudoConsole) {
            closePseudoConsole(pseudoConsole);
            pseudoConsole = nullptr;
        }
        closeHandleSafely(ptyInputRead);
        closeHandleSafely(ptyInputWrite);
        closeHandleSafely(ptyOutputRead);
        closeHandleSafely(ptyOutputWrite);
        closeHandleSafely(processInfo.hThread);
        closeHandleSafely(processInfo.hProcess);
    }
};
#endif

TerminalSession::TerminalSession(QObject *parent)
    : QObject(parent)
{
#if defined(Q_OS_UNIX)
    m_exitCheckTimer = new QTimer(this);
    m_exitCheckTimer->setInterval(80);
    connect(m_exitCheckTimer, &QTimer::timeout, this, &TerminalSession::onChildExitCheck);
#else
    m_conPty = nullptr;
#endif
}

TerminalSession::~TerminalSession()
{
#if defined(Q_OS_UNIX)
    terminate();
    if (m_childPid > 0) {
        ::kill(static_cast<pid_t>(m_childPid), SIGKILL);
        int status = 0;
        ::waitpid(static_cast<pid_t>(m_childPid), &status, 0);
        m_childPid = -1;
    }
#else
    terminateWindows(true);
#endif
}

bool TerminalSession::start(const TerminalProfile &profile)
{
    if (profile.shellPath.isEmpty()) {
        emit sessionError(QStringLiteral("Shell path is empty."));
        return false;
    }

#if defined(Q_OS_WIN)
    if (m_conPty) {
        terminateWindows(false);
    }
#endif
#if defined(Q_OS_UNIX)
    if (isRunning()) {
        terminate();
        if (m_childPid > 0) {
            ::kill(static_cast<pid_t>(m_childPid), SIGKILL);
            int status = 0;
            ::waitpid(static_cast<pid_t>(m_childPid), &status, 0);
            finalizeChildExit(status, true);
        }
    }
#endif

#if defined(Q_OS_WIN)
    QString resolvedShellPath = profile.shellPath;
    const QFileInfo requestedShellInfo(profile.shellPath);
    if (!requestedShellInfo.exists() || !requestedShellInfo.isExecutable()) {
        const QString discoveredPath = QStandardPaths::findExecutable(profile.shellPath);
        if (!discoveredPath.isEmpty()) {
            resolvedShellPath = discoveredPath;
        }
    }
    const QFileInfo shellPathInfo(resolvedShellPath);
    if (!shellPathInfo.exists()) {
        emit sessionError(QStringLiteral("Shell not found: %1").arg(profile.shellPath));
        return false;
    }
    if (!shellPathInfo.isExecutable()) {
        emit sessionError(QStringLiteral("Shell is not executable: %1").arg(resolvedShellPath));
        return false;
    }
#else
    const QFileInfo shellPathInfo(profile.shellPath);
    if (!shellPathInfo.exists()) {
        emit sessionError(QStringLiteral("Shell not found: %1").arg(profile.shellPath));
        return false;
    }
    if (!shellPathInfo.isExecutable()) {
        emit sessionError(QStringLiteral("Shell is not executable: %1").arg(profile.shellPath));
        return false;
    }
#endif

    m_profile = profile;
#if defined(Q_OS_WIN)
    m_profile.shellPath = shellPathInfo.absoluteFilePath();
    const quint64 generation = m_generation.fetch_add(1, std::memory_order_relaxed) + 1;
#endif

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
    m_terminationRequested = false;
    m_forceKillSent = false;
    m_terminationStartMs = 0;
    m_writeBuffer.clear();

    const int flags = ::fcntl(m_masterFd, F_GETFL, 0);
    if (flags >= 0) {
        ::fcntl(m_masterFd, F_SETFL, flags | O_NONBLOCK);
    }

    if (m_readNotifier) {
        delete m_readNotifier;
        m_readNotifier = nullptr;
    }
    m_readNotifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Read, this);
    connect(m_readNotifier, &QSocketNotifier::activated, this, [this]() {
        onMasterPtyReadyRead();
    });

    if (m_writeNotifier) {
        delete m_writeNotifier;
        m_writeNotifier = nullptr;
    }
    m_writeNotifier = new QSocketNotifier(m_masterFd, QSocketNotifier::Write, this);
    m_writeNotifier->setEnabled(false);
    connect(m_writeNotifier, &QSocketNotifier::activated, this, [this]() {
        onMasterPtyWritable();
    });

    if (m_exitCheckTimer) {
        m_exitCheckTimer->start();
    }
    return true;
#else
    std::unique_ptr<ConPtyState> conPty = std::make_unique<ConPtyState>();
    conPty->createPseudoConsole = reinterpret_cast<CreatePseudoConsoleFn>(::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "CreatePseudoConsole"));
    conPty->resizePseudoConsole = reinterpret_cast<ResizePseudoConsoleFn>(::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "ResizePseudoConsole"));
    conPty->closePseudoConsole = reinterpret_cast<ClosePseudoConsoleFn>(::GetProcAddress(::GetModuleHandleW(L"kernel32.dll"), "ClosePseudoConsole"));
    if (!conPty->createPseudoConsole || !conPty->resizePseudoConsole || !conPty->closePseudoConsole) {
        emit sessionError(QStringLiteral("Windows ConPTY API is unavailable (requires Windows 10 version 1809 or newer)."));
        return false;
    }

    SECURITY_ATTRIBUTES securityAttributes {};
    securityAttributes.nLength = sizeof(securityAttributes);
    securityAttributes.bInheritHandle = TRUE;

    constexpr DWORD pipeBufferSize = 256 * 1024;
    if (!::CreatePipe(&conPty->ptyInputRead, &conPty->ptyInputWrite, &securityAttributes, pipeBufferSize)) {
        emit sessionError(QStringLiteral("CreatePipe(input) failed: %1").arg(formatWin32Error(::GetLastError())));
        return false;
    }
    if (!::CreatePipe(&conPty->ptyOutputRead, &conPty->ptyOutputWrite, &securityAttributes, pipeBufferSize)) {
        emit sessionError(QStringLiteral("CreatePipe(output) failed: %1").arg(formatWin32Error(::GetLastError())));
        return false;
    }
    ::SetHandleInformation(conPty->ptyInputWrite, HANDLE_FLAG_INHERIT, 0);
    ::SetHandleInformation(conPty->ptyOutputRead, HANDLE_FLAG_INHERIT, 0);

    // Start ConPTY at the widget's requested grid size to avoid an initial
    // 80x24 render/wrap pass before the first resize reaches the backend.
    const COORD initialSize {
        static_cast<SHORT>(std::clamp(m_requestedCols, 1, 32767)),
        static_cast<SHORT>(std::clamp(m_requestedRows, 1, 32767)),
    };
    const HRESULT createConPtyHr =
        conPty->createPseudoConsole(initialSize, conPty->ptyInputRead, conPty->ptyOutputWrite, 0, &conPty->pseudoConsole);
    if (FAILED(createConPtyHr)) {
        emit sessionError(QStringLiteral("CreatePseudoConsole failed: %1").arg(formatHResultError(createConPtyHr)));
        return false;
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
    const std::vector<wchar_t> environmentBlock = buildWindowsEnvironmentBlock(environment);

    SIZE_T attributeListBytes = 0;
    ::InitializeProcThreadAttributeList(nullptr, 1, 0, &attributeListBytes);
    if (attributeListBytes == 0) {
        emit sessionError(QStringLiteral("InitializeProcThreadAttributeList sizing failed: %1")
                              .arg(formatWin32Error(::GetLastError())));
        return false;
    }
    std::vector<char> attributeListStorage(attributeListBytes);
    auto *attributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributeListStorage.data());
    if (!::InitializeProcThreadAttributeList(attributeList, 1, 0, &attributeListBytes)) {
        emit sessionError(QStringLiteral("InitializeProcThreadAttributeList failed: %1").arg(formatWin32Error(::GetLastError())));
        return false;
    }

    STARTUPINFOEXW startupInfoEx {};
    startupInfoEx.StartupInfo.cb = sizeof(startupInfoEx);
    startupInfoEx.lpAttributeList = attributeList;

    const BOOL updated = ::UpdateProcThreadAttribute(startupInfoEx.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
        conPty->pseudoConsole, sizeof(HPCON), nullptr, nullptr);
    if (!updated) {
        ::DeleteProcThreadAttributeList(attributeList);
        emit sessionError(QStringLiteral("UpdateProcThreadAttribute(PSEUDOCONSOLE) failed: %1")
                              .arg(formatWin32Error(::GetLastError())));
        return false;
    }

    QString commandLine;
    for (int i = 0; i < profile.arguments.size(); ++i) {
        if (i > 0) {
            commandLine.append(QLatin1Char(' '));
        }
        commandLine.append(quoteWindowsCommandArg(profile.arguments.at(i)));
    }
    std::vector<wchar_t> commandLineBuffer;
    LPWSTR commandLinePtr = nullptr;
    if (!commandLine.isEmpty()) {
        std::wstring commandLineWide = commandLine.toStdWString();
        commandLineBuffer.assign(commandLineWide.begin(), commandLineWide.end());
        commandLineBuffer.push_back(L'\0');
        commandLinePtr = commandLineBuffer.data();
    }

    const std::wstring executableWide = shellPathInfo.absoluteFilePath().toStdWString();
    std::wstring workingDirectoryWide;
    LPCWSTR workingDirectoryPtr = nullptr;
    if (!profile.workingDirectory.isEmpty()) {
        workingDirectoryWide = profile.workingDirectory.toStdWString();
        workingDirectoryPtr = workingDirectoryWide.c_str();
    }

    const DWORD createFlags = EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT;
    const BOOL processCreated = ::CreateProcessW(executableWide.c_str(), commandLinePtr, nullptr, nullptr, FALSE,
        createFlags, environmentBlock.empty() ? nullptr : const_cast<wchar_t *>(environmentBlock.data()), workingDirectoryPtr,
        &startupInfoEx.StartupInfo, &conPty->processInfo);
    ::DeleteProcThreadAttributeList(attributeList);
    if (!processCreated) {
        emit sessionError(QStringLiteral("CreateProcessW failed for '%1': %2")
                              .arg(shellPathInfo.absoluteFilePath(), formatWin32Error(::GetLastError())));
        return false;
    }

    closeHandleSafely(conPty->ptyInputRead);
    closeHandleSafely(conPty->ptyOutputWrite);
    closeHandleSafely(conPty->processInfo.hThread);

    ConPtyState *state = conPty.get();
    state->running.store(true);
    state->generation = generation;
    QPointer<TerminalSession> owner(this);

    state->readerThread = std::thread([owner, state]() {
        std::vector<char> buffer(8192);
        constexpr int maxChunksPerBatch = 32;
        constexpr qsizetype maxBytesPerBatch = 256 * 1024;
        QByteArray batchedOutput;
        batchedOutput.reserve(static_cast<qsizetype>(buffer.size()) * 4);
        const quint64 capturedGeneration = state->generation;

        auto dispatchBatchedOutput = [owner, capturedGeneration, &batchedOutput]() {
            if (batchedOutput.isEmpty()) {
                return;
            }
            QByteArray chunk = std::move(batchedOutput);
            batchedOutput.clear();
            if (!owner) {
                return;
            }
            QMetaObject::invokeMethod(owner.data(), [owner, chunk = std::move(chunk), capturedGeneration]() {
                if (!owner || owner->m_generation.load(std::memory_order_relaxed) != capturedGeneration) {
                    return;
                }
                emit owner->outputReceived(chunk);
            }, Qt::QueuedConnection);
        };

        while (!state->stopRequested.load()) {
            DWORD bytesRead = 0;
            const BOOL ok = ::ReadFile(state->ptyOutputRead, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr);
            if (ok && bytesRead > 0) {
                batchedOutput.append(buffer.data(), static_cast<qsizetype>(bytesRead));

                int chunksRead = 1;
                while (!state->stopRequested.load() && chunksRead < maxChunksPerBatch && batchedOutput.size() < maxBytesPerBatch) {
                    DWORD availableBytes = 0;
                    if (!::PeekNamedPipe(state->ptyOutputRead, nullptr, 0, nullptr, &availableBytes, nullptr) || availableBytes == 0) {
                        break;
                    }

                    bytesRead = 0;
                    const DWORD chunkBytes = std::min<DWORD>(availableBytes, static_cast<DWORD>(buffer.size()));
                    const BOOL chunkOk = ::ReadFile(state->ptyOutputRead, buffer.data(), chunkBytes, &bytesRead, nullptr);
                    if (!chunkOk || bytesRead == 0) {
                        break;
                    }
                    batchedOutput.append(buffer.data(), static_cast<qsizetype>(bytesRead));
                    ++chunksRead;
                }

                dispatchBatchedOutput();
                continue;
            }

            const DWORD error = ::GetLastError();
            if (state->stopRequested.load() || error == ERROR_BROKEN_PIPE || error == ERROR_HANDLE_EOF || error == ERROR_NO_DATA
                || error == ERROR_OPERATION_ABORTED) {
                dispatchBatchedOutput();
                break;
            }
            dispatchBatchedOutput();
            if (owner) {
                QMetaObject::invokeMethod(owner.data(), [owner, error, capturedGeneration]() {
                    if (!owner || owner->m_generation.load(std::memory_order_relaxed) != capturedGeneration) {
                        return;
                    }
                    emit owner->sessionError(QStringLiteral("ConPTY read failed: %1").arg(formatWin32Error(error)));
                }, Qt::QueuedConnection);
            }
            break;
        }

        dispatchBatchedOutput();
    });

    state->writerThread = std::thread([owner, state]() {
        const quint64 capturedGeneration = state->generation;
        for (;;) {
            QByteArray toWrite;
            {
                std::unique_lock<std::mutex> lock(state->writeMutex);
                state->writeCv.wait(lock, [state]() {
                    return state->stopRequested.load() || !state->writeQueue.isEmpty();
                });
                if (state->stopRequested.load() && state->writeQueue.isEmpty()) {
                    break;
                }
                toWrite.swap(state->writeQueue);
            }

            qsizetype offset = 0;
            while (offset < toWrite.size()) {
                const qsizetype remaining = toWrite.size() - offset;
                const DWORD chunkSize =
                    static_cast<DWORD>(std::min<qsizetype>(remaining, static_cast<qsizetype>(std::numeric_limits<DWORD>::max())));
                DWORD bytesWritten = 0;
                const BOOL ok = ::WriteFile(state->ptyInputWrite, toWrite.constData() + offset, chunkSize, &bytesWritten, nullptr);
                if (ok && bytesWritten > 0) {
                    offset += static_cast<qsizetype>(bytesWritten);
                    continue;
                }

                const DWORD error = ::GetLastError();
                if (state->stopRequested.load() || error == ERROR_BROKEN_PIPE || error == ERROR_NO_DATA
                    || error == ERROR_OPERATION_ABORTED) {
                    break;
                }
                if (owner) {
                    QMetaObject::invokeMethod(owner.data(), [owner, error, capturedGeneration]() {
                        if (!owner || owner->m_generation.load(std::memory_order_relaxed) != capturedGeneration) {
                            return;
                        }
                        emit owner->sessionError(QStringLiteral("ConPTY write failed: %1").arg(formatWin32Error(error)));
                    }, Qt::QueuedConnection);
                }
                state->stopRequested.store(true);
                break;
            }
        }
    });

    state->waitThread = std::thread([owner, state]() {
        const quint64 capturedGeneration = state->generation;
        const DWORD waitResult = ::WaitForSingleObject(state->processInfo.hProcess, INFINITE);
        int exitCode = 0;
        if (waitResult == WAIT_OBJECT_0) {
            DWORD rawExitCode = 0;
            if (::GetExitCodeProcess(state->processInfo.hProcess, &rawExitCode)) {
                exitCode = static_cast<int>(rawExitCode);
            }
        }
        state->running.store(false);
        state->stopRequested.store(true);
        state->writeCv.notify_all();

        if (!state->exitEmitted.exchange(true)) {
            if (owner) {
                QMetaObject::invokeMethod(owner.data(), [owner, exitCode, capturedGeneration]() {
                    if (!owner || owner->m_generation.load(std::memory_order_relaxed) != capturedGeneration) {
                        return;
                    }
                    emit owner->processExited(exitCode);
                }, Qt::QueuedConnection);
            }
        }
    });

    m_conPty = conPty.release();
    return true;
#endif
}

bool TerminalSession::isRunning() const
{
#if defined(Q_OS_UNIX)
    return m_childPid > 0;
#else
    return m_conPty && m_conPty->running.load();
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

    if (!m_writeBuffer.isEmpty()) {
        m_writeBuffer.append(data);
        flushPendingWriteBuffer();
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
            m_writeBuffer.append(data.constData() + offset, data.size() - offset);
            if (m_writeNotifier) {
                m_writeNotifier->setEnabled(true);
            }
            return;
        }
        if (written < 0) {
            emit sessionError(QStringLiteral("PTY write failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        }
        return;
    }
#else
    if (!m_conPty || !m_conPty->running.load()) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_conPty->writeMutex);
        m_conPty->writeQueue.append(data);
    }
    m_conPty->writeCv.notify_one();
#endif
}

void TerminalSession::resizePty(int rows, int cols)
{
    m_requestedRows = std::max(1, rows);
    m_requestedCols = std::max(1, cols);

#if defined(Q_OS_UNIX)
    if (m_masterFd < 0) {
        return;
    }
    struct winsize ws {};
    ws.ws_row = static_cast<unsigned short>(m_requestedRows);
    ws.ws_col = static_cast<unsigned short>(m_requestedCols);
    ws.ws_xpixel = 0;
    ws.ws_ypixel = 0;
    ::ioctl(m_masterFd, TIOCSWINSZ, &ws);
#else
    if (!m_conPty || !m_conPty->running.load() || !m_conPty->resizePseudoConsole || !m_conPty->pseudoConsole) {
        return;
    }
    const COORD newSize {
        static_cast<SHORT>(std::clamp(m_requestedCols, 1, 32767)),
        static_cast<SHORT>(std::clamp(m_requestedRows, 1, 32767)),
    };
    const HRESULT hr = m_conPty->resizePseudoConsole(m_conPty->pseudoConsole, newSize);
    if (FAILED(hr)) {
        emit sessionError(QStringLiteral("ResizePseudoConsole failed: %1").arg(formatHResultError(hr)));
    }
#endif
}

void TerminalSession::terminate()
{
#if defined(Q_OS_UNIX)
    const qint64 pid = m_childPid;
    if (pid <= 0) {
        closeMasterPty();
        return;
    }
    closeMasterPty();
    if (!m_terminationRequested) {
        m_terminationRequested = true;
        m_forceKillSent = false;
        m_terminationStartMs = QDateTime::currentMSecsSinceEpoch();
        ::kill(static_cast<pid_t>(pid), SIGHUP);
    }
    if (m_exitCheckTimer && !m_exitCheckTimer->isActive()) {
        m_exitCheckTimer->start();
    }
#else
    terminateWindows(false);
#endif
}

#if defined(Q_OS_WIN)
void TerminalSession::terminateWindows(bool blockUntilStopped)
{
    if (!m_conPty) {
        return;
    }

    ConPtyState *state = m_conPty;
    m_conPty = nullptr;
    const quint64 capturedGeneration = state->generation;

    auto teardownState = [state]() {
        state->stopRequested.store(true);
        state->writeCv.notify_all();

        if (state->closePseudoConsole && state->pseudoConsole) {
            state->closePseudoConsole(state->pseudoConsole);
            state->pseudoConsole = nullptr;
        }

        closeHandleSafely(state->ptyInputWrite);
        closeHandleSafely(state->ptyOutputRead);

        if (isValidHandle(state->processInfo.hProcess)) {
            const DWORD waitResult = ::WaitForSingleObject(state->processInfo.hProcess, 1000);
            if (waitResult == WAIT_TIMEOUT) {
                ::TerminateProcess(state->processInfo.hProcess, 1);
                ::WaitForSingleObject(state->processInfo.hProcess, 1000);
            }
        }

        if (state->writerThread.joinable()) {
            state->writerThread.join();
        }
        if (state->readerThread.joinable()) {
            state->readerThread.join();
        }
        if (state->waitThread.joinable()) {
            state->waitThread.join();
        }
        state->running.store(false);
    };

    if (blockUntilStopped) {
        teardownState();
        delete state;
        return;
    }

    QPointer<TerminalSession> owner(this);
    std::thread([owner, state, capturedGeneration, teardownState]() mutable {
        teardownState();
        if (!state->exitEmitted.exchange(true) && owner) {
            QMetaObject::invokeMethod(owner.data(), [owner, capturedGeneration]() {
                if (!owner || owner->m_generation.load(std::memory_order_relaxed) != capturedGeneration) {
                    return;
                }
                emit owner->processExited(0);
            }, Qt::QueuedConnection);
        }
        delete state;
    }).detach();
}
#endif

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
    QByteArray batchedOutput;
    batchedOutput.reserve(static_cast<qsizetype>(sizeof(buffer)) * 4);
    int chunksRead = 0;
    for (;;) {
        const ssize_t n = ::read(m_masterFd, buffer, sizeof(buffer));
        if (n > 0) {
            batchedOutput.append(buffer, static_cast<qsizetype>(n));
            ++chunksRead;
            if (chunksRead >= maxChunksPerActivation) {
                // Yield back to the Qt event loop to prevent UI starvation under heavy TUI redraws.
                if (!batchedOutput.isEmpty()) {
                    emit outputReceived(batchedOutput);
                }
                return;
            }
            continue;
        }
        if (n == 0) {
            if (!batchedOutput.isEmpty()) {
                emit outputReceived(batchedOutput);
            }
            closeMasterPty();
            return;
        }
        if (errno == EINTR) {
            continue;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (!batchedOutput.isEmpty()) {
                emit outputReceived(batchedOutput);
            }
            return;
        }
        if (!batchedOutput.isEmpty()) {
            emit outputReceived(batchedOutput);
        }
        emit sessionError(QStringLiteral("PTY read failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return;
    }
}

void TerminalSession::onMasterPtyWritable()
{
    flushPendingWriteBuffer();
}

void TerminalSession::flushPendingWriteBuffer()
{
    if (m_masterFd < 0 || m_writeBuffer.isEmpty()) {
        if (m_writeNotifier) {
            m_writeNotifier->setEnabled(false);
        }
        return;
    }

    while (!m_writeBuffer.isEmpty()) {
        const ssize_t written = ::write(m_masterFd, m_writeBuffer.constData(), static_cast<size_t>(m_writeBuffer.size()));
        if (written > 0) {
            m_writeBuffer.remove(0, static_cast<qsizetype>(written));
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (m_writeNotifier) {
                m_writeNotifier->setEnabled(true);
            }
            return;
        }
        if (written < 0) {
            emit sessionError(QStringLiteral("PTY write failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        }
        m_writeBuffer.clear();
        if (m_writeNotifier) {
            m_writeNotifier->setEnabled(false);
        }
        return;
    }

    if (m_writeNotifier) {
        m_writeNotifier->setEnabled(false);
    }
}

void TerminalSession::finalizeChildExit(int status, bool hasExitStatus)
{
    m_childPid = -1;
    m_terminationRequested = false;
    m_forceKillSent = false;
    m_terminationStartMs = 0;
    closeMasterPty();
    if (m_exitCheckTimer) {
        m_exitCheckTimer->stop();
    }
    if (!m_exitEmitted) {
        m_exitEmitted = true;
        const int exitCode = hasExitStatus && WIFEXITED(status) ? WEXITSTATUS(status) : 0;
        emit processExited(exitCode);
    }
}

void TerminalSession::onChildExitCheck()
{
    if (m_childPid <= 0) {
        if (m_exitCheckTimer) {
            m_exitCheckTimer->stop();
        }
        return;
    }

    int status = 0;
    const pid_t result = ::waitpid(static_cast<pid_t>(m_childPid), &status, WNOHANG);
    if (result == 0) {
        if (m_terminationRequested && !m_forceKillSent) {
            const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - m_terminationStartMs;
            if (elapsedMs >= 500) {
                ::kill(static_cast<pid_t>(m_childPid), SIGKILL);
                m_forceKillSent = true;
            }
        }
        return;
    }

    if (result < 0) {
        if (errno == ECHILD) {
            finalizeChildExit(0, false);
            return;
        }
        emit sessionError(QStringLiteral("waitpid failed: %1").arg(QString::fromLocal8Bit(std::strerror(errno))));
        return;
    }

    finalizeChildExit(status, true);
}

void TerminalSession::closeMasterPty()
{
    if (m_readNotifier) {
        m_readNotifier->setEnabled(false);
        delete m_readNotifier;
        m_readNotifier = nullptr;
    }
    if (m_writeNotifier) {
        m_writeNotifier->setEnabled(false);
        delete m_writeNotifier;
        m_writeNotifier = nullptr;
    }
    m_writeBuffer.clear();
    if (m_masterFd >= 0) {
        ::close(m_masterFd);
        m_masterFd = -1;
    }
}
#endif

} // namespace nord::terminal
