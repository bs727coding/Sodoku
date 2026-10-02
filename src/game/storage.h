// File locations and crash-safe file I/O for saves, settings and history.
// Data lives in %LOCALAPPDATA%\Sudoku, or in .\data next to the exe when a file named
// "portable" (or "portable.txt") sits beside Sudoku.exe.
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace sudoku::storage {

const std::wstring& dataDir();  // created on first use
std::wstring file(const wchar_t* name);

bool readText(const std::wstring& path, std::string& out);
bool writeAtomic(const std::wstring& path, std::string_view data);  // temp file + atomic rename
bool appendText(const std::wstring& path, std::string_view data);
bool removeFile(const std::wstring& path);
void openFolder(const std::wstring& path);

int64_t nowUtc();    // unix seconds
int todayLocal();    // yyyymmdd
int minuteOfDay();   // minutes since local midnight

// Redirects all storage to `dir` (used by tests and screenshot mode).
void overrideDataDir(const std::wstring& dir);

}  // namespace sudoku::storage
