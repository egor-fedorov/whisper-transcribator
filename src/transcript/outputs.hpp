#pragma once
#include "support/json.hpp"
#include "transcript/options.hpp"
#include "transcript/types.hpp"
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>

namespace wt {
struct Job;
class Journal;
using SegmentConsumer = std::function<void(const Segment&)>;
struct TranscriptSource {
    int64_t samples;
    std::vector<std::string> languages;
    // Rendering JSON traverses the bounded source once for text and once for segments.
    std::function<void(const SegmentConsumer&)> visit;
    // Prepared by the application, or restored unchanged from a checkpoint.
    Json metadata = Json::object();
};
std::string timestamp(double seconds);
void render_stream(std::ostream& stream, const std::string& format, const Job& job,
                   const RenderOptions& options, const TranscriptSource& transcript);
void publish_outputs(const Job& job, const RenderOptions& options, Journal& journal,
                     bool overwrite);
} // namespace wt
