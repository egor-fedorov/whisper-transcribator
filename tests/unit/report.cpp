#include "support/report.hpp"
#include "support/test.hpp"
#include <iostream>
#include <sstream>

using namespace wt;
using namespace wt::test;
namespace {
void reporting(const fs::path&) {
    std::ostringstream output;
    {
        StreamCapture capture(std::cerr, output.rdbuf());
        configure_reporting(true, false);
        log_message(LogLevel::info, "hidden-info");
        report_progress("hidden-progress", "0");
        log_message(LogLevel::warning, "visible-warning");
        configure_reporting(false, false);
        log_message(LogLevel::debug, "hidden-debug");
        log_message(LogLevel::info, "visible-info");
        report_progress("phase", "visible-first");
        report_progress("phase", "hidden-second");
        configure_reporting(false, true);
        log_message(LogLevel::debug, "visible-debug");
    }
    auto text = output.str();
    if (text.find("hidden-") != std::string::npos ||
        text.find("visible-warning") == std::string::npos ||
        text.find("visible-info") == std::string::npos ||
        text.find("visible-first") == std::string::npos ||
        text.find("visible-debug") == std::string::npos)
        throw std::runtime_error("Incorrect reporting or throttling");
    configure_reporting(false, false);
}
} // namespace
int main() { return run_tests({{"reporting levels and throttling", reporting}}); }
