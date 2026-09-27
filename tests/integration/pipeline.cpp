#include "transcript/pipeline.hpp"
#include "support/fixtures.hpp"
#include "support/io.hpp"
#include "support/test.hpp"
#include "transcript/journal.hpp"

using namespace wt;
using namespace wt::test;
namespace {
void named_window_events(const fs::path& root) {
    Fixture f(root);
    Journal journal(f.job, f.options, f.fingerprint());
    Audio audio;
    int started = 0, committed = 0;
    run_chunks(
        journal, 16, [&](size_t size) { return audio.read(size); },
        [&](const auto& pcm) {
            require(started == committed + 1, "recognition must follow its start event");
            return audio.recognize(pcm);
        },
        [](const auto& pcm) { return pcm.size(); },
        {[&](int64_t samples, size_t size) {
             require(samples == journal.samples() && size > 0 && size <= 16);
             ++started;
         },
         [&](int64_t samples) {
             require(samples == journal.samples() && samples > 0);
             ++committed;
             require(committed == started);
         }},
        0);
    require(journal.finished() && started == 3 && committed == 3);
}
void bounded_windows_and_byte_identical_resume(const fs::path& root) {
    Fixture f(root / "roundtrip");
    auto fingerprint = f.fingerprint();
    {
        Journal journal(f.job, f.options, fingerprint);
        Audio audio;
        run(f, journal, audio);
        require(audio.recognized == 43 && audio.peak <= 16);
        require(!has_checkpoint(f.job));
    }
    std::map<std::string, std::string> expected;
    for (const auto& [format, path] : f.job.outputs) {
        expected[format] = read_text(path);
        fs::remove(path);
    }
    auto data = Json::parse(expected.at("json"));
    require(data.at("segments").size() == 9 && data.at("duration") == 35 / 16000.0);
    require(data.at("text").get<std::string>() + "\n" == expected.at("text"));
    {
        Journal journal(f.job, f.options, fingerprint);
        Audio audio;
        rejects([&] { run(f, journal, audio, 1); }, "injected inference failure");
        require(journal.samples() == 12);
        rejects([&] { Journal busy(f.job, f.options, fingerprint); },
                "Checkpoint is busy or lock is unsafe");
    }
    rejects([&] { Journal needs_flag(f.job, f.options, fingerprint); }, "Saved progress exists");
    f.options.resume = true;
    {
        Journal journal(f.job, f.options, fingerprint);
        Audio audio;
        run(f, journal, audio);
        require(audio.cursor == 35 && audio.recognized == 27);
    }
    for (const auto& [format, path] : f.job.outputs)
        require(read_text(path) == expected.at(format), "resumed output changed");
}
} // namespace
int main() {
    return run_tests({
        {"named recognition and commit events", named_window_events},
        {"bounded windows and byte-identical resume", bounded_windows_and_byte_identical_resume},
    });
}
