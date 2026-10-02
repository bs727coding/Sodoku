#include "storage.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

#include <ctime>
#include <iterator>

namespace sudoku::storage {
namespace {

std::wstring g_dir;

std::wstring exeDir() {
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetModuleFileNameW(nullptr, buf, DWORD(std::size(buf)));
    std::wstring p(buf, n);
    const size_t slash = p.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring(L".") : p.substr(0, slash);
}

bool fileExists(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

std::wstring resolveDir() {
    const std::wstring exe = exeDir();
    if (fileExists(exe + L"\\portable") || fileExists(exe + L"\\portable.txt")) return exe + L"\\data";
    std::wstring dir;
    PWSTR local = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_CREATE, nullptr, &local))) {
        dir = local;
        CoTaskMemFree(local);
    } else {
        wchar_t buf[MAX_PATH];
        const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
        dir = (n && n < MAX_PATH) ? std::wstring(buf, n) : exe;
    }
    return dir + L"\\Sudoku";
}

bool writeAll(HANDLE h, std::string_view data) {
    DWORD written = 0;
    return data.empty() || (WriteFile(h, data.data(), DWORD(data.size()), &written, nullptr) && written == data.size());
}

}  // namespace

const std::wstring& dataDir() {
    if (g_dir.empty()) {
        g_dir = resolveDir();
        CreateDirectoryW(g_dir.c_str(), nullptr);
    }
    return g_dir;
}

void overrideDataDir(const std::wstring& dir) {
    g_dir = dir;
    CreateDirectoryW(g_dir.c_str(), nullptr);
}

std::wstring file(const wchar_t* name) { return dataDir() + L"\\" + name; }

bool readText(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(h, &size) && size.QuadPart < (64ll << 20);
    if (ok) {
        out.resize(size_t(size.QuadPart));
        DWORD read = 0;
        ok = out.empty() || (ReadFile(h, out.data(), DWORD(out.size()), &read, nullptr) && read == out.size());
    }
    CloseHandle(h);
    return ok;
}

bool writeAtomic(const std::wstring& path, std::string_view data) {
    const std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const bool ok = writeAll(h, data);
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

bool appendText(const std::wstring& path, std::string_view data) {
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const bool ok = writeAll(h, data);
    FlushFileBuffers(h);  // history is precious and appends are rare
    CloseHandle(h);
    return ok;
}

bool removeFile(const std::wstring& path) {
    return DeleteFileW(path.c_str()) != 0 || GetLastError() == ERROR_FILE_NOT_FOUND;
}

void openFolder(const std::wstring& path) {
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

int64_t nowUtc() { return int64_t(std::time(nullptr)); }

int todayLocal() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return st.wYear * 10000 + st.wMonth * 100 + st.wDay;
}

int minuteOfDay() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return st.wHour * 60 + st.wMinute;
}

}  // namespace sudoku::storage
