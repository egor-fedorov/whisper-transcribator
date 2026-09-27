#pragma once
// POSIX parts of the platform layer that tests replace or call directly.
#include "support/fs.hpp"
#include <unistd.h>

namespace wt::platform {
// The kernel's atomic rename that fails with EEXIST instead of replacing `to`. It fails with
// EINVAL, ENOSYS or ENOTSUP where the kernel or filesystem cannot provide it.
int exclusive_rename(const char* from, const char* to);
// Steps of rename_noreplace(), each returning 0 or -1 with errno set; replaceable in tests.
struct NoReplaceSteps {
    int (*exclusive)(const char* from, const char* to) = exclusive_rename;
    int (*link)(const char* from, const char* to) = ::link;
};
bool rename_noreplace(const fs::path& from, const fs::path& to, const NoReplaceSteps& steps);
} // namespace wt::platform
