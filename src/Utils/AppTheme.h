#pragma once

#include <QColor>
#include <QString>

class QWidget;

namespace AppTheme {
enum class Color {
    Window, Surface, SubtleSurface, HoverSurface, Border, HoverBorder,
    Text, SecondaryText, MutedText, DisabledText, DisabledSurface,
    Accent, AccentSurface, AccentHover, Selection, SelectionBorder,
    Danger, DangerSurface, DangerBorder, Warning, WarningSurface, WarningBorder,
    Purple, PurpleSurface, Record, AlternateRecord, Today, Note, ArrivalText
};

void initialize();
bool isNightMode();
void setNightMode(bool enabled);
QColor color(Color role);

// ${role} placeholders keep custom widgets in sync with Ela's active theme.
void bindStyleSheet(QWidget* widget, const QString& styleTemplate);
void bindStyleSheet(QWidget* widget, const QWidget* source);
}
