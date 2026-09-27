#include "support/cancel.hpp"
#include "platform/system.hpp"
#include <stdexcept>

namespace wt {
volatile std::sig_atomic_t stop_signal = 0;
void check_cancelled() {
    if (stop_signal)
        throw Cancelled{};
}
void install_signal_handlers() {
    // A second interrupt ends the process at once; committed progress stays recoverable.
    if (!platform::handle_interrupts([](int signal) {
            if (stop_signal)
                platform::exit_now(128 + signal);
            stop_signal = signal;
        }))
        throw std::runtime_error("Cannot install signal handlers");
}
} // namespace wt
