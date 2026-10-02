// Calendar helpers. Dates are packed as yyyymmdd integers (e.g. 20261001).
#pragma once

namespace sudoku {

constexpr int packDate(int y, int m, int d) { return y * 10000 + m * 100 + d; }
constexpr int dateYear(int dt) { return dt / 10000; }
constexpr int dateMonth(int dt) { return dt / 100 % 100; }
constexpr int dateDay(int dt) { return dt % 100; }

// Days since 1970-01-01 (H. Hinnant's civil calendar algorithms).
constexpr int daysFromCivil(int y, int m, int d) {
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = unsigned(y - era * 400);
    const unsigned doy = unsigned((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + int(doe) - 719468;
}

constexpr int civilFromDays(int z) {
    z += 719468;
    const int era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned doe = unsigned(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const int y = int(yoe) + era * 400;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    const unsigned d = doy - (153 * mp + 2) / 5 + 1;
    const unsigned m = mp < 10 ? mp + 3 : mp - 9;
    return packDate(y + (m <= 2), int(m), int(d));
}

constexpr int dateToDays(int dt) { return daysFromCivil(dateYear(dt), dateMonth(dt), dateDay(dt)); }
constexpr int addDays(int dt, int n) { return civilFromDays(dateToDays(dt) + n); }

// 0 = Monday ... 6 = Sunday (1970-01-01 was a Thursday).
constexpr int weekday(int dt) { return ((dateToDays(dt) % 7) + 7 + 3) % 7; }

constexpr bool isLeapYear(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
constexpr int daysInMonth(int y, int m) {
    constexpr int k[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return m == 2 && isLeapYear(y) ? 29 : k[m - 1];
}

static_assert(civilFromDays(daysFromCivil(2026, 10, 1)) == 20261001);
static_assert(weekday(20261001) == 3);  // Thursday

}  // namespace sudoku
