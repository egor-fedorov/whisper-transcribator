#include "app/configuration.hpp"
#include "audio/reader.hpp"
#include "platform/system.hpp"
#include "support/io.hpp"
#include "support/process.hpp"
#include "support/test.hpp"
#include "support/wav.hpp"
#include "transcript/jobs.hpp"
#include "transcript/journal.hpp"
#include "transcript/metadata.hpp"
#include "transcript/outputs.hpp"
#include "transcript/pipeline.hpp"
#include <algorithm>
#include <iostream>

using namespace wt;
using namespace wt::test;
namespace {
void scenario(const fs::path& root, int seconds) {
    fs::create_directories(root);
    auto path = root / "synthetic.wav";
    atomic_write_stream(path, [&](auto& out) {
        uint32_t bytes = uint32_t(seconds) * sample_rate * 2;
        out << "RIFF";
        little(out, 36 + bytes, 4);
        out << "WAVEfmt ";
        little(out, 16, 4);
        little(out, 1, 2);
        little(out, 1, 2);
        little(out, sample_rate, 4);
        little(out, sample_rate * 2, 4);
        little(out, 2, 2);
        little(out, 16, 2);
        out << "data";
        little(out, bytes, 4);
        std::string block(sample_rate * 2, '\0');
        // Keep inference and rendering active instead of taking the digital-silence shortcut.
        for (size_t i = 0; i < block.size(); i += 2)
            block[i] = 1;
        for (int i = 0; i < seconds; ++i)
            out << block;
    });
    CliOptions options;
    options.jobs.inputs = {path.string()};
    options.jobs.output_dir = root.string();
    options.jobs.format = "all";
    auto job = prepare_jobs(options.jobs, options.checkpoint).at(0);
    Journal journal(
        job, options.checkpoint,
        job_fingerprint(job, describe_run(options), options.inference.language, Json::object()),
        describe_output(options));
    AudioReader reader(path);
    int64_t processed = 0;
    run_chunks(
        journal, size_t(options.chunking.chunk_seconds) * sample_rate,
        [&](size_t n) { return reader.read(n); },
        [&](const auto& pcm) {
            if (pcm.size() > size_t(options.chunking.chunk_seconds) * sample_rate)
                throw std::runtime_error("Unbounded PCM window");
            processed += static_cast<int64_t>(pcm.size());
            double duration = pcm.size() / double(sample_rate);
            Transcript result{"en", duration, {}};
            // Deliberately large fake text also exercises bounded final rendering.
            for (int i = 0; i < 1024; ++i)
                result.segments.push_back(
                    {duration * i / 1024, duration * (i + 1) / 1024, std::string(1024, 'x'), 0});
            return result;
        },
        [](const auto& pcm) { return pcm.size(); });
    auto expected = int64_t(seconds) * sample_rate;
    if (journal.samples() != expected || processed < expected || processed > expected * 2)
        throw std::runtime_error("Lost committed samples or excessive tail reprocessing");
    publish_outputs(job, options.rendering, journal, options.checkpoint.overwrite);
}
int64_t measure(const fs::path& root, int seconds) {
    auto metrics = root.parent_path() / (root.filename().string() + "-rss");
    auto log = root.parent_path() / (root.filename().string() + "-log");
    Process child(platform::executable_path(),
                  {"--measure", root.u8string(), std::to_string(seconds), metrics.u8string()}, log);
    auto status = child.wait(150);
    require(status == 0,
            "Memory scenario failed (" + std::to_string(status) + "): " + read_text(log));
    return std::stoll(read_text(metrics));
}
} // namespace
int main(int argc, char** argv) {
    if (argc == 5 && std::string(argv[1]) == "--measure") {
        try {
            auto root = fs::u8path(argv[2]);
            scenario(root, std::stoi(argv[3]));
            // Windows cannot unlink the WAV until AudioReader has released its handle.
            fs::remove_all(root);
            atomic_write(fs::u8path(argv[4]), std::to_string(peak_rss_kib()));
            return 0;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    return run_tests(
        {{"bounded RSS with synthetic audio and fake inference", [](const fs::path& root) {
              auto short_rss = measure(root / "short", 120);
              auto long_rss = measure(root / "long", 3600);
              std::cout << "Synthetic decoding + fake inference + TXT/SRT/VTT/JSON: 120s RSS "
                        << short_rss << " KiB; 3600s RSS " << long_rss << " KiB\n";
              if (long_rss > short_rss + 32 * 1024)
                  throw std::runtime_error("Memory grew by more than 32 MiB with recording length");
          }}});
}
