#include "report.hpp"
#include "app.hpp"
#include <chrono>
#include <iostream>
#include <mutex>
#include <unistd.h>

namespace wt {
namespace {
std::mutex output_mutex;
bool quiet = false, verbose = false, active_line = false;
std::string last_phase;
auto last_update = std::chrono::steady_clock::time_point::min();
void clear_line() {
    if (active_line) {
        std::cerr << '\n';
        active_line = false;
    }
}
} // namespace
void configure_reporting(bool silent, bool detailed) {
    std::lock_guard<std::mutex> lock(output_mutex);
    clear_line();
    quiet = silent;
    verbose = detailed;
    last_phase.clear();
    last_update = std::chrono::steady_clock::time_point::min();
}
void log_message(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(output_mutex);
    if ((level == LogLevel::debug && !verbose) ||
        (quiet && (level == LogLevel::debug || level == LogLevel::info)))
        return;
    clear_line();
    if (level == LogLevel::warning)
        std::cerr << "Warning: ";
    std::cerr << message;
    if (message.empty() || message.back() != '\n')
        std::cerr << '\n';
}
void report_progress(const std::string& phase, const std::string& detail, bool force) {
    std::lock_guard<std::mutex> lock(output_mutex);
    if (quiet)
        return;
    auto now = std::chrono::steady_clock::now();
    bool tty = isatty(STDERR_FILENO);
    auto interval = tty ? std::chrono::milliseconds(250) : std::chrono::milliseconds(5000);
    if (!force && phase == last_phase && now - last_update < interval)
        return;
    last_phase = phase;
    last_update = now;
    if (tty) {
        std::cerr << "\r\033[2K" << phase << ": " << detail << std::flush;
        active_line = true;
    } else
        std::cerr << phase << ": " << detail << '\n';
}
void finish_progress() {
    std::lock_guard<std::mutex> lock(output_mutex);
    clear_line();
    last_phase.clear();
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
