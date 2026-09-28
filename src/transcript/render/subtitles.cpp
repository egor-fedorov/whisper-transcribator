#include "transcript/render/detail/formatters.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace wt::render_detail {
namespace {
std::string cue_text(const std::string& text) {
    std::string result;
    for (char c : normalized_text(text)) {
        if (c == '&')
            result += "&amp;";
        else if (c == '<')
            result += "&lt;";
        else if (c == '>')
            result += "&gt;";
        else if (c != '\0')
            result += c;
    }
    return result;
}
std::string vtt_timestamp(int64_t ms) {
    auto value = timestamp(ms / 1000.0);
    value[value.size() - 4] = '.';
    return value;
}
} // namespace
bool render_srt(std::ostream& stream, const TranscriptSource& source) {
    int64_t index = 0;
    source.visit([&](const Segment& segment) {
        stream << ++index << '\n'
               << timestamp(segment.start) << " --> " << timestamp(segment.end) << '\n'
               << normalized_text(segment.text) << "\n\n";
    });
    return index != 0;
}
bool render_vtt(std::ostream& stream, const TranscriptSource& source) {
    stream << "WEBVTT\n\n";
    int64_t index = 0, previous_start = 0;
    source.visit([&](const Segment& segment) {
        auto from =
            std::max(previous_start, static_cast<int64_t>(std::llround(segment.start * 1000)));
        auto to = std::max(from + 1, static_cast<int64_t>(std::llround(segment.end * 1000)));
        stream << ++index << '\n'
               << vtt_timestamp(from) << " --> " << vtt_timestamp(to) << '\n'
               << cue_text(segment.text) << "\n\n";
        previous_start = from;
    });
    return index != 0;
}
} // namespace wt::render_detail
namespace wt {
std::string timestamp(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0)
        throw std::runtime_error("Invalid timestamp");
    auto ms = static_cast<int64_t>(std::llround(seconds * 1000));
    std::ostringstream output;
    output << std::setfill('0') << std::setw(2) << ms / 3600000 << ':' << std::setw(2)
           << ms / 60000 % 60 << ':' << std::setw(2) << ms / 1000 % 60 << ',' << std::setw(3)
           << ms % 1000;
    return output.str();
}
} // namespace wt
