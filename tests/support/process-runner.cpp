#include "platform/system.hpp"
#include "support/cancel.hpp"
#include "support/files.hpp"
#include "support/process.hpp"
#include "support/test.hpp"
#include <chrono>
#include <iostream>
#include <thread>

// Smoke-test interruption without Bash signals, which cannot reach native Windows processes.
int main(int argc, char** argv) {
    using namespace wt;
    using namespace wt::test;
    platform::prepare_console();
    if (argc < 5 || std::string(argv[1]) != "--interrupt-after")
        return 2;
    try {
        auto log = fs::u8path(argv[2]);
        std::string needle = argv[3];
        Process child(fs::u8path(argv[4]), std::vector<std::string>(argv + 5, argv + argc), log);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
        for (;;) {
            require(child.running(), "Transcription exited before interruption");
            if (needle.empty()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                break;
            }
            if (fs::exists(log) && read_text(log).find(needle) != std::string::npos)
                break;
            require(std::chrono::steady_clock::now() < deadline, "Checkpoint wait timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
#ifdef _WIN32
        child.interrupt(SIGINT);
#else
        child.interrupt(SIGTERM);
#endif
        return child.wait(30);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
