#pragma once
#include "platform/win32/handles.hpp"

namespace wt::platform::win32 {
std::optional<FileStatus> query(HANDLE file, bool with_security);
std::optional<FileStatus> status_at(const fs::path& path, DWORD flags);
} // namespace wt::platform::win32
