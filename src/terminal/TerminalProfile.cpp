#include "nordterminal/TerminalProfile.h"

#include <QDir>
#include <QFileInfo>

namespace nord::terminal {

TerminalProfile TerminalProfile::defaultForHost()
{
    TerminalProfile profile;
#if defined(Q_OS_WIN)
    profile.name = QStringLiteral("PowerShell");
    profile.shellPath = QStringLiteral("powershell.exe");
    profile.arguments = {QStringLiteral("-NoLogo"), QStringLiteral("-NoExit")};
#elif defined(Q_OS_MACOS)
    profile.name = QStringLiteral("zsh");
    profile.shellPath = QStringLiteral("/bin/zsh");
    profile.arguments = {QStringLiteral("-i")};
#else
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
