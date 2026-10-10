#ifndef ATTENDANCEJSONSERVICE_H
#define ATTENDANCEJSONSERVICE_H

#include <QString>
#include <QDate>
#include <QList>

struct AttendanceImportPreview {
    bool success = false;
    QList<QDate> dates;
    int overwrittenCount = 0;
    bool hasWorkSchedule = false;
    QString errorMessage;
};

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
    static AttendanceImportPreview previewImport(const QString& filePath);
    static AttendanceImportResult importFromLarkJson(const QString& filePath);
    static AttendanceExportResult exportToJson(const QString& filePath);
};

#endif // ATTENDANCEJSONSERVICE_H
