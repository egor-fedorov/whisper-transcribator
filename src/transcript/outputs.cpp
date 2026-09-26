#include "transcript/outputs.hpp"
#include "support/options.hpp"
#include "transcript/journal.hpp"
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
} // namespace
void render_stream(std::ostream& stream, const std::string& format, const Job& job,
                   const Options& options, const Journal& journal) {
    bool any = false;
    if (format == "text" || format == "json") {
        if (format == "json") {
            double duration = journal.samples() / double(sample_rate);
            Json header = {
                {"schema_version", 2},
                {"source", job.source.string()},
                {"model", options.model},
                {"language", journal.language().empty() ? Json(nullptr) : Json(journal.language())},
                {"languages", journal.languages()},
                {"language_probability", nullptr},
                {"duration", duration},
                {"duration_after_vad", options.no_vad ? Json(duration) : Json(nullptr)}};
            auto text = header.dump();
            text.pop_back();
            stream << text << ",\"text\":\"";
        }
        double previous_end = 0;
        size_t paragraph_chars = 0;
        bool previous_sentence = false;
        std::string previous_language;
        journal.visit([&](const Segment& segment) {
            auto text = normalized_text(segment.text);
            if (text.empty())
                return;
            bool paragraph = any && options.text_layout == "paragraphs" &&
                             ((segment.start - previous_end) * 1000 >= options.paragraph_pause_ms ||
                              segment.language != previous_language ||
                              (paragraph_chars >= 600 && previous_sentence));
            std::string separator = !any ? "" : paragraph ? "\n\n" : " ";
            if (paragraph)
                paragraph_chars = 0;
            auto emit = [&](const std::string& value) {
                if (format == "text") {
                    stream << value;
                    return;
                }
                auto escaped = Json(value).dump();
                stream.write(escaped.data() + 1, static_cast<std::streamsize>(escaped.size() - 2));
            };
            emit(separator);
            emit(text);
            paragraph_chars = std::min<size_t>(
                600, paragraph_chars + std::count_if(text.begin(), text.end(), [](unsigned char c) {
                         return (c & 0xc0) != 0x80;
                     }));
            previous_sentence = sentence_end(text);
            previous_end = segment.end;
            previous_language = segment.language;
            any = true;
        });
        stream << (format == "text" ? "\n" : "\",\"segments\":[");
    }
    if (format == "srt" || format == "vtt" || format == "json") {
        if (format == "vtt")
            stream << "WEBVTT\n\n";
        int64_t index = 0;
        int64_t previous_start = 0;
        journal.visit([&](const Segment& segment) {
            if (format == "srt")
                stream << index + 1 << '\n'
                       << timestamp(segment.start) << " --> " << timestamp(segment.end) << '\n'
                       << segment.text << "\n\n";
            else if (format == "vtt") {
                auto from = std::max(previous_start,
                                     static_cast<int64_t>(std::llround(segment.start * 1000)));
                auto to =
                    std::max(from + 1, static_cast<int64_t>(std::llround(segment.end * 1000)));
                auto vtt_time = [](int64_t ms) {
                    auto value = timestamp(ms / 1000.0);
                    value[value.size() - 4] = '.';
                    return value;
                };
                stream << index + 1 << '\n'
                       << vtt_time(from) << " --> " << vtt_time(to) << '\n'
                       << cue_text(segment.text) << "\n\n";
                previous_start = from;
            } else {
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
void publish_outputs(const Job& job, const Options& options, Journal& journal) {
    journal.publish(job, options, [&](auto& out, const auto& format) {
        render_stream(out, format, job, options, journal);
    });
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
