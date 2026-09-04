#pragma once

#include "AttendanceTypes.h"

class WorkTimeCalculator {
public:
    static WorkTimeResult calculateWorkTimeResult(
        const AttendanceRecord& record,
        const WorkSchedule& schedule);
};
