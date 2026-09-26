#include "transcript/pipeline.hpp"
#include "support/cancel.hpp"
#include "support/io.hpp"
#include "support/options.hpp"
#include "transcript/jobs.hpp"
#include "transcript/journal.hpp"
#include "transcript/metadata.hpp"
#include "transcript/outputs.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace wt;
namespace {
void require(bool condition, const std::string& message = "assertion failed") {
    if (!condition)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "expected failure");
}
struct Fixture {
    fs::path root;
    Options options;
    Job job;
    explicit Fixture(const fs::path& directory) : root(directory) {
        fs::create_directories(root);
        atomic_write(root / "source.wav", "fake audio");
        options.inputs = {(root / "source.wav").string()};
        options.output_dir = root.string();
        options.format = "all";
        options.language = "auto";
        options.chunk_seconds = 30;
        job = prepare_jobs(options).at(0);
    }
    Json fingerprint() { return job_fingerprint(job, options, {{"model_sha256", "fixture"}}); }
};
struct Audio {
    int64_t total = 35, cursor = 0, recognized = 0;
    size_t peak = 0;
    std::vector<float> read(size_t limit) {
        auto count = static_cast<size_t>(std::min<int64_t>(limit, total - cursor));
        std::vector<float> result(count);
        for (auto& sample : result)
            sample = static_cast<float>(cursor++);
        return result;
    }
    Transcript recognize(const std::vector<float>& pcm, const std::string& language) {
        require(!pcm.empty());
        for (size_t i = 1; i < pcm.size(); ++i)
            require(pcm[i] == pcm[i - 1] + 1, "non-contiguous PCM");
        peak = std::max(peak, pcm.size());
        recognized += static_cast<int64_t>(pcm.size());
        require(language.empty(), "auto language must not be locked to a previous window");
        double duration = pcm.size() / double(sample_rate);
        Transcript result{"en", duration, {}};
        for (size_t i = 0; i < pcm.size(); i += 4)
            result.segments.push_back({i / double(sample_rate),
                                       std::min(i + 4, pcm.size()) / double(sample_rate),
                                       "chunk \"" + std::to_string(int(pcm[i])) + "\" \\", 0.1});
        return result;
    }
};
void run(Fixture& f, Journal& journal, Audio& audio, int fail_at = -1) {
    int calls = 0;
    run_chunks(
        journal, 16, [&](size_t n) { return audio.read(n); },
        [&](const auto& pcm, const auto& language) {
            if (calls++ == fail_at)
                throw std::runtime_error("injected inference failure");
            return audio.recognize(pcm, language);
        },
        [](const auto& pcm) { return pcm.size() - 4; }, {}, 4);
    publish_outputs(f.job, f.options, journal);
}
void signal_test(Fixture& f, int signal) {
    int ready[2];
    require(pipe(ready) == 0);
    auto child = fork();
    require(child >= 0);
    if (child == 0) {
        close(ready[0]);
        struct sigaction action {};
        action.sa_handler = [](int value) { stop_signal = value; };
        sigemptyset(&action.sa_mask);
        sigaction(SIGTERM, &action, nullptr);
        sigaction(SIGINT, &action, nullptr);
        try {
            Journal journal(f.job, f.options, f.fingerprint());
            Audio audio;
            int calls = 0;
            run_chunks(
                journal, 16, [&](size_t n) { return audio.read(n); },
                [&](const auto& pcm, const auto& language) {
                    if (calls++ == 1) {
                        sigset_t blocked, previous;
                        sigemptyset(&blocked);
                        sigaddset(&blocked, SIGINT);
                        sigaddset(&blocked, SIGTERM);
                        sigprocmask(SIG_BLOCK, &blocked, &previous);
                        auto waiting = previous;
                        sigdelset(&waiting, SIGINT);
                        sigdelset(&waiting, SIGTERM);
                        char byte = 'x';
                        if (write(ready[1], &byte, 1) != 1)
                            _exit(4);
                        while (!stop_signal)
                            sigsuspend(&waiting);
                        sigprocmask(SIG_SETMASK, &previous, nullptr);
                        check_cancelled();
                    }
                    return audio.recognize(pcm, language);
                },
                [](const auto& pcm) { return pcm.size(); });
        } catch (const Cancelled&) {
            _exit(128 + stop_signal);
        } catch (...) {
            _exit(3);
        }
        _exit(2);
    }
    close(ready[1]);
    char byte = 0;
    require(read(ready[0], &byte, 1) == 1, "worker failed before checkpoint");
    close(ready[0]);
    require(kill(child, signal) == 0);
    int status = 0;
    require(waitpid(child, &status, 0) == child);
    if (signal == SIGKILL)
        require(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    else
        require(WIFEXITED(status) && WEXITSTATUS(status) == 128 + signal);
    require(!fs::exists(f.job.outputs.at("text")));
    f.options.resume = true;
    Journal journal(f.job, f.options, f.fingerprint());
    require(journal.samples() == 16);
    Audio audio;
    run(f, journal, audio);
    require(audio.recognized == 23, "only uncommitted tail may be recognized again");
}
} // namespace
int main(int argc, char** argv) {
    if (argc < 2)
        return 2;
    auto root = fs::absolute(argv[1]);
    fs::create_directories(root);
    auto pattern = (root / "run-XXXXXX").string();
    auto temporary = mkdtemp(pattern.data());
    if (!temporary)
        return 1;
    root = temporary;
    std::string name;
    try {
        name = "early failures leave no saved progress";
        Fixture early(root / "early");
        {
            Journal journal(early.job, early.options, early.fingerprint());
            Audio audio;
            rejects([&] { run(early, journal, audio, 0); });
            require(!has_checkpoint(early.job));
            rejects([&] { Journal busy(early.job, early.options, early.fingerprint()); });
        }
        atomic_write(early.job.source, "replacement input", true);
        {
            Journal journal(early.job, early.options, early.fingerprint());
            require(!has_checkpoint(early.job));
        }
        name = "empty checkpoint recovery and unknown contents";
        Fixture zero(root / "zero");
        auto zero_path = checkpoint_path(zero.job);
        {
            Journal journal(zero.job, zero.options, zero.fingerprint());
            journal.append(16, {"en", 0.001, {{0, 0.001, "orphan", 0}}});
        }
        auto zero_manifest = Json::parse(read_text(zero_path / "manifest.json"));
        zero_manifest["chunks"] = zero_manifest["samples"] = 0;
        zero_manifest["languages"] = Json::array();
        zero_manifest["last_hash"] = "";
        atomic_write(zero_path / "manifest.json", zero_manifest.dump(), true);
        atomic_write(zero_path / "unknown", "preserve");
        rejects([&] { Journal journal(zero.job, zero.options, zero.fingerprint()); });
        require(read_text(zero_path / "unknown") == "preserve");
        fs::remove(zero_path / "unknown");
        atomic_write(zero.job.source, "replacement", true);
        {
            Journal journal(zero.job, zero.options, zero.fingerprint());
            require(!has_checkpoint(zero.job));
        }
        fs::create_directory(zero_path);
        fs::permissions(zero_path, fs::perms::owner_all);
        {
            Journal journal(zero.job, zero.options, zero.fingerprint());
            require(!has_checkpoint(zero.job));
        }
        name = "bounded windows and byte-identical resume";
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
            rejects([&] { run(f, journal, audio, 1); });
            require(journal.samples() == 12);
            rejects([&] { Journal busy(f.job, f.options, fingerprint); });
        }
        rejects([&] { Journal needs_flag(f.job, f.options, fingerprint); });
        f.options.resume = true;
        {
            Journal journal(f.job, f.options, fingerprint);
            Audio audio;
            run(f, journal, audio);
            require(audio.cursor == 35 && audio.recognized == 27);
        }
        for (const auto& [format, path] : f.job.outputs)
            require(read_text(path) == expected.at(format), "resumed output changed");

        name = "silence and per-window language";
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

        name = "pause selection";
        Transcript boundary{"en", 30, {{0, 10, "one", 0}, {10, 25, "two", 0}, {25, 30, "tail", 0}}};
        require(committed_cut(30 * sample_rate, 30 * sample_rate, boundary) == 25 * sample_rate);
        require(committed_cut(30 * sample_rate, 24 * sample_rate, boundary) == 10 * sample_rate);
        boundary.segments = {{0, 30, "unsplittable", 0}};
        require(committed_cut(30 * sample_rate, 30 * sample_rate, boundary) == 30 * sample_rate);
        boundary.segments.clear();
        require(committed_cut(30 * sample_rate, 25 * sample_rate, boundary) == 30 * sample_rate);
        require(pause_cut(16000, {{0, 13000}, {15000, 16000}}, 100) == 14000);
        require(pause_cut(16000, {{0, 16000}}, 100) == 16000);
        require(pause_cut(16000, {}, 100) == 16000);
        require(pause_cut(16000, {{0, 12000}}, 100) == 14000);
        require(pause_cut(16000, {{0, 13000}, {15000, 16000}}, 2000) == 16000);
        require(pause_cut(16000, {{0, 12000}, {15200, 16000}}, 200) == 13600);
        require(pause_cut(16000, {{0, 12000}, {15199, 16000}}, 200) == 16000);
        require(pause_cut(16000, {{0, 12000}, {12001, 16000}}, 0) == 12000);

        name = "incompatible and corrupted checkpoints";
        Fixture corrupt(root / "corrupt");
        auto original = corrupt.fingerprint();
        {
            Journal journal(corrupt.job, corrupt.options, original);
            journal.append(16, {"en", 0.001, {{0, 0.001, "text", 0}}});
        }
        corrupt.options.resume = true;
        corrupt.options.overwrite = true;
        for (const auto& field : {"source", "sha256", "backend", "run", "language", "model"}) {
            auto changed = original;
            changed[field] = "changed";
            rejects([&] { Journal journal(corrupt.job, corrupt.options, changed); });
        }
        corrupt.options.overwrite = false;
        auto modified = fs::last_write_time(corrupt.job.source);
        atomic_write(corrupt.job.source, "same bytes", true);
        fs::last_write_time(corrupt.job.source, modified);
        rejects([&] { Journal journal(corrupt.job, corrupt.options, corrupt.fingerprint()); });
        atomic_write(corrupt.job.source, "fake audio", true);
        {
            Journal journal(corrupt.job, corrupt.options, original);
            Audio shortened;
            shortened.total = 8;
            rejects([&] { run(corrupt, journal, shortened); });
            require(shortened.recognized == 0);
        }
        auto chunk = checkpoint_path(corrupt.job) / "chunk-0.json";
        auto manifest_path = checkpoint_path(corrupt.job) / "manifest.json";
        auto manifest_bytes = read_text(manifest_path);
        auto legacy = Json::parse(manifest_bytes);
        legacy["schema_version"] = 1;
        atomic_write(manifest_path, legacy.dump(), true);
        rejects([&] { Journal journal(corrupt.job, corrupt.options, original); });
        require(Json::parse(read_text(manifest_path))["schema_version"] == 1);
        atomic_write(manifest_path, manifest_bytes, true);
        auto saved = read_text(chunk);
        auto edited = Json::parse(saved);
        edited["segments"][0]["text"] = "tampered";
        atomic_write(chunk, edited.dump(), true);
        rejects([&] { Journal journal(corrupt.job, corrupt.options, original); });
        atomic_write(chunk, "{", true);
        rejects([&] { Journal journal(corrupt.job, corrupt.options, original); });
        atomic_write(chunk, saved, true);
        atomic_write(checkpoint_path(corrupt.job) / "chunk-1.json", "incomplete orphan");
        {
            Journal journal(corrupt.job, corrupt.options, original);
            journal.append(16, {"en", 0.001, {{0, 0.001, "next", 0}}});
            journal.visit([](const auto&) {});
        }
        corrupt.options.resume = false;
        corrupt.options.overwrite = true;
        {
            Journal journal(corrupt.job, corrupt.options, original);
            require(journal.samples() == 0);
        }

        name = "partial publication and external edits";
        Fixture publish(root / "publish");
        {
            Journal journal(publish.job, publish.options, publish.fingerprint());
            journal.append(16, {"en", 0.001, {{0, 0.001, "final", 0}}});
            journal.finish();
            fs::create_directory(publish.job.outputs.at("srt"));
            rejects([&] { publish_outputs(publish.job, publish.options, journal); });
            require(fs::exists(publish.job.outputs.at("json")));
            require(!fs::exists(publish.job.outputs.at("text")));
            fs::remove(publish.job.outputs.at("srt"));
        }
        auto json = read_text(publish.job.outputs.at("json"));
        atomic_write(publish.job.outputs.at("json"), "external edit", true);
        publish.options.resume = true;
        require(prepare_jobs(publish.options).size() == 1);
        rejects([&] { Journal journal(publish.job, publish.options, publish.fingerprint()); });
        atomic_write(publish.job.outputs.at("json"), json, true);
        {
            Journal journal(publish.job, publish.options, publish.fingerprint());
            require(journal.finished());
            Audio audio;
            run(publish, journal, audio);
            require(audio.cursor == 0 && audio.recognized == 0);
        }

        name = "write failure before manifest commit";
        Fixture disk(root / "disk");
        auto manifest = checkpoint_path(disk.job) / "manifest.json";
        std::string saved_manifest;
        {
            Journal journal(disk.job, disk.options, disk.fingerprint());
            journal.append(16, {"en", 0.001, {{0, 0.001, "committed", 0}}});
            saved_manifest = read_text(manifest);
            fs::remove(manifest);
            fs::create_directory(manifest);
            rejects([&] { journal.append(16, {"en", 0.001, {{0, 0.001, "uncommitted", 0}}}); });
        }
        fs::remove(manifest);
        atomic_write(manifest, saved_manifest);
        disk.options.resume = true;
        {
            Journal journal(disk.job, disk.options, disk.fingerprint());
            require(journal.samples() == 16);
            Audio audio;
            run(disk, journal, audio);
        }

        name = "checkpoint symlink rejection";
        Fixture links(root / "links");
        fs::create_symlink(root, root / "links/.whisper-transcribator");
        rejects([&] { Journal journal(links.job, links.options, links.fingerprint()); });

        name = "signal recovery";
        for (int signal : {SIGINT, SIGTERM, SIGKILL}) {
            Fixture signal_fixture(root / ("signal-" + std::to_string(signal)));
            signal_test(signal_fixture, signal);
        }
    } catch (const std::exception& error) {
        std::cerr << "FAILED " << name << ": " << error.what() << " (fixtures: " << root << ")\n";
        return 1;
    }
    fs::remove_all(root);
    std::cout << "Bounded pipeline, checkpoint integrity, publication and signal recovery passed\n";
}
