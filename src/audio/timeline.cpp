#include "audio/timeline.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace wt {
namespace {
int64_t add(int64_t a, int64_t b) {
    if ((b > 0 && a > std::numeric_limits<int64_t>::max() - b) ||
        (b < 0 && a < std::numeric_limits<int64_t>::min() - b))
        throw std::runtime_error("Audio timestamp overflow");
    return a + b;
}
} // namespace
AudioPlacement AudioTimeline::locate(std::optional<int64_t> pts, int64_t delay, int64_t tolerance,
                                     bool transport, TimestampGaps gaps) {
    auto expected = add(position, delay);
    auto start = pts ? add(*pts, shift) : expected;
    constexpr auto bound = std::numeric_limits<int64_t>::max() / 4;
    if (start < -bound || start > bound || expected > bound)
        throw std::runtime_error("Audio timestamp out of range");
    bool reset = false;
    auto delta = start - expected;
    auto jitter = std::max<int64_t>(1600, tolerance);
    // The first timestamp defines preroll/leading delay, not clock jitter.
    if (started && delta >= -jitter && delta <= jitter)
        start = expected;
    else if (started && (delta < -jitter ||
                         (transport && gaps == TimestampGaps::automatic && delta > 160000))) {
        // A slow recording clock can accumulate negative drift in any container.
        // Keep every decoded sample instead of trimming speech or failing the file.
        shift = add(shift, -delta);
        start = expected;
        reset = true;
    }
    started = true;
    return {start, reset || start != expected, reset};
}
void AudioTimeline::advance(int64_t start, size_t count) {
    if (!count)
        return;
    if (count > static_cast<size_t>(std::numeric_limits<int64_t>::max()))
        throw std::runtime_error("Audio sample counter overflow");
    auto end = add(start, static_cast<int64_t>(count));
    position = advanced ? std::max(position, end) : end;
    advanced = true;
}
} // namespace wt
