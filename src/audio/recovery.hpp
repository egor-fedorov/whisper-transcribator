#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace wt {
class DecodeRecovery {
    std::array<unsigned, 128> window{};
    size_t slot = window.size() - 1;
    unsigned recent = 0, consecutive = 0;
    uint64_t total = 0;

  public:
    void packet();
    void frame() { consecutive = 0; }
    bool recover(int code);
    uint64_t errors() const { return total; }
};
} // namespace wt
