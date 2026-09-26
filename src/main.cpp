#include "app.hpp"
#include <iostream>

int main(int argc, char** argv) {
    try {
        wt::install_signal_handlers();
        auto status = wt::run_cli(argc, argv);
        wt::finish_progress();
        return status;
    } catch (const wt::Cancelled&) {
        wt::log_message(wt::LogLevel::warning,
                        "Interrupted; committed progress is preserved. Repeat with --resume.");
        return 128 + wt::stop_signal;
    } catch (const wt::UsageError& error) {
        wt::log_message(wt::LogLevel::error, error.what());
        return 2;
    } catch (const std::exception& error) {
        wt::log_message(wt::LogLevel::error, error.what());
        return wt::stop_signal ? 128 + wt::stop_signal : 1;
    }
}
