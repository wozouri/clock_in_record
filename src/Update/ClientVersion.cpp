#include "ClientVersion.h"

#include <QDate>
#include <QStringList>

namespace {

bool parseClientVersion(const QString& version, QList<quint32>& parts, bool requirePrefix)
{
    QString normalized = version.trimmed();
    const bool hasPrefix = normalized.startsWith(QLatin1Char('v'));
    if (requirePrefix != hasPrefix) {
        return false;
    }
    if (hasPrefix) {
        normalized.remove(0, 1);
    }
    const QStringList rawParts = normalized.split(QLatin1Char('.'), Qt::KeepEmptyParts);
    if (rawParts.size() != 3 || rawParts.at(0).size() != 4 || rawParts.at(1).size() != 2
        || rawParts.at(2).size() != 2) {
        return false;
    }

    parts.clear();
    parts.reserve(rawParts.size());
    for (const QString& rawPart : rawParts) {
        if (rawPart.isEmpty()) {
            return false;
        }
        for (const QChar character : rawPart) {
            if (!character.isDigit()) {
                return false;
            }
        }
        bool ok = false;
        const uint value = rawPart.toUInt(&ok, 10);
        if (!ok) {
            return false;
        }
        parts.push_back(value);
    }
    return QDate(parts.at(0), static_cast<int>(parts.at(1)), static_cast<int>(parts.at(2))).isValid();
}

}  // namespace

namespace attendance {

bool isValidClientVersion(const QString& version)
{
    QList<quint32> parts;
    return parseClientVersion(version, parts, true);
}

int compareClientVersions(const QString& left, const QString& right)
{
    QList<quint32> leftParts;
    QList<quint32> rightParts;
    if (!parseClientVersion(left, leftParts, false) && !parseClientVersion(left, leftParts, true)) {
        return 0;
    }
    if (!parseClientVersion(right, rightParts, false) && !parseClientVersion(right, rightParts, true)) {
        return 0;
    }

    for (int index = 0; index < leftParts.size(); ++index) {
        const quint32 leftPart = leftParts.at(index);
        const quint32 rightPart = rightParts.at(index);
        if (leftPart < rightPart) {
            return -1;
        }
        if (leftPart > rightPart) {
            return 1;
        }
    }
    return 0;
}

bool isClientVersionNewer(const QString& candidate, const QString& installed)
{
    return compareClientVersions(candidate, installed) > 0;
}

}  // namespace attendance
