#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>

namespace wt {
enum class TimestampGaps { automatic, preserve };
struct AudioPlacement {
    int64_t start;
    bool discontinuity, reset;
};
class AudioTimeline {
    int64_t position = 0, shift = 0;
    bool started = false, advanced = false;

  public:
    int64_t end() const { return position; }
    AudioPlacement locate(std::optional<int64_t> pts, int64_t delay, int64_t tolerance,
                          bool allow_reset, TimestampGaps gaps = TimestampGaps::automatic);
    void advance(int64_t start, size_t count);
};
} // namespace wt
