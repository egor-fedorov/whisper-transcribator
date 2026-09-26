#pragma once
#include <csignal>

namespace wt {
struct Cancelled {};
extern volatile std::sig_atomic_t stop_signal;
void check_cancelled();
void install_signal_handlers();
} // namespace wt
