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
    auto job = prepare_jobs(o.jobs, o.checkpoint)[0];
    write_outputs(job, o, {"ru", 1, {{0, 1, u8"\u0422\u0435\u043a\u0441\u0442.", 0}}});
    require(read_text(job.outputs.at("text")) == u8"\u0422\u0435\u043a\u0441\u0442.\n");
}
void render_outputs_once(const fs::path& root) {

    auto o = input(root);
    o.jobs.output_dir = root.string();
    o.jobs.format = "all";
    auto job = prepare_jobs(o.jobs, o.checkpoint)[0];
    Transcript result{"en", 2, {{0, 1.234, " Hello.", 0.1}, {1.234, 2, " World! ", 0.2}}};
    write_outputs(job, o, result);
    require(read_text(root / "a.txt") == "Hello. World!\n");
    require(read_text(root / "a.srt") ==
            "1\n00:00:00,000 --> 00:00:01,234\nHello.\n\n2\n00:00:01,234 --> "
            "00:00:02,000\nWorld!\n\n");
    auto data = Json::parse(read_text(root / "a.json"));
    require(read_text(root / "a.vtt") ==
            "WEBVTT\n\n1\n00:00:00.000 --> 00:00:01.234\nHello.\n\n2\n00:00:01.234 --> "
            "00:00:02.000\nWorld!\n\n");
    require(data["schema_version"] == 2 && data["segments"].size() == 2);
    require(data["text"] == "Hello. World!" && data["run"]["backend"] == "whisper.cpp");
    require(data["model"] == "small" && data["language"] == "en" && data["duration"] == 2.0 &&
            data["duration_after_vad"].is_null());
    require(data["segments"][0] == Json({{"id", 0},
                                         {"start", 0.0},
                                         {"end", 1.234},
                                         {"text", "Hello."},
                                         {"language", "en"},
                                         {"avg_logprob", nullptr},
                                         {"compression_ratio", nullptr},
                                         {"no_speech_prob", 0.1}}));
}
void empty_transcript_never_publishes_output(const fs::path& root) {

    auto o = input(root);
    auto job = prepare_jobs(o.jobs, o.checkpoint)[0];
    rejects([&] { write_outputs(job, o, {"en", 1, {{0, 1, "  ", 0}}}); }, "No transcript produced");
    require(!fs::exists(root / "a.txt"));
}
void invalid_timestamps(const fs::path& root) {

    auto o = input(root);
    auto job = prepare_jobs(o.jobs, o.checkpoint)[0];
    rejects([&] { write_outputs(job, o, {"en", 2, {{2, 1, "bad", 0}}}); },
            "Invalid checkpoint segment");
    rejects([&] { timestamp(std::numeric_limits<double>::infinity()); }, "Invalid timestamp");
    require(timestamp(3661.234) == "01:01:01,234");
}
void silence_and_per_window_language(const fs::path& root) {
    Fixture silent(root / "silent");
    {
        Journal journal(silent.job, silent.options.checkpoint, silent.fingerprint(),
                        describe_output(silent.options));
        journal.append(16, {"", 0.001, {}});
        require(journal.language().empty());
        journal.append(16, {"en", 0.001, {{0, 0.001, "Hello", 0}}});
        require(journal.language() == "en");
        journal.append(16, {"ru", 0.001, {{0, 0.001, "Other language", 0}}});
        require(journal.language().empty() &&
                journal.languages() == std::vector<std::string>({"en", "ru"}));
        journal.append(16, {"", 0.001, {}});
        journal.finish();
        publish_outputs(silent.job, silent.options.rendering, journal,
                        silent.options.checkpoint.overwrite);
        require(read_text(silent.job.outputs.at("text")) == "Hello\n\nOther language\n");
        auto mixed = Json::parse(read_text(silent.job.outputs.at("json")));
        require(mixed["language"].is_null() && mixed["segments"][1]["language"] == "ru");
    }
    Fixture empty(root / "empty");
    {
        Journal journal(empty.job, empty.options.checkpoint, empty.fingerprint(),
                        describe_output(empty.options));
        journal.append(16, {"", 0.001, {}});
        journal.finish();
        rejects(
            [&] {
                publish_outputs(empty.job, empty.options.rendering, journal,
                                empty.options.checkpoint.overwrite);
            },
            "No transcript produced");
        require(!fs::exists(empty.job.outputs.at("json")));
    }
}
} // namespace
int main() {
    return run_tests({
        {"unicode filenames and transcript", unicode_filenames_and_transcript},
        {"render outputs once", render_outputs_once},
        {"empty transcript never publishes output", empty_transcript_never_publishes_output},
        {"invalid timestamps", invalid_timestamps},
        {"silence and per-window language", silence_and_per_window_language},
    });
}
