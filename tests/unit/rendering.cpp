#include "support/json.hpp"
#include "support/options.hpp"
#include "support/test.hpp"
#include "transcript/outputs.hpp"
#include <algorithm>
#include <sstream>

using namespace wt;
using namespace wt::test;
namespace {
std::string render(const std::string& format, const std::vector<Segment>& segments,
                   const Options& options = {}) {
    TranscriptSource source{20 * sample_rate, {}, [&](const SegmentConsumer& consume) {
                                for (const auto& segment : segments)
                                    consume(segment);
                            }};
    for (const auto& segment : segments)
        if (std::find(source.languages.begin(), source.languages.end(), segment.language) ==
            source.languages.end())
            source.languages.push_back(segment.language);
    std::ostringstream output;
    render_stream(output, format, {"/fixture/source.wav", {}}, options, source);
    return output.str();
}
void paragraphs_and_vtt(const fs::path&) {
    std::vector<Segment> segments = {{0, 5, "First\nline.", 0, "en"},
                                     {5, 6, "Same paragraph.", 0, "en"},
                                     {9, 9, "New & <tag> --> cue.", 0, "en"},
                                     {15, 16, u8"\u041f\u0440\u0438\u0432\u0435\u0442.", 0, "ru"}};
    auto text = render("text", segments);
    require(text == u8"First line. Same paragraph.\n\nNew & <tag> --> "
                    u8"cue.\n\n\u041f\u0440\u0438\u0432\u0435\u0442.\n");
    auto json = Json::parse(render("json", segments));
    require(json["text"].get<std::string>() + "\n" == text && json["language"].is_null());
    require(json["segments"][0]["text"] == "First\nline.");
    auto vtt = render("vtt", segments);
    require(vtt.rfind("WEBVTT\n\n", 0) == 0);
    require(vtt.find("00:00:09.000 --> 00:00:09.001") != std::string::npos);
    require(vtt.find("New &amp; &lt;tag&gt; --&gt; cue.") != std::string::npos);
}
void single_line(const fs::path&) {
    Options options;
    options.text_layout = "single-line";
    std::vector<Segment> segments = {{0, 1, "One.", 0, "en"}, {8, 9, "Two.", 0, "en"}};
    require(render("text", segments, options) == "One. Two.\n");
    require(Json::parse(render("json", segments, options))["text"] == "One. Two.");
}
void unicode_paragraph_limit(const fs::path&) {
    std::string unicode;
    for (int i = 0; i < 599; ++i)
        unicode += u8"\u044f";
    std::vector<Segment> segments = {{0, 1, unicode + ".", 0, "ru"}, {1, 2, "Next.", 0, "ru"}};
    require(render("text", segments) == unicode + ".\n\nNext.\n");
}
void whitespace_and_escaping(const fs::path&) {
    std::vector<Segment> segments = {{0, 1, " \n\t", 0, "en"},
                                     {1, 2, " quote \" and \\ line\r\n", 0, "en"}};
    auto text = render("text", segments);
    require(text == "quote \" and \\ line\n");
    auto json = Json::parse(render("json", segments));
    require(json["text"].get<std::string>() + "\n" == text);
    require(json["segments"].size() == 2 && json["segments"][1]["text"] == segments[1].text);
    require(json["language"] == "en");
    rejects([&] { render("text", {}); }, "No transcript produced");
}
void repeated_streaming_visit(const fs::path&) {
    int visits = 0;
    TranscriptSource source{sample_rate, {"en"}, [&](const SegmentConsumer& consume) {
                                ++visits;
                                consume({0, 1, "Streamed.", 0, "en"});
                            }};
    std::ostringstream output;
    render_stream(output, "json", {"/nonexistent/input.wav", {}}, Options{}, source);
    require(visits == 2 && Json::parse(output.str())["text"] == "Streamed.");
}
} // namespace
int main() {
    return run_tests(
        {{"paragraphs and WebVTT escaping", paragraphs_and_vtt},
         {"single-line output", single_line},
         {"Unicode paragraph length", unicode_paragraph_limit},
         {"whitespace and JSON escaping", whitespace_and_escaping},
         {"repeatable streaming source without filesystem access", repeated_streaming_visit}});
}
