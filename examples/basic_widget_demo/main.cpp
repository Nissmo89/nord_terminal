#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QMainWindow>

#include "nordterminal/TerminalWidget.h"

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QMainWindow window;
    auto *terminal = new nord::terminal::TerminalWidget;

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList themeCandidates = {
        appDir + QStringLiteral("/themes/nord-dark.json"),
        appDir + QStringLiteral("/../themes/nord-dark.json"),
        appDir + QStringLiteral("/../../themes/nord-dark.json")
    };
    for (const QString &path : themeCandidates) {
        if (QFileInfo::exists(path)) {
            terminal->loadThemeFromFile(QDir::cleanPath(path));
            break;
        }
    }
    terminal->startShell();

    window.setCentralWidget(terminal);
    window.resize(1000, 640);
    window.setWindowTitle(QStringLiteral("NordTerminal Demo"));
    window.show();

    return app.exec();
}
