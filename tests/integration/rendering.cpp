#include "support/fixtures.hpp"
#include "support/io.hpp"
#include "support/test.hpp"
#include "transcript/jobs.hpp"
#include "transcript/journal.hpp"
#include "transcript/metadata.hpp"
#include "transcript/outputs.hpp"
#include <limits>

using namespace wt;
using namespace wt::test;
namespace {
void unicode_filenames_and_transcript(const fs::path& root) {

    auto o = input(root, u8"\u041b\u0435\u043a\u0446\u0438\u044f (1).mp4");
    auto job = prepare_jobs(o)[0];
    write_outputs(job, o, {"ru", 1, {{0, 1, u8"\u0422\u0435\u043a\u0441\u0442.", 0}}});
    require(read_text(job.outputs.at("text")) == u8"\u0422\u0435\u043a\u0441\u0442.\n");
}
void render_outputs_once(const fs::path& root) {

    auto o = input(root);
    o.output_dir = root.string();
    o.format = "all";
    auto job = prepare_jobs(o)[0];
    Transcript result{"en", 2, {{0, 1.234, " Hello.", 0.1}, {1.234, 2, " World! ", 0.2}}};
    write_outputs(job, o, result);
    require(read_text(root / "a.txt") == "Hello. World!\n");
    require(read_text(root / "a.srt") ==
            "1\n00:00:00,000 --> 00:00:01,234\nHello.\n\n2\n00:00:01,234 --> "
            "00:00:02,000\nWorld!\n\n");
    auto data = Json::parse(read_text(root / "a.json"));
    require(data["schema_version"] == 2 && data["segments"].size() == 2);
    require(data["text"] == "Hello. World!" && data["run"]["backend"] == "whisper.cpp");
}
void empty_transcript_never_publishes_output(const fs::path& root) {

    auto o = input(root);
    auto job = prepare_jobs(o)[0];
    rejects([&] { write_outputs(job, o, {"en", 1, {{0, 1, "  ", 0}}}); });
    require(!fs::exists(root / "a.txt"));
}
void invalid_timestamps(const fs::path& root) {

    auto o = input(root);
    auto job = prepare_jobs(o)[0];
    rejects([&] { write_outputs(job, o, {"en", 2, {{2, 1, "bad", 0}}}); });
    rejects([&] { timestamp(std::numeric_limits<double>::infinity()); });
    require(timestamp(3661.234) == "01:01:01,234");
}
void silence_and_per_window_language(const fs::path& root) {
    Fixture silent(root / "silent");
    {
        Journal journal(silent.job, silent.options, silent.fingerprint());
        journal.append(16, {"", 0.001, {}});
        require(journal.language().empty());
        journal.append(16, {"en", 0.001, {{0, 0.001, "Hello", 0}}});
        require(journal.language() == "en");
        journal.append(16, {"ru", 0.001, {{0, 0.001, "Other language", 0}}});
        require(journal.language().empty() &&
                journal.languages() == std::vector<std::string>({"en", "ru"}));
        journal.append(16, {"", 0.001, {}});
        journal.finish();
        publish_outputs(silent.job, silent.options, journal);
        require(read_text(silent.job.outputs.at("text")) == "Hello\n\nOther language\n");
        auto mixed = Json::parse(read_text(silent.job.outputs.at("json")));
        require(mixed["language"].is_null() && mixed["segments"][1]["language"] == "ru");
    }
    Fixture empty(root / "empty");
    {
        Journal journal(empty.job, empty.options, empty.fingerprint());
        journal.append(16, {"", 0.001, {}});
        journal.finish();
        rejects([&] { publish_outputs(empty.job, empty.options, journal); });
        require(!fs::exists(empty.job.outputs.at("json")));
    }
}
void paragraphs_and_vtt(const fs::path& root) {
    Fixture f(root);
    auto& options = f.options;
    auto& job = f.job;
    require(job.outputs.size() == 4);
    auto fingerprint = f.fingerprint();
    Journal journal(job, options, fingerprint);
    journal.append(5 * sample_rate, {"en", 5, {{0, 5, "First\nline.", 0}}});
    journal.append(10 * sample_rate,
                   {"en", 10, {{0, 1, "Same paragraph.", 0}, {4, 4, "New & <tag> --> cue.", 0}}});
    journal.append(sample_rate, {"ru", 1, {{0, 1, u8"\u041f\u0440\u0438\u0432\u0435\u0442.", 0}}});
    journal.finish();
    publish_outputs(job, options, journal);
    auto text = read_text(job.outputs.at("text"));
    require(text == u8"First line. Same paragraph.\n\nNew & <tag> --> "
                    u8"cue.\n\n\u041f\u0440\u0438\u0432\u0435\u0442.\n");
    auto json = Json::parse(read_text(job.outputs.at("json")));
    require(json["text"].get<std::string>() + "\n" == text && json["language"].is_null());
    require(json["segments"][0]["text"] == "First\nline.");
    auto vtt = read_text(job.outputs.at("vtt"));
    require(vtt.rfind("WEBVTT\n\n", 0) == 0);
    require(vtt.find("00:00:09.000 --> 00:00:09.001") != std::string::npos);
    require(vtt.find("New &amp; &lt;tag&gt; --&gt; cue.") != std::string::npos);
}
void single_line(const fs::path& root) {
    Fixture f(root);
    auto& options = f.options;
    auto& job = f.job;
    options.text_layout = "single-line";
    {
        Journal journal(job, options, job_fingerprint(job, options, Json::object()));
        journal.append(10 * sample_rate, {"en", 10, {{0, 1, "One.", 0}, {8, 9, "Two.", 0}}});
        journal.finish();
        publish_outputs(job, options, journal);
    }
    require(read_text(job.outputs.at("text")) == "One. Two.\n");
}
void unicode_paragraph_limit(const fs::path& root) {
    Fixture f(root);
    auto& options = f.options;
    auto& job = f.job;
    options.text_layout = "paragraphs";
    {
        Journal journal(job, options, job_fingerprint(job, options, Json::object()));
        std::string unicode;
        for (int i = 0; i < 599; ++i)
            unicode += u8"\u044f";
        journal.append(3 * sample_rate, {"ru", 3, {{0, 1, unicode + ".", 0}, {1, 2, "Next.", 0}}});
        journal.finish();
        publish_outputs(job, options, journal);
        require(read_text(job.outputs.at("text")) == unicode + ".\n\nNext.\n");
    }
}
} // namespace
int main() {
    return run_tests({
        {"paragraphs and WebVTT escaping", paragraphs_and_vtt},
        {"single-line output", single_line},
        {"Unicode paragraph length", unicode_paragraph_limit},
        {"unicode filenames and transcript", unicode_filenames_and_transcript},
        {"render outputs once", render_outputs_once},
        {"empty transcript never publishes output", empty_transcript_never_publishes_output},
        {"invalid timestamps", invalid_timestamps},
        {"silence and per-window language", silence_and_per_window_language},
    });
}
