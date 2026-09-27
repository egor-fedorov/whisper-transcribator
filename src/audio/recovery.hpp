#pragma once
#include <cstdint>

namespace wt {
struct DecodeErrorPolicy {
    bool strict = false;
    int limit_seconds = 30;
};
class DecodeRecovery {
    DecodeErrorPolicy policy;
    double without_frame = 0;
    unsigned stalled = 0;
    bool damaged = false;
    uint64_t total = 0;

  public:
    explicit DecodeRecovery(DecodeErrorPolicy policy = {});
    void packet(double seconds);
    void frame();
    void awaiting_input() const;
    bool recover(int code);
    uint64_t errors() const { return total; }
};
} // namespace wt
