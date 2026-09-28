#include "platform/system.hpp"
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <dlfcn.h>
#include <string>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace wt::platform {
bool handle_interrupts(void (*handler)(int signal)) {
    struct sigaction action {};
    action.sa_handler = handler;
    sigemptyset(&action.sa_mask);
    sigaddset(&action.sa_mask, SIGINT);
    sigaddset(&action.sa_mask, SIGTERM);
    return !sigaction(SIGINT, &action, nullptr) && !sigaction(SIGTERM, &action, nullptr);
}
void exit_now(int status) { _exit(status); }
void prepare_console() {}
bool stderr_is_terminal() { return isatty(STDERR_FILENO); }
fs::path home_directory() {
    const char* home = std::getenv("HOME");
    return home ? home : "";
}
fs::path cache_directory() {
    auto home = home_directory();
    return home.empty() ? home : home / ".cache";
}
fs::path executable_path() {
    std::error_code error;
#ifdef __APPLE__
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size))
        return {};
    auto result = fs::canonical(path.c_str(), error);
#else
    auto result = fs::read_symlink("/proc/self/exe", error);
#endif
    return error ? fs::path{} : result;
}
fs::path library_path(const void* address) {
    Dl_info library{};
    if (!dladdr(address, &library) || !library.dli_fname)
        return {};
    return library.dli_fname;
}
} // namespace wt::platform
