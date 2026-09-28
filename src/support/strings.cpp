#include "support/strings.hpp"
#include <cstdlib>

namespace wt {
std::string env(const char* key) {
    const char* value = std::getenv(key);
    return value ? value : "";
}
std::string trim(const std::string& value) {
    auto first = value.find_first_not_of(" \t\r\n");
    return first == std::string::npos
               ? ""
               : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
} // namespace wt
