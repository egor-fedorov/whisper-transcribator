#include "support/report.hpp"
#include "platform/system.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>

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
    bool tty = platform::stderr_is_terminal();
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
std::string format_seconds(double seconds) {
    std::ostringstream text;
    text << std::fixed << std::setprecision(1) << seconds;
    return text.str();
}
FileProgress::FileProgress(std::string value, double duration)
    : label(std::move(value)), total(std::isfinite(duration) && duration > 0 ? duration : 0) {}
void FileProgress::begin_window(int64_t samples, size_t count) {
    start = samples / 16000.0;
    window = count / 16000.0;
    if (base < 0) {
        base = committed = start;
        began = std::chrono::steady_clock::now();
    }
    update(0);
}
void FileProgress::commit(int64_t samples) {
    if (base < 0) {
        base = samples / 16000.0;
        began = std::chrono::steady_clock::now();
    }
    committed = samples / 16000.0;
    start = committed;
    window = 0;
    update(0);
}
void FileProgress::update(int percent) {
    double position = start + window * std::clamp(percent, 0, 100) / 100.0;
    if (position > total)
        total = 0;
    std::ostringstream text;
    text << label << " | " << std::fixed << std::setprecision(1) << position << "s";
    if (total > 0) {
        text << " / ~" << total << "s (" << std::min(99, int(position / total * 100)) << "%)";
        auto elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
        if (committed > base && elapsed > 0)
            text << " | ETA ~" << (total - position) * elapsed / (committed - base) << "s";
    }
    report_progress("Transcribing", text.str());
}
} // namespace wt
