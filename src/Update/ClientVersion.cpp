#include "ClientVersion.h"

#include <QStringList>

#include <algorithm>
#include <limits>

namespace {

bool parseClientVersion(const QString& version, QList<quint32>& parts)
{
    const QString normalized = version.trimmed();
    const QStringList rawParts = normalized.split(QLatin1Char('.'), Qt::KeepEmptyParts);
    if (rawParts.size() < 3) {
        return false;
    }

    parts.clear();
    parts.reserve(rawParts.size());
    for (const QString& rawPart : rawParts) {
        if (rawPart.isEmpty() || rawPart.size() > 10) {
            return false;
        }
        for (const QChar character : rawPart) {
            if (!character.isDigit()) {
                return false;
            }
        }
        bool ok = false;
        const qulonglong value = rawPart.toULongLong(&ok, 10);
        if (!ok || value > std::numeric_limits<quint32>::max()) {
            return false;
        }
        parts.push_back(static_cast<quint32>(value));
    }
    return true;
}

}  // namespace

namespace attendance {

bool isValidClientVersion(const QString& version)
{
    QList<quint32> parts;
    return parseClientVersion(version, parts);
}

int compareClientVersions(const QString& left, const QString& right)
{
    QList<quint32> leftParts;
    QList<quint32> rightParts;
    if (!parseClientVersion(left, leftParts) || !parseClientVersion(right, rightParts)) {
        return 0;
    }

    const int count = std::max(leftParts.size(), rightParts.size());
    for (int index = 0; index < count; ++index) {
        const quint32 leftPart = index < leftParts.size() ? leftParts.at(index) : 0;
        const quint32 rightPart = index < rightParts.size() ? rightParts.at(index) : 0;
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
