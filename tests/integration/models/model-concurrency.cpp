#include "models/models.hpp"
#include "models/options.hpp"
#include "platform/file.hpp"
#include "platform/system.hpp"
#include "support/atomic.hpp"
#include "support/cancel.hpp"
#include "support/error.hpp"
#include "support/files.hpp"
#include "support/fixtures/models.hpp"
#include "support/hash.hpp"
#include "support/platform/process.hpp"
#include "support/scoped.hpp"
#include "support/test.hpp"
#include <chrono>
#include <iostream>
#include <thread>

using namespace wt;
using namespace wt::test;
namespace {
void concurrency(const fs::path& root) {
    auto executable = platform::executable_path();
    Process first(executable, {"--download", root.u8string()});
    await_file(root / "fetched");
    Process second(executable, {"--download", root.u8string()});
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    require(second.running());
    atomic_write(root / "release", "release");
    require(first.wait() == 0 && second.wait() == 0);
    require(read_text(root / model_fixture().file) == "abc");
    fs::remove(root / model_fixture().file);
    auto lock = platform::open_private(root / "fixture.bin.lock");
    require(lock && platform::try_lock(lock) == platform::Lock::acquired);
    Process cancelled(executable, {"--cancel", root.u8string()});
    await_file(root / "ready");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    cancelled.interrupt(SIGINT);
    require(cancelled.wait() == 130);
    lock.close();
    require(!fs::exists(root / model_fixture().file));
}
void names(const fs::path& root) {
    ScopedCurrentPath current(root);
    fs::create_directory("small");
    ModelCacheOptions options;
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
        fs::path root = fs::u8path(argv[2]);
        install_signal_handlers();
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
        {"concurrent preparation and cancelled lock wait", concurrency},
        {"catalog precedence and explicit local paths", names},
    });
}
