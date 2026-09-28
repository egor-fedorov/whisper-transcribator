#include "platform/system.hpp"
#include "support/cancel.hpp"
#include "support/fixtures.hpp"
#include "support/io.hpp"
#include "support/process.hpp"
#include "support/test.hpp"
#include "transcript/checkpoint/journal.hpp"
#include "transcript/pipeline.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

using namespace wt;
using namespace wt::test;
namespace {
void signal_test(Fixture& f, int signal) {
    Process child(platform::executable_path(), {"--signal", f.root.u8string()});
    await_file(f.root / "ready");
    if (signal == 0)
        child.terminate();
    else
        child.interrupt(signal);
    require(child.wait() == (signal ? 128 + signal : 137));
    require(!fs::exists(f.job.outputs.at("text")));
    f.options.checkpoint.resume = true;
    Journal journal(f.job, f.options.checkpoint, f.fingerprint(), describe_output(f.options));
    require(journal.samples() == 16);
    Audio audio;
    run(f, journal, audio);
    require(audio.recognized == 23, "only uncommitted tail may be recognized again");
}
void signal_recovery(const fs::path& root) {
    for (int signal : {SIGINT,
#ifndef _WIN32
                       SIGTERM,
#endif
                       0}) {
        Fixture f(root / ("signal-" + std::to_string(signal)));
        signal_test(f, signal);
    }
}
void repeated_interrupt(const fs::path& root) {
    Process child(platform::executable_path(), {"--twice", root.u8string()});
    await_file(root / "ready");
    child.interrupt(SIGINT);
    await_file(root / "interrupted");
    child.interrupt(SIGINT);
    require(child.wait() == 130);
}
void first_record_kill(const fs::path& root) {
    for (bool manifest_exists : {false, true}) {
        for (bool resume : {false, true}) {
            Fixture f(root / (std::to_string(manifest_exists) + std::to_string(resume)));
            auto directory = checkpoint_path(f.job);
            if (manifest_exists) {
                Journal journal(f.job, f.options.checkpoint, f.fingerprint(),
                                describe_output(f.options));
                journal.append(16, {"en", 0.001, {{0, 0.001, "orphan", 0}}});
            }
            fs::create_directories(directory);
            std::error_code refused;
            wt::test::permissions(directory.parent_path(), fs::perms::owner_all, refused);
            wt::test::permissions(directory, fs::perms::owner_all, refused);
            if (manifest_exists) {
                auto manifest = Json::parse(read_text(directory / "manifest.json"));
                manifest["chunks"] = manifest["samples"] = 0;
                manifest["last_hash"] = "";
                manifest["languages"] = Json::array();
                atomic_write(directory / "manifest.json", manifest.dump(), true);
                fs::remove(directory / "chunk-0.json");
            }
            Process child(
                platform::executable_path(),
                {"--partial",
                 (directory / (manifest_exists ? "chunk-0.json" : "manifest.json")).u8string()});
            await_file(directory / "ready");
            child.terminate();
            require(child.wait() == 137);
            fs::remove(directory / "ready");
            f.options.checkpoint.resume = resume;
            Journal journal(f.job, f.options.checkpoint, f.fingerprint(),
                            describe_output(f.options));
            require(journal.samples() == 0 && !has_checkpoint(f.job));
        }
    }
}
int worker(const std::string& mode, const fs::path& path) {
    install_signal_handlers();
    try {
        if (mode == "--partial") {
            atomic_write_stream(
                path,
                [&](auto& stream) {
                    stream << "{partial" << std::flush;
                    atomic_write(path.parent_path() / "ready", "ready");
                    wait_for_interrupt();
                },
                true, true);
        } else if (mode == "--twice") {
            atomic_write(path / "ready", "ready");
            try {
                wait_for_interrupt();
            } catch (const Cancelled&) {
            }
            // Keep the cancellation flag set: the next event must exit from the handler.
            std::ofstream(path / "interrupted").put('x');
            for (;;)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
        } else {
            Fixture f(path, true);
            Journal journal(f.job, f.options.checkpoint, f.fingerprint(),
                            describe_output(f.options));
            Audio audio;
            int calls = 0;
            run_chunks(
                journal, 16, [&](size_t n) { return audio.read(n); },
                [&](const auto& pcm) {
                    if (calls++ == 1) {
                        atomic_write(path / "ready", "ready");
                        wait_for_interrupt();
                    }
                    return audio.recognize(pcm);
                },
                [](const auto& pcm) { return pcm.size(); });
        }
    } catch (const Cancelled&) {
        return 128 + stop_signal;
    }
    return 2;
}
} // namespace
int main(int argc, char** argv) {
    if (argc == 3)
        return worker(argv[1], fs::u8path(argv[2]));
    return run_tests({
        {"second interrupt terminates immediately", repeated_interrupt},
        {"signal recovery", signal_recovery},
        {"SIGKILL during first manifest and chunk writes", first_record_kill},
    });
}
