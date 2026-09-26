#include "models/models.hpp"
#include "support/cancel.hpp"
#include "support/error.hpp"
#include "support/fixtures.hpp"
#include "support/hash.hpp"
#include "support/io.hpp"
#include "support/options.hpp"
#include "support/test.hpp"
#include <chrono>
#include <fcntl.h>
#include <iostream>
#include <sys/file.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace wt;
using namespace wt::test;
namespace {
void await_file(const fs::path& path) {
    for (int i = 0; i < 500; ++i) {
        if (fs::exists(path))
            return;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    throw std::runtime_error("model worker timed out");
}
pid_t worker(const char* executable, const fs::path& root, const char* mode) {
    auto pid = fork();
    require(pid >= 0);
    if (!pid) {
        execl(executable, executable, mode, root.c_str(), nullptr);
        _exit(2);
    }
    return pid;
}
void wait_for(pid_t child, int expected) {
    int status = 0;
    require(waitpid(child, &status, 0) == child && WIFEXITED(status) &&
            WEXITSTATUS(status) == expected);
}
void concurrency(const fs::path& root, const char* executable) {
    auto first = worker(executable, root, "--download");
    await_file(root / "fetched");
    auto second = worker(executable, root, "--download");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    int status = 0;
    require(waitpid(second, &status, WNOHANG) == 0);
    atomic_write(root / "release", "release");
    wait_for(first, 0);
    wait_for(second, 0);
    require(read_text(root / model_fixture().file) == "abc");
    fs::remove(root / model_fixture().file);
    int fd = open((root / "fixture.bin.lock").c_str(), O_RDWR | O_CLOEXEC);
    require(fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0);
    auto cancelled = worker(executable, root, "--cancel");
    await_file(root / "ready");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    require(kill(cancelled, SIGTERM) == 0);
    wait_for(cancelled, 143);
    close(fd);
    require(!fs::exists(root / model_fixture().file));
}
void names(const fs::path& root) {
    ScopedCurrentPath current(root);
    fs::create_directory("small");
    Options options;
    options.download_root = (root / "cache").string();
    options.local_files_only = true;
    bool missing = false;
    try {
        prepare_model("small", options);
    } catch (const UsageError&) {
        throw std::runtime_error("Directory shadowed catalog name");
    } catch (const std::runtime_error& error) {
        missing = std::string(error.what()).find("offline") != std::string::npos;
    }
    require(missing && !fs::exists(options.download_root));
    fs::remove("small");
    atomic_write("./small", "local weights");
    require(prepare_model("./small", options).hash == sha256("./small"));
}
} // namespace
int main(int argc, char** argv) {
    if (argc == 3) {
        fs::path root = argv[2];
        struct sigaction action {};
        action.sa_handler = [](int signal) { stop_signal = signal; };
        sigemptyset(&action.sa_mask);
        sigaction(SIGTERM, &action, nullptr);
        try {
            bool cancel = std::string(argv[1]) == "--cancel";
            if (cancel)
                atomic_write(root / "ready", "ready");
            auto prepared =
                ensure_cached(model_fixture(), root, false, [&](const auto&, const auto& path) {
                    require(!cancel);
                    atomic_write(root / "fetched", "one download only");
                    await_file(root / "release");
                    atomic_write(path, "abc", true);
                });
            require(prepared.hash == model_fixture().hash);
            return 0;
        } catch (const Cancelled&) {
            return 128 + stop_signal;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    return run_tests({
        {"concurrent preparation and cancelled lock wait",
         [&](const fs::path& root) { concurrency(root, argv[0]); }},
        {"catalog precedence and explicit local paths", names},
    });
}
