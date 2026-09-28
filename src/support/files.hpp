#pragma once
#include "support/fs.hpp"
#include <string>

namespace wt {
fs::path resolve_path(const fs::path& path);
bool same_file(const fs::path& a, const fs::path& b);
void probe_directory(const fs::path& path);
std::string read_text(const fs::path& path);
} // namespace wt
