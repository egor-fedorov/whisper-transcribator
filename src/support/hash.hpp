#pragma once
#include "support/fs.hpp"
#include <string>

namespace wt {
std::string sha256(const fs::path& path);
std::string sha256_text(const std::string& text);
} // namespace wt
