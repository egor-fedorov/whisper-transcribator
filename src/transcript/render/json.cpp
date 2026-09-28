#include "transcript/render/detail/formatters.hpp"
#include <ostream>

namespace wt::render_detail {
bool render_json(std::ostream& stream, const Job& job, const RenderOptions& options,
                 const TranscriptSource& source) {
    double duration = source.samples / double(sample_rate);
    Json header = {
        {"schema_version", 2},
        {"source", job.source.string()},
        {"model", source.metadata.at("model")},
        {"language", source.languages.size() == 1 ? Json(source.languages.front()) : Json(nullptr)},
        {"languages", source.languages},
        {"language_probability", nullptr},
        {"duration", duration},
        {"duration_after_vad",
         source.metadata.at("run").at("vad").get<bool>() ? Json(nullptr) : Json(duration)}};
    auto text = header.dump();
    text.pop_back();
    stream << text << ",\"text\":\"";
    bool any = render_paragraphs(source, options, [&](const auto& value) {
        auto escaped = Json(value).dump();
        stream.write(escaped.data() + 1, static_cast<std::streamsize>(escaped.size() - 2));
    });
    stream << "\",\"segments\":[";
    int64_t index = 0;
    source.visit([&](const Segment& segment) {
        if (index)
            stream << ',';
        stream << Json({{"id", index},
                        {"start", segment.start},
                        {"end", segment.end},
                        {"text", segment.text},
                        {"language", segment.language},
                        {"avg_logprob", nullptr},
                        {"compression_ratio", nullptr},
                        {"no_speech_prob", segment.no_speech_probability}})
                      .dump();
        ++index;
    });
    stream << "],\"run\":" << source.metadata.at("run").dump() << "}\n";
    return any || index != 0;
}
} // namespace wt::render_detail
