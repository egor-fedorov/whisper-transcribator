#pragma once
#include "transcript/types.hpp"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <utility>

namespace wt {
class Journal;
using ReadAudio = std::function<std::vector<float>(size_t)>;
using Recognize = std::function<Transcript(const std::vector<float>&)>;
using ChooseCut = std::function<size_t(const std::vector<float>&)>;
struct WindowProgress {
    std::function<void(int64_t, size_t)> recognizing;
    std::function<void(int64_t)> committed;
};
void run_chunks(Journal& journal, size_t limit, const ReadAudio& read, const Recognize& recognize,
                const ChooseCut& cut, const WindowProgress& progress = {},
                size_t guard_samples = 2 * sample_rate);
size_t committed_cut(size_t samples, size_t preferred, const Transcript& transcript,
                     size_t guard_samples = 2 * sample_rate);
size_t pause_cut(size_t samples, const std::vector<std::pair<int64_t, int64_t>>& speech,
                 int minimum_silence_ms);
} // namespace wt
