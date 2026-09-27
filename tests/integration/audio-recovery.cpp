#include "audio/audio.hpp"
#include "support/fixtures.hpp"
#include "support/hash.hpp"
#include "support/io.hpp"
#include "support/test.hpp"
#include "transcript/journal.hpp"
#include "transcript/outputs.hpp"
#include "transcript/pipeline.hpp"
#include <iostream>

using namespace wt;
using namespace wt::test;
namespace {
int64_t transcribe(Fixture& f, Journal& journal, int interrupt = 0) {
    AudioReader reader(f.job.source);
    int64_t recognized = 0;
    run_chunks(
        journal, sample_rate, [&](size_t count) { return reader.read(count); },
        [&](const auto& pcm) {
            recognized += static_cast<int64_t>(pcm.size());
            double duration = pcm.size() / double(sample_rate);
            auto hash = sha256_text(
                std::string(reinterpret_cast<const char*>(pcm.data()), pcm.size() * sizeof(float)));
            return Transcript{"en", duration, {{0, duration, hash, 0}}};
        },
        [](const auto& pcm) { return pcm.size(); },
        {{},
         [&](int64_t samples) {
             if (interrupt && samples >= int64_t(interrupt) * sample_rate)
                 throw std::runtime_error("injected interruption");
         }},
        0);
    publish_outputs(f.job, f.options, journal);
    return recognized;
}
void resume(const fs::path& root, const fs::path& source, int interrupt) {
    Fixture f(root);
    f.job.source = source;
    f.options.no_vad = true;
    auto fingerprint = f.fingerprint();
    int64_t total;
    {
        Journal journal(f.job, f.options, fingerprint);
        total = transcribe(f, journal);
        require(total > int64_t(interrupt) * sample_rate && !has_checkpoint(f.job));
    }
    std::map<std::string, std::string> expected;
    for (const auto& [format, path] : f.job.outputs) {
        expected[format] = read_text(path);
        fs::remove(path);
    }
    {
        Journal journal(f.job, f.options, fingerprint);
        rejects([&] { transcribe(f, journal, interrupt); }, "injected interruption");
        require(journal.samples() == int64_t(interrupt) * sample_rate);
    }
    f.options.resume = true;
    {
        Journal journal(f.job, f.options, fingerprint);
        require(transcribe(f, journal) == total - int64_t(interrupt) * sample_rate);
    }
    for (const auto& [format, path] : f.job.outputs)
        require(read_text(path) == expected.at(format), "damaged audio changed resumed output");
}
void severe(const fs::path& root, const fs::path& source) {
    Fixture f(root);
    f.job.source = source;
    auto fingerprint = f.fingerprint();
    std::string saved;
    {
        Journal journal(f.job, f.options, fingerprint);
        rejects([&] { transcribe(f, journal); }, "Too many");
        require(journal.samples() > 0 && !journal.finished());
        saved = read_text(checkpoint_path(f.job) / "manifest.json");
    }
    for (const auto& [format, path] : f.job.outputs)
        require(!fs::exists(path), "severely damaged audio published a transcript");
    f.options.resume = true;
    Journal journal(f.job, f.options, fingerprint);
    rejects([&] { transcribe(f, journal); }, "Too many");
    require(read_text(checkpoint_path(f.job) / "manifest.json") == saved);
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 3 && argc != 4)
        return 2;
    configure_audio_logging();
    auto source = fs::canonical(argv[2]);
    if (std::string(argv[1]) == "--severe" && argc == 3)
        return run_tests({{"severe corruption fails without losing committed progress",
                           [&](const fs::path& root) { severe(root, source); }}});
    if (std::string(argv[1]) != "--resume" || argc != 4)
        return 2;
    return run_tests({{"damaged audio resumes to byte-identical PCM-based transcripts",
                       [&](const fs::path& root) { resume(root, source, std::stoi(argv[3])); }}});
}
