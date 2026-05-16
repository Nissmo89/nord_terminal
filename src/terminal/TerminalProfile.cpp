#include "nordterminal/TerminalProfile.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QtGlobal>

namespace nord::terminal {

TerminalProfile TerminalProfile::defaultForHost()
{
    TerminalProfile profile;
#if defined(Q_OS_WIN)
    auto isUsableExecutable = [](const QString &path) {
        if (path.isEmpty()) {
            return false;
        }
        const QFileInfo info(path);
        return info.exists() && info.isExecutable();
    };

    const QString comSpec = qEnvironmentVariable("COMSPEC");
    QString commandPromptPath;
    if (isUsableExecutable(comSpec)) {
        commandPromptPath = comSpec;
    } else {
        commandPromptPath = QStandardPaths::findExecutable(QStringLiteral("cmd.exe"));
    }

    if (isUsableExecutable(commandPromptPath)) {
        profile.name = QStringLiteral("Command Prompt");
        profile.shellPath = commandPromptPath;
        // Disable Command Processor AutoRun scripts for predictable startup behavior.
        profile.arguments = {QStringLiteral("/D")};
    } else {
        QString powershellPath = QStandardPaths::findExecutable(QStringLiteral("pwsh.exe"));
        QString shellName = QStringLiteral("PowerShell");
        if (powershellPath.isEmpty()) {
            powershellPath = QStandardPaths::findExecutable(QStringLiteral("powershell.exe"));
        } else {
            shellName = QStringLiteral("PowerShell 7");
        }

        if (powershellPath.isEmpty()) {
            powershellPath = QStringLiteral("powershell.exe");
        }

        profile.name = shellName;
        profile.shellPath = powershellPath;
        profile.arguments = {QStringLiteral("-NoLogo"), QStringLiteral("-NoExit")};
    }
#elif defined(Q_OS_MACOS)
    profile.name = QStringLiteral("zsh");
    profile.shellPath = QStringLiteral("/bin/zsh");
    profile.arguments = {QStringLiteral("-i")};
#else
    const QString shellEnv = qEnvironmentVariable("SHELL");
    if (!shellEnv.isEmpty() && QFileInfo(shellEnv).isExecutable()) {
        const QFileInfo shellInfo(shellEnv);
        profile.name = shellInfo.fileName();
        profile.shellPath = shellEnv;
        profile.arguments = {QStringLiteral("-i")};
        profile.workingDirectory = QDir::homePath();
        return profile;
    }

    const QString zshPath = QStringLiteral("/bin/zsh");
    if (QFileInfo(zshPath).exists() && QFileInfo(zshPath).isExecutable()) {
        profile.name = QStringLiteral("zsh");
        profile.shellPath = zshPath;
    } else {
        profile.name = QStringLiteral("bash");
        profile.shellPath = QStringLiteral("/bin/bash");
    }
    profile.arguments = {QStringLiteral("-i")};
#endif
    profile.workingDirectory = QDir::homePath();
    return profile;
}

} // namespace nord::terminal
