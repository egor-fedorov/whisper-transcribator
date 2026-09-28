#pragma once
#include <string>

namespace wt {
std::string trim(const std::string& value);
std::string env(const char* key);
} // namespace wt
