#pragma once
#include "audio/recovery.hpp"
#include "audio/timeline.hpp"

namespace wt {
struct AudioOptions {
    int stream = -1;
    TimestampGaps gaps = TimestampGaps::automatic;
    DecodeErrorPolicy errors;
};
} // namespace wt
