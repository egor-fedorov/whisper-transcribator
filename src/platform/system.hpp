#pragma once
// Process-level operating-system services; system-posix.cpp implements them for Linux and macOS.
#include "support/fs.hpp"

namespace wt::platform {
// Calls `handler` with SIGINT or SIGTERM when the user interrupts the process or the system asks
// it to stop. The handler runs asynchronously: it may only set a volatile sig_atomic_t flag or
// call exit_now(). Returns false if the handler cannot be installed.
bool handle_interrupts(void (*handler)(int signal));
// Ends the process with `status` at once, without destructors or flushing streams.
[[noreturn]] void exit_now(int status);
bool stderr_is_terminal();
// The running executable, or an empty path if the system cannot report it.
fs::path executable_path();
// The shared library or executable containing `address`, or an empty path.
fs::path library_path(const void* address);
} // namespace wt::platform
