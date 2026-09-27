#include "transcript/outputs.hpp"
#include "support/options.hpp"
#include "transcript/metadata.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace wt {
namespace {
std::string normalized_text(const std::string& value) {
    std::string result;
    bool space = false;
    for (unsigned char c : value) {
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
            space = !result.empty();
        else {
            if (space)
                result += ' ';
            result += static_cast<char>(c);
            space = false;
        }
    }
    return result;
}
bool sentence_end(std::string text) {
    bool removed = true;
    while (removed && !text.empty()) {
        removed = false;
        for (const std::string suffix :
             {"\"", "'", ")", "]", "}", u8"\u00bb", u8"\u201d", u8"\u2019"})
            if (text.size() >= suffix.size() &&
                text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0) {
                text.resize(text.size() - suffix.size());
                removed = true;
                break;
            }
    }
    return !text.empty() &&
           (text.back() == '.' || text.back() == '!' || text.back() == '?' ||
            (text.size() >= 3 && (text.compare(text.size() - 3, 3, u8"\u2026") == 0 ||
                                  text.compare(text.size() - 3, 3, u8"\u3002") == 0)));
}
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
class Paragraphs {
    const Options& options;
    double previous_end = 0;
    size_t characters = 0;
    bool any = false, previous_sentence = false;
    std::string previous_language;

  public:
    explicit Paragraphs(const Options& value) : options(value) {}
    std::string append(const Segment& segment) {
        auto text = normalized_text(segment.text);
        if (text.empty())
            return {};
        bool paragraph =
            any && options.text_layout == "paragraphs" &&
            ((segment.start - previous_end) * 1000 >= options.paragraph_pause_ms ||
             segment.language != previous_language || (characters >= 600 && previous_sentence));
        std::string separator = !any ? "" : paragraph ? "\n\n" : " ";
        if (paragraph)
            characters = 0;
        characters = std::min<size_t>(
            600, characters + std::count_if(text.begin(), text.end(),
                                            [](unsigned char c) { return (c & 0xc0) != 0x80; }));
        previous_sentence = sentence_end(text);
        previous_end = segment.end;
        previous_language = segment.language;
        any = true;
        return separator + text;
    }
};
bool render_paragraphs(const TranscriptSource& source, const Options& options,
                       const std::function<void(const std::string&)>& emit) {
    Paragraphs paragraphs(options);
    bool any = false;
    source.visit([&](const Segment& segment) {
        auto text = paragraphs.append(segment);
        if (!text.empty()) {
            emit(text);
            any = true;
        }
    });
    return any;
}
bool render_text(std::ostream& stream, const Options& options, const TranscriptSource& source) {
    bool any = render_paragraphs(source, options, [&](const auto& text) { stream << text; });
    stream << '\n';
    return any;
}
bool render_srt(std::ostream& stream, const TranscriptSource& source) {
    int64_t index = 0;
    source.visit([&](const Segment& segment) {
        stream << ++index << '\n'
               << timestamp(segment.start) << " --> " << timestamp(segment.end) << '\n'
               << segment.text << "\n\n";
    });
    return index != 0;
}
std::string vtt_timestamp(int64_t ms) {
    auto value = timestamp(ms / 1000.0);
    value[value.size() - 4] = '.';
    return value;
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
bool render_json(std::ostream& stream, const Job& job, const Options& options,
                 const TranscriptSource& source) {
    double duration = source.samples / double(sample_rate);
    Json header = {
        {"schema_version", 2},
        {"source", job.source.string()},
        {"model", options.model},
        {"language", source.languages.size() == 1 ? Json(source.languages.front()) : Json(nullptr)},
        {"languages", source.languages},
        {"language_probability", nullptr},
        {"duration", duration},
        {"duration_after_vad", options.no_vad ? Json(duration) : Json(nullptr)}};
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
    stream << "],\"run\":" << run_metadata(options).dump() << "}\n";
    return any || index != 0;
}
} // namespace
void render_stream(std::ostream& stream, const std::string& format, const Job& job,
                   const Options& options, const TranscriptSource& source) {
    bool any = false;
    if (format == "text")
        any = render_text(stream, options, source);
    else if (format == "json")
        any = render_json(stream, job, options, source);
    else if (format == "srt")
        any = render_srt(stream, source);
    else if (format == "vtt")
        any = render_vtt(stream, source);
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
