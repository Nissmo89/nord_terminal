#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QKeySequence>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QSet>
#include <QStatusBar>
#include <QToolBar>
#include <QVector>

#include <algorithm>

#include "nordterminal/TerminalThemeLoader.h"
#include "nordterminal/TerminalWidget.h"

namespace {

struct ThemeEntry {
    QString id;
    QString name;
    QString path;
};

QStringList themeDirectoryCandidates(const QString &appDir)
{
    return {
        QDir::cleanPath(appDir + QStringLiteral("/themes")),
        QDir::cleanPath(appDir + QStringLiteral("/../themes")),
        QDir::cleanPath(appDir + QStringLiteral("/../../themes"))
    };
}

QVector<ThemeEntry> discoverThemes(const QStringList &directories)
{
    QVector<ThemeEntry> themes;
    QSet<QString> seenPaths;

    for (const QString &directoryPath : directories) {
        const QDir directory(directoryPath);
        if (!directory.exists()) {
            continue;
        }

        const QFileInfoList files =
            directory.entryInfoList({QStringLiteral("*.json")}, QDir::Files | QDir::Readable, QDir::Name | QDir::IgnoreCase);
        for (const QFileInfo &fileInfo : files) {
            const QString canonicalPath = fileInfo.canonicalFilePath();
            const QString resolvedPath =
                QDir::cleanPath(canonicalPath.isEmpty() ? fileInfo.absoluteFilePath() : canonicalPath);
            if (seenPaths.contains(resolvedPath)) {
                continue;
            }
            seenPaths.insert(resolvedPath);

            const nord::terminal::ThemeLoadResult result = nord::terminal::TerminalThemeLoader::loadFromFile(resolvedPath);
            if (!result.ok) {
                continue;
            }

            ThemeEntry entry;
            entry.id = result.theme.id;
            entry.name = result.theme.name.isEmpty() ? fileInfo.completeBaseName() : result.theme.name;
            entry.path = resolvedPath;
            themes.push_back(entry);
        }
    }

    std::sort(themes.begin(), themes.end(), [](const ThemeEntry &lhs, const ThemeEntry &rhs) {
        return QString::localeAwareCompare(lhs.name, rhs.name) < 0;
    });
    return themes;
}

int preferredThemeIndex(const QVector<ThemeEntry> &themes)
{
    for (int index = 0; index < themes.size(); ++index) {
        if (themes[index].id == QStringLiteral("nord-dark")) {
            return index;
        }
    }
    return themes.isEmpty() ? -1 : 0;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QMainWindow window;
    auto *terminal = new nord::terminal::TerminalWidget;
    window.setCentralWidget(terminal);
    window.statusBar()->showMessage(QStringLiteral("Ready"));

    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList themeDirectories = themeDirectoryCandidates(appDir);
    QVector<ThemeEntry> themes = discoverThemes(themeDirectories);

    auto *toolbar = new QToolBar(QStringLiteral("Appearance"), &window);
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    window.addToolBar(Qt::TopToolBarArea, toolbar);

    toolbar->addWidget(new QLabel(QStringLiteral("Theme:"), toolbar));
    auto *themeCombo = new QComboBox(toolbar);
    themeCombo->setMinimumContentsLength(18);
    themeCombo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    toolbar->addWidget(themeCombo);

    auto repopulateThemes = [&themes, &themeDirectories, themeCombo]() {
        const QString previousPath = themeCombo->currentData().toString();
        themes = discoverThemes(themeDirectories);

        themeCombo->clear();
        for (const ThemeEntry &theme : themes) {
            themeCombo->addItem(theme.name, theme.path);
        }

        int desiredIndex = -1;
        if (!previousPath.isEmpty()) {
            for (int i = 0; i < themeCombo->count(); ++i) {
                if (themeCombo->itemData(i).toString() == previousPath) {
                    desiredIndex = i;
                    break;
                }
            }
        }
        if (desiredIndex < 0) {
            desiredIndex = preferredThemeIndex(themes);
        }
        if (desiredIndex >= 0) {
            themeCombo->setCurrentIndex(desiredIndex);
        }
    };

    auto applyThemeIndex = [terminal, &window, &themes](int index) {
        if (index < 0 || index >= themes.size()) {
            return;
        }
        const ThemeEntry &entry = themes[index];
        if (!terminal->loadThemeFromFile(entry.path)) {
            window.statusBar()->showMessage(QStringLiteral("Failed to load theme: %1").arg(entry.name), 5000);
            return;
        }
        window.setWindowTitle(QStringLiteral("NordTerminal Demo - %1").arg(entry.name));
        window.statusBar()->showMessage(QStringLiteral("Theme: %1").arg(entry.name), 2000);
    };

    QObject::connect(themeCombo, qOverload<int>(&QComboBox::currentIndexChanged), &window, applyThemeIndex);

    auto *reloadThemesAction = toolbar->addAction(QStringLiteral("Reload"));
    QObject::connect(reloadThemesAction, &QAction::triggered, &window, [repopulateThemes, &window, themeCombo]() {
        repopulateThemes();
        if (themeCombo->count() == 0) {
            window.statusBar()->showMessage(QStringLiteral("No themes found in themes/ directories."), 5000);
        } else {
            window.statusBar()->showMessage(QStringLiteral("Theme list reloaded"), 1500);
        }
    });

    auto *previousThemeAction = toolbar->addAction(QStringLiteral("Prev"));
    previousThemeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+[")));
    QObject::connect(previousThemeAction, &QAction::triggered, &window, [themeCombo]() {
        if (themeCombo->count() <= 0) {
            return;
        }
        const int next = (themeCombo->currentIndex() - 1 + themeCombo->count()) % themeCombo->count();
        themeCombo->setCurrentIndex(next);
    });

    auto *nextThemeAction = toolbar->addAction(QStringLiteral("Next"));
    nextThemeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+]")));
    QObject::connect(nextThemeAction, &QAction::triggered, &window, [themeCombo]() {
        if (themeCombo->count() <= 0) {
            return;
        }
        const int next = (themeCombo->currentIndex() + 1) % themeCombo->count();
        themeCombo->setCurrentIndex(next);
    });

    repopulateThemes();
    if (themeCombo->count() == 0) {
        QMessageBox::warning(&window,
            QStringLiteral("No Themes Found"),
            QStringLiteral("No valid theme JSON files were found in:\n%1").arg(themeDirectories.join('\n')));
    } else {
        applyThemeIndex(themeCombo->currentIndex());
    }

    terminal->startShell();

    window.resize(1000, 640);
    if (themeCombo->count() == 0) {
        window.setWindowTitle(QStringLiteral("NordTerminal Demo"));
    }
    window.show();

    return app.exec();
}
