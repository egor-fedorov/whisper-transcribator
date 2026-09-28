#include "transcript/boundaries.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wt {
size_t pause_cut(size_t samples, const std::vector<std::pair<int64_t, int64_t>>& speech,
                 int minimum_silence_ms) {
    int64_t previous = 0, selected = static_cast<int64_t>(samples);
    auto gap = [&](int64_t end) {
        auto middle = previous + (end - previous) / 2;
        if (end > previous && end - previous >= int64_t(minimum_silence_ms) * 16 &&
            middle >= static_cast<int64_t>(samples * 3 / 4) &&
            middle < static_cast<int64_t>(samples))
            selected = middle;
    };
    for (const auto& [start, end] : speech) {
        if (start < previous || end < start || end > static_cast<int64_t>(samples))
            throw std::runtime_error("Invalid VAD intervals");
        gap(start);
        previous = end;
    }
    gap(static_cast<int64_t>(samples));
    return static_cast<size_t>(selected);
}
size_t committed_cut(size_t samples, size_t preferred, const Transcript& transcript,
                     size_t guard_samples) {
    if (!preferred || preferred > samples)
        throw std::runtime_error("Invalid preferred boundary");
    if (transcript.segments.empty())
        return samples;
    auto target = std::min(preferred, samples > guard_samples ? samples - guard_samples : 0);
    double duration = samples / double(sample_rate);
    std::vector<double> suffix(transcript.segments.size() + 1, duration);
    for (size_t i = transcript.segments.size(); i-- > 0;) {
        const auto& segment = transcript.segments[i];
        if (!std::isfinite(segment.start) || !std::isfinite(segment.end) || segment.start < 0 ||
            segment.end < segment.start || segment.end > duration)
            throw std::runtime_error("Invalid recognition timestamps");
        suffix[i] = std::min(suffix[i + 1], segment.start);
    }
    double end = 0;
    size_t selected = 0;
    for (size_t i = 0; i < transcript.segments.size(); ++i) {
        end = std::max(end, transcript.segments[i].end);
        auto count = static_cast<size_t>(std::ceil(end * sample_rate));
        if (count && count <= target && suffix[i + 1] >= count / double(sample_rate))
            selected = count;
    }
    return selected ? selected : samples;
}
} // namespace wt
