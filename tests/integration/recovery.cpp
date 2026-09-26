#include "support/cancel.hpp"
#include "support/fixtures.hpp"
#include "support/io.hpp"
#include "support/test.hpp"
#include "transcript/journal.hpp"
#include "transcript/pipeline.hpp"
#include <sys/wait.h>
#include <unistd.h>

using namespace wt;
using namespace wt::test;
namespace {
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
void signal_recovery(const fs::path& root) {
    for (int signal : {SIGINT, SIGTERM, SIGKILL}) {
        Fixture signal_fixture(root / ("signal-" + std::to_string(signal)));
        signal_test(signal_fixture, signal);
    }
}
void repeated_interrupt(const fs::path&) {
    auto child = fork();
    if (child < 0)
        throw std::runtime_error("Unexpected signal handling result");
    if (!child) {
        install_signal_handlers();
        raise(SIGINT);
        if (stop_signal != SIGINT)
            _exit(3);
        raise(SIGTERM);
        _exit(4);
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 143)
        throw std::runtime_error("Unexpected signal handling result");
}
} // namespace
int main() {
    return run_tests({
        {"second interrupt terminates immediately", repeated_interrupt},
        {"signal recovery", signal_recovery},
    });
}
