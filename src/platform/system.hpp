#pragma once
// Process-level operating-system services, implemented by system-posix.cpp for Linux and macOS and
// by system-win32.cpp for Windows.
#include "support/fs.hpp"

namespace wt::platform {
// Calls `handler` with SIGINT or SIGTERM when the user interrupts the process or the system asks
// it to stop. The handler runs asynchronously: it may only set a volatile sig_atomic_t flag or
// call exit_now(). Returns false if the handler cannot be installed. On Windows, Ctrl+C and
// Ctrl+Break report SIGINT; closing the console, logging off and shutting down report SIGTERM,
// after which the system ends the process within seconds.
bool handle_interrupts(void (*handler)(int signal));
// Ends the process with `status` at once, without destructors or flushing streams.
[[noreturn]] void exit_now(int status);
// Prepares the console for UTF-8 text and terminal escape sequences where the system needs it
// (Windows), restoring its previous settings at exit.
void prepare_console();
// Whether standard error is a terminal that understands the escape sequences of progress lines.
bool stderr_is_terminal();
// The user's home directory (HOME; USERPROFILE on Windows), or an empty path.
fs::path home_directory();
// The default directory for per-user caches (~/.cache; %LOCALAPPDATA% on Windows), or an empty
// path if the environment does not name one.
fs::path cache_directory();
// The running executable, or an empty path if the system cannot report it.
fs::path executable_path();
// The shared library or executable containing `address`, or an empty path.
fs::path library_path(const void* address);
} // namespace wt::platform
