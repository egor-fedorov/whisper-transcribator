#include "support/report.hpp"
#include "support/cancel.hpp"
#include <iostream>
#include <sstream>
#include <sys/wait.h>
#include <unistd.h>

using namespace wt;
int main() {
    std::ostringstream output;
    auto* previous = std::cerr.rdbuf(output.rdbuf());
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
    std::cerr.rdbuf(previous);
    auto text = output.str();
    if (text.find("hidden-") != std::string::npos ||
        text.find("visible-warning") == std::string::npos ||
        text.find("visible-info") == std::string::npos ||
        text.find("visible-first") == std::string::npos ||
        text.find("visible-debug") == std::string::npos)
        return 1;
    auto child = fork();
    if (child < 0)
        return 1;
    if (!child) {
        install_signal_handlers();
        raise(SIGINT);
        if (stop_signal != SIGINT)
            _exit(3);
        raise(SIGTERM);
        _exit(4);
    }
    int status = 0;
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status) != 143)
        return 1;
    std::cout << "Reporting, throttling and repeated cancellation passed\n";
}
