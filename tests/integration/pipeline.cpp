#include "transcript/pipeline.hpp"
#include "support/fixtures.hpp"
#include "support/io.hpp"
#include "support/test.hpp"
#include "transcript/journal.hpp"
#include "transcript/outputs.hpp"
#include <algorithm>
#include <limits>

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
void digital_silence(const fs::path& root) {
    for (bool no_vad : {false, true}) {
        Fixture f(root / (no_vad ? "plain" : "vad"));
        f.options.no_vad = no_vad;
        auto fingerprint = f.fingerprint();
        auto run_silence = [&](Journal& journal, bool interrupt = false) {
            size_t cursor = 0;
            int commits = 0;
            run_chunks(
                journal, 16,
                [&](size_t count) {
                    count = std::min(count, 35 - cursor);
                    cursor += count;
                    return std::vector<float>(count, -0.0f);
                },
                [](const auto&) -> Transcript { throw std::runtime_error("unexpected inference"); },
                [](const auto&) -> size_t { throw std::runtime_error("unexpected VAD"); },
                {[](int64_t, size_t) { throw std::runtime_error("unexpected recognition event"); },
                 [&](int64_t samples) {
                     require(samples == journal.samples());
                     ++commits;
                     if (interrupt)
                         throw std::runtime_error("interrupted after silent commit");
                 }});
            require(cursor == 35 && journal.samples() == 35 && journal.finished());
            return commits;
        };
        std::string expected;
        {
            Journal journal(f.job, f.options, fingerprint);
            require(run_silence(journal) == 3);
            require(journal.languages().empty());
            journal.visit(
                [](const Segment&) { throw std::runtime_error("unexpected silent text"); });
            rejects([&] { publish_outputs(f.job, f.options, journal); }, "No transcript produced");
            expected = read_text(checkpoint_path(f.job) / "manifest.json");
        }
        f.options.overwrite = true;
        {
            Journal journal(f.job, f.options, fingerprint);
            rejects([&] { run_silence(journal, true); }, "interrupted after silent commit");
            require(journal.samples() == 16 && !journal.finished());
        }
        f.options.overwrite = false;
        f.options.resume = true;
        {
            Journal journal(f.job, f.options, fingerprint);
            require(run_silence(journal) == 2);
            rejects([&] { publish_outputs(f.job, f.options, journal); }, "No transcript produced");
            require(read_text(checkpoint_path(f.job) / "manifest.json") == expected,
                    "silent resume changed the completed journal");
        }
        for (const auto& [format, path] : f.job.outputs)
            require(!fs::exists(path), "empty transcript must not be published");
    }
}
void silence_then_speech_resume(const fs::path& root) {
    Fixture f(root);
    auto fingerprint = f.fingerprint();
    auto transcribe = [&](Journal& journal, bool interrupt = false) {
        size_t cursor = 0;
        int recognized = 0;
        run_chunks(
            journal, 16,
            [&](size_t count) {
                count = std::min(count, 48 - cursor);
                std::vector<float> pcm(count);
                for (size_t i = 0; i < count; ++i)
                    pcm[i] = cursor + i < 32 ? 0 : 0.1f;
                cursor += count;
                return pcm;
            },
            [&](const auto& pcm) {
                ++recognized;
                double duration = pcm.size() / double(sample_rate);
                return Transcript{"en", duration, {{0, duration, "speech", 0}}};
            },
            [](const auto& pcm) { return pcm.size(); },
            {{},
             [&](int64_t samples) {
                 if (interrupt && samples == 16)
                     throw std::runtime_error("interrupted during leading silence");
             }},
            0);
        require(recognized == 1 && journal.samples() == 48);
        publish_outputs(f.job, f.options, journal);
    };
    {
        Journal journal(f.job, f.options, fingerprint);
        transcribe(journal);
    }
    std::map<std::string, std::string> expected;
    for (const auto& [format, path] : f.job.outputs) {
        expected[format] = read_text(path);
        fs::remove(path);
    }
    {
        Journal journal(f.job, f.options, fingerprint);
        rejects([&] { transcribe(journal, true); }, "interrupted during leading silence");
    }
    f.options.resume = true;
    {
        Journal journal(f.job, f.options, fingerprint);
        transcribe(journal);
    }
    for (const auto& [format, path] : f.job.outputs)
        require(read_text(path) == expected.at(format), "silent prefix changed resumed output");
}
void nonzero_windows(const fs::path& root) {
    int index = 0;
    for (float sample : {1e-12f, std::numeric_limits<float>::denorm_min(),
                         std::numeric_limits<float>::quiet_NaN()}) {
        Fixture f(root / std::to_string(index++));
        Journal journal(f.job, f.options, f.fingerprint());
        bool read = false;
        int recognized = 0;
        run_chunks(
            journal, 16,
            [&](size_t) {
                if (read)
                    return std::vector<float>{};
                read = true;
                std::vector<float> pcm(15, 0);
                pcm[7] = sample;
                return pcm;
            },
            [&](const auto& pcm) {
                ++recognized;
                return Transcript{"", pcm.size() / double(sample_rate), {}};
            },
            [](const auto&) -> size_t { throw std::runtime_error("unexpected EOF cut"); });
        require(recognized == 1 && journal.samples() == 15);
    }
}
} // namespace
int main() {
    return run_tests({
        {"named recognition and commit events", named_window_events},
        {"bounded windows and byte-identical resume", bounded_windows_and_byte_identical_resume},
        {"digital silence bypasses inference and VAD with resumable commits", digital_silence},
        {"silence followed by speech resumes to byte-identical outputs",
         silence_then_speech_resume},
        {"quiet speech and invalid samples are not classified as digital silence", nonzero_windows},
    });
}
