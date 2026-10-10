#include "AppFont.h"

#include <QApplication>
#include <QDebug>
#include <QFont>
#include <QFontDatabase>

void initializeAppFont()
{
    static const QString family = [] {
        QString regularFamily;
        for (const auto& path : {QStringLiteral(":/fonts/HarmonyOS_Sans_SC_Regular.ttf"),
                                QStringLiteral(":/fonts/HarmonyOS_Sans_SC_Bold.ttf")}) {
            const int id = QFontDatabase::addApplicationFont(path);
            if (id < 0) {
                qWarning() << "Unable to load bundled font:" << path;
                continue;
            }
            const auto families = QFontDatabase::applicationFontFamilies(id);
            if (regularFamily.isEmpty() && !families.isEmpty()) regularFamily = families.first();
        }
        return regularFamily;
    }();

    QFont font(family.isEmpty() ? QStringLiteral("Microsoft YaHei") : family);
    font.setPointSize(10);
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    QApplication::setFont(font);
}
