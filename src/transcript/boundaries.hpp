#pragma once
#include "transcript/types.hpp"
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace wt {
size_t committed_cut(size_t samples, size_t preferred, const Transcript& transcript,
                     size_t guard_samples = 2 * sample_rate);
size_t pause_cut(size_t samples, const std::vector<std::pair<int64_t, int64_t>>& speech,
                 int minimum_silence_ms);
} // namespace wt
