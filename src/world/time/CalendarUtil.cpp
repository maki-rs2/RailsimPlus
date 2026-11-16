#include "CalendarUtil.h"

namespace rs::world {

bool IsLeapYear(int64_t year) {
    return (year % 4 == 0) && ((year % 100 != 0) || (year % 400 == 0));
}

DateParts ToDateParts(int64_t dayNumber) {
    // Howard Hinnant "chrono-Compatible Low-Level Date Algorithms" の
    // civil_from_days を移植(shifted epoch: 0000-03-01起点)。
    int64_t z = dayNumber + 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    int64_t doe = z - era * 146097;                                    // [0, 146096]
    int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365; // [0, 399]
    int64_t y = yoe + era * 400;
    int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);              // [0, 365]
    int64_t mp = (5 * doy + 2) / 153;                                   // [0, 11]
    int64_t d = doy - (153 * mp + 2) / 5 + 1;                           // [1, 31]
    int64_t m = mp + (mp < 10 ? 3 : -9);                                // [1, 12]

    DateParts parts;
    parts.year  = static_cast<int>(y + (m <= 2 ? 1 : 0));
    parts.month = static_cast<int>(m);
    parts.day   = static_cast<int>(d);
    return parts;
}

int64_t FromDateParts(const DateParts& parts) {
    // civil_from_days の逆変換(days_from_civil)。
    int64_t y = parts.year - (parts.month <= 2 ? 1 : 0);
    int64_t m = parts.month;
    int64_t d = parts.day;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;                                        // [0, 399]
    int64_t mp = (m + 9) % 12;                                          // [0, 11]
    int64_t doy = (153 * mp + 2) / 5 + d - 1;                           // [0, 365]
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;                 // [0, 146096]
    return era * 146097 + doe - 719468;
}

int DayOfWeek(int64_t dayNumber) {
    int64_t offset = (dayNumber + kEpochWeekday) % 7;
    if (offset < 0) offset += 7;
    return static_cast<int>(offset);
}

int DayOfYear(int64_t dayNumber) {
    DateParts parts = ToDateParts(dayNumber);
    DateParts jan1{parts.year, 1, 1};
    return static_cast<int>(dayNumber - FromDateParts(jan1)) + 1;
}

} // namespace rs::world
