#include "transcript/render/detail/formatters.hpp"
#include <algorithm>
#include <ostream>

namespace wt::render_detail {
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
namespace {
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
class Paragraphs {
    const RenderOptions& options;
    double previous_end = 0;
    size_t characters = 0;
    bool any = false, previous_sentence = false;
    std::string previous_language;

  public:
    explicit Paragraphs(const RenderOptions& value) : options(value) {}
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
} // namespace
bool render_paragraphs(const TranscriptSource& source, const RenderOptions& options,
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
bool render_text(std::ostream& stream, const RenderOptions& options,
                 const TranscriptSource& source) {
    bool any = render_paragraphs(source, options, [&](const auto& text) { stream << text; });
    stream << '\n';
    return any;
}
} // namespace wt::render_detail
