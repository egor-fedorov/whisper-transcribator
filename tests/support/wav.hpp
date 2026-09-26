#pragma once
#include <cstdint>
#include <ostream>
#include <string>

namespace wt::test {
inline void little(std::string& out, unsigned value, int bytes) {
    for (int i = 0; i < bytes; ++i)
        out += static_cast<char>((value >> (8 * i)) & 255);
}
inline void little(std::ostream& out, uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i)
        out.put(static_cast<char>((value >> (8 * i)) & 255));
}
} // namespace wt::test
