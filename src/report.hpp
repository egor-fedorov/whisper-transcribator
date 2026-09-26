#pragma once
#include <chrono>
#include <cstdint>
#include <string>

namespace wt {
enum class LogLevel { debug, info, warning, error };
void configure_reporting(bool quiet, bool verbose);
void log_message(LogLevel level, const std::string& message);
void report_progress(const std::string& phase, const std::string& detail, bool force = false);
void finish_progress();
void install_signal_handlers();
class FileProgress {
    std::string label;
    double total = 0, base = -1, committed = 0, start = 0, window = 0;
    std::chrono::steady_clock::time_point began;

  public:
    FileProgress(std::string label, double duration);
    void begin_window(int64_t samples, size_t count);
    void commit(int64_t samples);
    void update(int percent);
};
} // namespace wt
