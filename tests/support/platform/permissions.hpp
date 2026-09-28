#pragma once
#include "support/fs.hpp"

namespace wt::test {
void permissions(const fs::path& path, fs::perms mode);
void permissions(const fs::path& path, fs::perms mode, std::error_code& error);
} // namespace wt::test
