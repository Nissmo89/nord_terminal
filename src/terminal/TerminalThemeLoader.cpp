#include "nordterminal/TerminalThemeLoader.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>

namespace nord::terminal {
namespace {

bool readColor(const QJsonObject &obj, const QString &name, QColor &out, QString &error)
{
    if (!obj.contains(name) || !obj.value(name).isString()) {
        error = QStringLiteral("Missing color field '%1'.").arg(name);
        return false;
    }

    const QString value = obj.value(name).toString();
    const QColor color(value);
    if (!color.isValid()) {
        error = QStringLiteral("Invalid color '%1' for '%2'.").arg(value, name);
        return false;
    }
    out = color;
    return true;
}

} // namespace

ThemeLoadResult TerminalThemeLoader::loadFromFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {false, {}, QStringLiteral("Unable to open theme file: %1").arg(path)};
    }
    return loadFromJson(file.readAll());
}

ThemeLoadResult TerminalThemeLoader::loadFromJson(const QByteArray &jsonData)
{
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(jsonData, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return {false, {}, QStringLiteral("Theme JSON parse error: %1").arg(parseError.errorString())};
    }

    const QJsonObject root = document.object();
    if (!root.contains(QStringLiteral("colors")) || !root.value(QStringLiteral("colors")).isObject()) {
        return {false, {}, QStringLiteral("Theme JSON must contain a 'colors' object.")};
    }

    TerminalTheme theme = TerminalTheme::nordDark();
    theme.id = root.value(QStringLiteral("id")).toString(theme.id);
    theme.name = root.value(QStringLiteral("name")).toString(theme.name);

    const QJsonObject colors = root.value(QStringLiteral("colors")).toObject();
    QString error;
    if (!readColor(colors, QStringLiteral("background"), theme.background, error)
        || !readColor(colors, QStringLiteral("foreground"), theme.foreground, error)
        || !readColor(colors, QStringLiteral("cursor"), theme.cursor, error)
        || !readColor(colors, QStringLiteral("selection"), theme.selection, error)) {
        return {false, {}, error};
    }

    if (!colors.contains(QStringLiteral("ansi")) || !colors.value(QStringLiteral("ansi")).isArray()) {
        return {false, {}, QStringLiteral("Theme JSON must include 'colors.ansi' with 16 colors.")};
    }

    const QJsonArray ansi = colors.value(QStringLiteral("ansi")).toArray();
    if (ansi.size() != 16) {
        return {false, {}, QStringLiteral("Theme JSON 'colors.ansi' must contain exactly 16 colors.")};
    }

    for (int i = 0; i < ansi.size(); ++i) {
        if (!ansi[i].isString()) {
            return {false, {}, QStringLiteral("Theme ANSI color at index %1 must be a string.").arg(i)};
        }
        const QColor color(ansi[i].toString());
        if (!color.isValid()) {
            return {false, {}, QStringLiteral("Theme ANSI color at index %1 is invalid.").arg(i)};
        }
        theme.ansi[static_cast<std::size_t>(i)] = color;
    }

    if (root.contains(QStringLiteral("font")) && root.value(QStringLiteral("font")).isObject()) {
        const QJsonObject font = root.value(QStringLiteral("font")).toObject();
        const QString family = font.value(QStringLiteral("family")).toString(theme.font.family());
        const int size = font.value(QStringLiteral("size")).toInt(theme.font.pointSize());
        theme.font.setFamily(family);
        theme.font.setPointSize(size);
    }

    return {true, theme, {}};
}

QJsonObject TerminalThemeLoader::toJson(const TerminalTheme &theme)
{
    QJsonArray ansi;
    for (const QColor &color : theme.ansi) {
        ansi.append(color.name(QColor::HexRgb));
    }

    QJsonObject root;
    root.insert(QStringLiteral("id"), theme.id);
    root.insert(QStringLiteral("name"), theme.name);
    root.insert(QStringLiteral("font"),
        QJsonObject{
            {QStringLiteral("family"), theme.font.family()},
            {QStringLiteral("size"), theme.font.pointSize()}
        });
    root.insert(QStringLiteral("colors"),
        QJsonObject{
            {QStringLiteral("background"), theme.background.name(QColor::HexRgb)},
            {QStringLiteral("foreground"), theme.foreground.name(QColor::HexRgb)},
            {QStringLiteral("cursor"), theme.cursor.name(QColor::HexRgb)},
            {QStringLiteral("selection"), theme.selection.name(QColor::HexRgb)},
            {QStringLiteral("ansi"), ansi}
        });
    return root;
}

} // namespace nord::terminal
