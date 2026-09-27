#include "audio/recovery.hpp"
#include <stdexcept>
extern "C" {
#include <libavutil/error.h>
}

namespace wt {
void DecodeRecovery::packet() {
    slot = (slot + 1) % window.size();
    recent -= window[slot];
    window[slot] = 0;
}
bool DecodeRecovery::recover(int code) {
    if (code != AVERROR_INVALIDDATA)
        return false;
    ++total;
    ++window[slot];
    ++recent;
    if (++consecutive >= 8)
        throw std::runtime_error("Too many consecutive audio decoder errors (8)");
    if (recent >= 32)
        throw std::runtime_error("Too many audio decoder errors (32 within 128 input packets)");
    return true;
}
} // namespace wt
