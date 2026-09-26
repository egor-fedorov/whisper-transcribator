#include "pipeline.hpp"
#include "version.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>

namespace wt {
Json run_metadata(const Options& options) {
    return {{"backend", "whisper.cpp"},
            {"version", WT_VERSION},
            {"backend_revision", WT_WHISPER_REVISION},
            {"device", options.device},
            {"beam_size", options.beam_size},
            {"cpu_threads", options.cpu_threads},
            {"vad", !options.no_vad},
            {"vad_min_silence_ms", options.vad_min_silence_ms},
            {"flash_attention", true},
            {"chunk_seconds", options.chunk_seconds},
            {"chunk_min_silence_ms", options.chunk_min_silence_ms},
            {"chunking_version", chunking_version}};
}
void render_stream(std::ostream& stream, const std::string& format, const Job& job,
                   const Options& options, const Journal& journal) {
    bool any = false;
    if (format == "text" || format == "json") {
        if (format == "json") {
            double duration = journal.samples() / double(sample_rate);
            Json header = {{"schema_version", 1},
                           {"source", job.source.string()},
                           {"model", options.model},
                           {"language", journal.language()},
                           {"language_probability", nullptr},
                           {"duration", duration},
                           {"duration_after_vad", options.no_vad ? Json(duration) : Json(nullptr)}};
            auto text = header.dump();
            text.pop_back();
            stream << text << ",\"text\":\"";
        }
        journal.visit([&](const Segment& segment) {
            if (any)
                stream << ' ';
            if (format == "text")
                stream << segment.text;
            else {
                auto escaped = Json(segment.text).dump();
                stream.write(escaped.data() + 1, static_cast<std::streamsize>(escaped.size() - 2));
            }
            any = true;
        });
        stream << (format == "text" ? "\n" : "\",\"segments\":[");
    }
    if (format == "srt" || format == "json") {
        int64_t index = 0;
        journal.visit([&](const Segment& segment) {
            if (format == "srt")
                stream << index + 1 << '\n'
                       << timestamp(segment.start) << " --> " << timestamp(segment.end) << '\n'
                       << segment.text << "\n\n";
            else {
                if (index)
                    stream << ',';
                stream << Json({{"id", index},
                                {"start", segment.start},
                                {"end", segment.end},
                                {"text", segment.text},
                                {"avg_logprob", nullptr},
                                {"compression_ratio", nullptr},
                                {"no_speech_prob", segment.no_speech_probability}})
                              .dump();
            }
            ++index;
            any = true;
        });
        if (format == "json")
            stream << "],\"run\":" << run_metadata(options).dump() << "}\n";
    }
    if (!any)
        throw std::runtime_error("No transcript produced for: " + job.source.string());
}
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
