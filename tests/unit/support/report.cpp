#include "support/report.hpp"
#include "support/scoped.hpp"
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
void silent_progress(const fs::path&) {
    std::ostringstream output;
    {
        StreamCapture capture(std::cerr, output.rdbuf());
        configure_reporting(false, false);
        FileProgress progress("1/1", 120);
        progress.commit(30 * 16000);
        require(output.str().find("30.0s / ~120.0s") != std::string::npos);
        require(output.str().find("ETA") == std::string::npos);
        output.str("");
        finish_progress();
        progress.invalidate_duration();
        progress.begin_window(30 * 16000, 16000);
        require(output.str().find("~120") == std::string::npos);
        require(output.str().find("ETA") == std::string::npos);
    }
    configure_reporting(false, false);
}
} // namespace
int main() {
    return run_tests({{"reporting levels and throttling", reporting},
                      {"silent commits and invalidated duration estimates", silent_progress}});
}
