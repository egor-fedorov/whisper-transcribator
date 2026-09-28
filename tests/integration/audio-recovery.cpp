#include "audio/reader.hpp"
#include "audio/runtime.hpp"
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
    AudioReader reader(f.job.source, -1, TimestampGaps::automatic, f.options.audio.errors);
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
    publish_outputs(f.job, f.options.rendering, journal, f.options.checkpoint.overwrite);
    return recognized;
}
void resume(const fs::path& root, const fs::path& source, int interrupt) {
    Fixture f(root);
    f.job.source = source;
    f.options.inference.no_vad = true;
    auto fingerprint = f.fingerprint();
    int64_t total;
    {
        Journal journal(f.job, f.options.checkpoint, fingerprint, describe_output(f.options));
        total = transcribe(f, journal);
        require(total > int64_t(interrupt) * sample_rate && !has_checkpoint(f.job));
    }
    std::map<std::string, std::string> expected;
    for (const auto& [format, path] : f.job.outputs) {
        expected[format] = read_text(path);
        fs::remove(path);
    }
    {
        Journal journal(f.job, f.options.checkpoint, fingerprint, describe_output(f.options));
        rejects([&] { transcribe(f, journal, interrupt); }, "injected interruption");
        require(journal.samples() == int64_t(interrupt) * sample_rate);
    }
    f.options.checkpoint.resume = true;
    {
        Journal journal(f.job, f.options.checkpoint, fingerprint, describe_output(f.options));
        require(transcribe(f, journal) == total - int64_t(interrupt) * sample_rate);
    }
    for (const auto& [format, path] : f.job.outputs)
        require(read_text(path) == expected.at(format), "damaged audio changed resumed output");
}
void failure(const fs::path& root, const fs::path& source, const std::string& mode) {
    Fixture f(root);
    f.job.source = source;
    if (mode == "strict")
        f.options.audio.errors.strict = true;
    else
        f.options.audio.errors.limit_seconds = std::stoi(mode);
    auto reason = mode == "strict" ? "--decode-errors tolerant" : "--decode-error-limit-seconds";
    auto fingerprint = f.fingerprint();
    std::string saved;
    {
        Journal journal(f.job, f.options.checkpoint, fingerprint, describe_output(f.options));
        rejects([&] { transcribe(f, journal); }, reason);
        require(journal.samples() > 0 && !journal.finished());
        saved = read_text(checkpoint_path(f.job) / "manifest.json");
    }
    for (const auto& [format, path] : f.job.outputs)
        require(!fs::exists(path), "severely damaged audio published a transcript");
    f.options.checkpoint.resume = true;
    {
        Journal journal(f.job, f.options.checkpoint, fingerprint, describe_output(f.options));
        rejects([&] { transcribe(f, journal); }, "Repeating --resume");
    }
    require(read_text(checkpoint_path(f.job) / "manifest.json") == saved);
    f.options.audio.errors.strict = false;
    f.options.audio.errors.limit_seconds = 0;
    rejects(
        [&] {
            Journal journal(f.job, f.options.checkpoint, f.fingerprint(),
                            describe_output(f.options));
        },
        "Checkpoint is incompatible");
    f.options.checkpoint.resume = false;
    f.options.checkpoint.overwrite = true;
    Journal journal(f.job, f.options.checkpoint, f.fingerprint(), describe_output(f.options));
    require(transcribe(f, journal) > 0 && !has_checkpoint(f.job));
    for (const auto& [format, path] : f.job.outputs)
        require(fs::file_size(path) > 0);
}
void damage(const fs::path& source, const fs::path& output, size_t amount, bool transport) {
    auto data = read_text(source);
    if (transport) {
        require(data.size() % 188 == 0 && amount > 0 && amount < data.size() / 376);
        // Fixed mux rate in the shell fixture makes 125 transport packets one second.
        auto start = data.size() / 2 / 188 * 188;
        data.erase(start, amount * 188);
    } else {
        // Synthetic, small MP4 only: leave all box and sample-table metadata intact.
        size_t start = 0;
        for (;;) {
            require(start + 8 <= data.size(), "mdat missing from M4A fixture");
            uint32_t size = 0;
            for (size_t i = 0; i < 4; ++i)
                size = (size << 8) | static_cast<unsigned char>(data[start + i]);
            require(size >= 8 && size <= data.size() - start);
            if (data.compare(start + 4, 4, "mdat") == 0) {
                auto offset = (start + size / 2) / 4096 * 4096;
                require(amount > 0 && offset >= start + 8 && amount <= start + size - offset);
                data.replace(offset, amount, amount, '\0');
                break;
            }
            start += size;
        }
    }
    atomic_write(output, data);
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 4 && argc != 5)
        return 2;
    configure_audio_logging();
    auto source = fs::canonical(argv[2]);
    auto mode = std::string(argv[1]);
    if (argc == 5 && (mode == "--zero-m4a" || mode == "--drop-ts")) {
        damage(source, argv[3], std::stoull(argv[4]), mode == "--drop-ts");
        return 0;
    }
    if (mode == "--failure" && argc == 4)
        return run_tests({{"decode failure preserves progress and allows an explicit restart",
                           [&](const fs::path& root) { failure(root, source, argv[3]); }}});
    if (std::string(argv[1]) != "--resume" || argc != 4)
        return 2;
    return run_tests({{"damaged audio resumes to byte-identical PCM-based transcripts",
                       [&](const fs::path& root) { resume(root, source, std::stoi(argv[3])); }}});
}
