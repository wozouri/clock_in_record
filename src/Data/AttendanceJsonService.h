#ifndef ATTENDANCEJSONSERVICE_H
#define ATTENDANCEJSONSERVICE_H

#include <QString>

struct AttendanceImportResult {
    bool success = false;
    int importedCount = 0;
    QString errorMessage;
};

struct AttendanceExportResult {
    bool success = false;
    bool hasData = true;
    int exportedCount = 0;
    QString errorMessage;
};

class AttendanceJsonService {
public:
    static AttendanceImportResult importFromLarkJson(const QString& filePath);
    static AttendanceExportResult exportToJson(const QString& filePath);
};

#endif // ATTENDANCEJSONSERVICE_H
