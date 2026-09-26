#include "app.hpp"
#include "version.hpp"
#include <cmath>
#include <iomanip>
#include <sstream>

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
Json render_json(const Job& job, const Options& options, const Transcript& result) {
    Json segments = Json::array();
    std::string text;
    for (const auto& segment : result.segments) {
        auto part = trim(segment.text);
        if (part.empty())
            continue;
        if (!std::isfinite(segment.start) || !std::isfinite(segment.end) || segment.start < 0 ||
            segment.end < segment.start)
            throw std::runtime_error("Invalid segment timestamps");
        if (!text.empty())
            text += ' ';
        text += part;
        segments.push_back({{"id", segments.size()},
                            {"start", segment.start},
                            {"end", segment.end},
                            {"text", part},
                            {"avg_logprob", nullptr},
                            {"compression_ratio", nullptr},
                            {"no_speech_prob", segment.no_speech_probability}});
    }
    if (text.empty())
        throw std::runtime_error("No transcript produced for: " + job.source.string());
    return {{"schema_version", 1},
            {"source", job.source.string()},
            {"model", options.model},
            {"language", result.language},
            {"language_probability", nullptr},
            {"duration", result.duration},
            {"duration_after_vad", options.no_vad ? Json(result.duration) : Json(nullptr)},
            {"text", text},
            {"segments", segments},
            {"run",
             {{"backend", "whisper.cpp"},
              {"version", WT_VERSION},
              {"backend_revision", WT_WHISPER_REVISION},
              {"device", options.device},
              {"beam_size", options.beam_size},
              {"cpu_threads", options.cpu_threads},
              {"vad", !options.no_vad},
              {"vad_min_silence_ms", options.vad_min_silence_ms},
              {"flash_attention", true}}}};
}
void write_outputs(const Job& job, const Options& options, const Transcript& result) {
    auto data = render_json(job, options, result);
    std::string srt;
    for (const auto& segment : data["segments"])
        srt += std::to_string(segment["id"].get<int>() + 1) + '\n' + timestamp(segment["start"]) +
               " --> " + timestamp(segment["end"]) + '\n' + segment["text"].get<std::string>() +
               "\n\n";
    for (const auto& [format, path] : job.outputs) {
        auto content = format == "text"  ? data["text"].get<std::string>() + '\n'
                       : format == "srt" ? srt
                                         : data.dump(2) + '\n';
        check_cancelled();
        atomic_write(path, content, options.overwrite);
    }
}
} // namespace wt
