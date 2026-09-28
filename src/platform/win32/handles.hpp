#pragma once
#include "platform/file.hpp"
#include <string>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace wt::platform::win32 {
constexpr DWORD share_all = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
inline HANDLE handle(const File& file) { return reinterpret_cast<HANDLE>(file.native()); }
inline File::Native native(HANDLE handle) { return reinterpret_cast<File::Native>(handle); }
void fail(DWORD error = GetLastError());
File open_unfollowed(const fs::path& path, DWORD access, DWORD disposition, DWORD flags,
                     SECURITY_ATTRIBUTES* security = nullptr);
bool is_directory(HANDLE file);
std::wstring final_path(HANDLE file);
File reopen(const File& file, DWORD access);
} // namespace wt::platform::win32
