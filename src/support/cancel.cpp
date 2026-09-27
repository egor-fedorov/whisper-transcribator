#include "support/cancel.hpp"
#include <stdexcept>
#include <unistd.h>

namespace wt {
volatile std::sig_atomic_t stop_signal = 0;
void check_cancelled() {
    if (stop_signal)
        throw Cancelled{};
}
void install_signal_handlers() {
    struct sigaction action {};
    action.sa_handler = [](int signal) {
        if (stop_signal)
            _exit(128 + signal);
        stop_signal = signal;
    };
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGINT);
    sigaddset(&action.sa_mask, SIGTERM);
    if (sigaction(SIGINT, &action, nullptr) || sigaction(SIGTERM, &action, nullptr))
        throw std::runtime_error("Cannot install signal handlers");
}
} // namespace wt
