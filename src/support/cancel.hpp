#pragma once
#include <atomic>
#include <csignal>

namespace wt {
struct Cancelled {};
static_assert(std::atomic<int>::is_always_lock_free, "Signal handlers require lock-free atomics");
extern std::atomic<int> stop_signal;
void check_cancelled();
void install_signal_handlers();
} // namespace wt
