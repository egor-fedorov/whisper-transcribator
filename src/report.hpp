#pragma once
#include <string>

namespace wt {
enum class LogLevel { debug, info, warning, error };
void configure_reporting(bool quiet, bool verbose);
void log_message(LogLevel level, const std::string& message);
void report_progress(const std::string& phase, const std::string& detail, bool force = false);
void finish_progress();
void install_signal_handlers();
} // namespace wt
