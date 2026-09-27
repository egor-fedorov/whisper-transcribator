#pragma once
#include "support/json.hpp"
#include "transcript/types.hpp"
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>

namespace wt {
struct Options;
struct Job;
class Journal;
using SegmentConsumer = std::function<void(const Segment&)>;
struct TranscriptSource {
    int64_t samples;
    std::vector<std::string> languages;
    // Rendering JSON traverses the bounded source once for text and once for segments.
    std::function<void(const SegmentConsumer&)> visit;
    Json metadata = Json::object();
};
std::string timestamp(double seconds);
void render_stream(std::ostream& stream, const std::string& format, const Job& job,
                   const Options& options, const TranscriptSource& transcript);
void publish_outputs(const Job& job, const Options& options, Journal& journal);
} // namespace wt
