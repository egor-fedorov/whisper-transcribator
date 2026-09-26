#include "pipeline.hpp"
#include <algorithm>
#include <iostream>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace wt;
namespace {
void little(std::ostream& out, uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i)
        out.put(static_cast<char>((value >> (i * 8)) & 255));
}
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
        for (int i = 0; i < seconds; ++i)
            out << block;
    });
    Options options;
    options.inputs = {path.string()};
    options.output_dir = root.string();
    options.format = "all";
    auto job = prepare_jobs(options).at(0);
    Journal journal(job, options, job_fingerprint(job, options, Json::object()));
    AudioReader reader(path);
    int64_t processed = 0;
    run_chunks(
        journal, size_t(options.chunk_seconds) * sample_rate,
        [&](size_t n) { return reader.read(n); },
        [&](const auto& pcm, const auto&) {
            if (pcm.size() > size_t(options.chunk_seconds) * sample_rate)
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
    journal.publish(job, options);
    fs::remove_all(root);
}
long measure(const fs::path& root, int seconds) {
    auto child = fork();
    if (child < 0)
        throw std::runtime_error("fork failed");
    if (!child) {
        try {
            scenario(root, seconds);
            _exit(0);
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            _exit(1);
        }
    }
    int status = 0;
    rusage usage{};
    if (wait4(child, &status, 0, &usage) != child || !WIFEXITED(status) || WEXITSTATUS(status))
        throw std::runtime_error("Memory scenario failed");
    return usage.ru_maxrss;
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    try {
        auto root = fs::absolute(argv[1]);
        fs::create_directories(root);
        auto pattern = (root / "run-XXXXXX").string();
        if (!mkdtemp(pattern.data()))
            throw std::runtime_error("mkdtemp failed");
        root = pattern;
        auto short_rss = measure(root / "short", 120);
        auto long_rss = measure(root / "long", 3600);
        std::cout << "Synthetic decoding + fake inference + TXT/SRT/VTT/JSON: 120s RSS "
                  << short_rss << " KiB; 3600s RSS " << long_rss << " KiB\n";
        if (long_rss > short_rss + 32 * 1024)
            throw std::runtime_error("Memory grew by more than 32 MiB with recording length");
        fs::remove_all(root);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
