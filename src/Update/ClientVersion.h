#ifndef CLIENTVERSION_H
#define CLIENTVERSION_H

#include <QString>

namespace attendance {

// 点分纯数字版本号，如 1.0.0。
bool isValidClientVersion(const QString& version);

// 左侧小于/等于/大于右侧时返回 -1/0/1；无法解析的版本返回 0。
int compareClientVersions(const QString& left, const QString& right);

bool isClientVersionNewer(const QString& candidate, const QString& installed);

}  // namespace attendance

#endif  // CLIENTVERSION_H
