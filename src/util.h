// Small string helpers shared by the UI code.
#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace sudoku {

inline std::wstring widen(std::string_view s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

// "5:07" or "1:02:03"
inline std::wstring formatDuration(int64_t ms) {
    if (ms < 0) ms = 0;
    const int64_t total = ms / 1000;
    const int h = int(total / 3600), m = int(total / 60 % 60), s = int(total % 60);
    wchar_t buf[32];
    if (h > 0) swprintf(buf, 32, L"%d:%02d:%02d", h, m, s);
    else swprintf(buf, 32, L"%d:%02d", m, s);
    return buf;
}

// Local calendar date (yyyymmdd) of a unix timestamp.
inline int localDateFromUnix(int64_t t) {
    ULARGE_INTEGER u;
    u.QuadPart = uint64_t(t + 11644473600LL) * 10000000ULL;
    FILETIME ft{u.LowPart, u.HighPart}, local{};
    SYSTEMTIME st{};
    if (!FileTimeToLocalFileTime(&ft, &local) || !FileTimeToSystemTime(&local, &st)) return 0;
    return st.wYear * 10000 + st.wMonth * 100 + st.wDay;
}

inline const wchar_t* monthName(int m) {
    static const wchar_t* names[] = {L"January", L"February", L"March",     L"April",   L"May",      L"June",
                                     L"July",    L"August",   L"September", L"October", L"November", L"December"};
    return names[(m - 1) % 12];
}

inline const wchar_t* weekdayName(int wd) {  // 0 = Monday
    static const wchar_t* names[] = {L"Monday", L"Tuesday", L"Wednesday", L"Thursday", L"Friday", L"Saturday", L"Sunday"};
    return names[wd % 7];
}

}  // namespace sudoku
