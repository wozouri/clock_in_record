#include "AppTheme.h"

#include <ElaTheme.h>
#include <QApplication>
#include <QPalette>
#include <QSettings>
#include <QVariant>
#include <QWidget>

namespace {
struct ThemeColor {
    const char* name;
    const char* light;
    const char* dark;
};

// Keep this table in AppTheme::Color order.
constexpr ThemeColor colors[] = {
    {"window", "#f7f9fc", "#202020"},
    {"surface", "#ffffff", "#282828"},
    {"subtleSurface", "#f3f7fb", "#30363e"},
    {"hoverSurface", "#edf4fb", "#354352"},
    {"border", "#dfe7ef", "#414953"},
    {"hoverBorder", "#9ebdd8", "#6685a3"},
    {"text", "#223550", "#e6edf5"},
    {"secondaryText", "#40566f", "#bdc9d8"},
    {"mutedText", "#65778a", "#9aaabe"},
    {"disabledText", "#9aa7b5", "#78828f"},
    {"disabledSurface", "#f4f6f8", "#252a30"},
    {"accent", "#1769aa", "#8ac8fa"},
    {"accentSurface", "#eaf4fd", "#293f53"},
    {"accentHover", "#dceefd", "#35546e"},
    {"selection", "#d9ecfb", "#304f69"},
    {"selectionBorder", "#1f79bd", "#73bcf4"},
    {"danger", "#d64d4d", "#ff9891"},
    {"dangerSurface", "#fff1f0", "#4b3030"},
    {"dangerBorder", "#e9b7b1", "#92564f"},
    {"warning", "#a86c22", "#edbd78"},
    {"warningSurface", "#fff0dc", "#4b3c29"},
    {"warningBorder", "#e6ad55", "#c39350"},
    {"purple", "#74559c", "#c6acec"},
    {"purpleSurface", "#eee8f7", "#42374f"},
    {"record", "#d8f6df", "#294735"},
    {"alternateRecord", "#d5fef4", "#344b6b"},
    {"today", "#1769aa", "#327db8"},
    {"note", "#c98a30", "#e8b260"},
    {"arrivalText", "#245c78", "#8ac8fa"}
};
constexpr char styleProperty[] = "attendanceThemeStyle";

void applyStyleSheet(QWidget* widget)
{
    QString sheet = widget->property(styleProperty).toString();
    for (const auto& entry : colors) {
        sheet.replace(QStringLiteral("${%1}").arg(QLatin1String(entry.name)),
            QLatin1String(AppTheme::isNightMode() ? entry.dark : entry.light));
    }
    widget->setStyleSheet(sheet);
    widget->update();
}
}

namespace AppTheme {
bool isNightMode()
{
    return eTheme->getThemeMode() == ElaThemeType::Dark;
}

QColor color(Color role)
{
    const auto& entry = colors[static_cast<int>(role)];
    return QColor(QLatin1String(isNightMode() ? entry.dark : entry.light));
}

void initialize()
{
    static const QPalette lightPalette = qApp->palette();
    const auto updatePalette = [] {
        QPalette palette = lightPalette;
        if (isNightMode()) {
            palette.setColor(QPalette::Window, color(Color::Window));
            palette.setColor(QPalette::Base, color(Color::Surface));
            palette.setColor(QPalette::AlternateBase, color(Color::SubtleSurface));
            palette.setColor(QPalette::Button, color(Color::Surface));
            for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
                palette.setColor(role, color(Color::Text));
                palette.setColor(QPalette::Disabled, role, color(Color::DisabledText));
            }
            palette.setColor(QPalette::ToolTipBase, color(Color::Surface));
            palette.setColor(QPalette::ToolTipText, color(Color::Text));
            palette.setColor(QPalette::Highlight, color(Color::Today));
            palette.setColor(QPalette::HighlightedText, Qt::white);
            palette.setColor(QPalette::Link, color(Color::Accent));
        }
        qApp->setPalette(palette);
    };
    static const auto connection = QObject::connect(eTheme, &ElaTheme::themeModeChanged, qApp, updatePalette);
    Q_UNUSED(connection);
    const auto saved = QSettings().value(QStringLiteral("appearance/theme"), QStringLiteral("light"));
    eTheme->setThemeMode(saved.toString() == QStringLiteral("dark")
        ? ElaThemeType::Dark : ElaThemeType::Light);
    updatePalette();
}

void setNightMode(bool enabled)
{
    QSettings settings;
    settings.setValue(QStringLiteral("appearance/theme"),
        enabled ? QStringLiteral("dark") : QStringLiteral("light"));
    eTheme->setThemeMode(enabled ? ElaThemeType::Dark : ElaThemeType::Light);
}

void bindStyleSheet(QWidget* widget, const QString& styleTemplate)
{
    if (!widget->property(styleProperty).isValid()) {
        QObject::connect(eTheme, &ElaTheme::themeModeChanged, widget,
            [widget] { applyStyleSheet(widget); });
    }
    widget->setProperty(styleProperty, styleTemplate);
    applyStyleSheet(widget);
}

void bindStyleSheet(QWidget* widget, const QWidget* source)
{
    bindStyleSheet(widget, source->property(styleProperty).toString());
}
}
