#include "app.hpp"
#include <iostream>

int main(int argc, char** argv) {
    struct sigaction action {};
    action.sa_handler = [](int signal) { wt::stop_signal = signal; };
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    try {
        return wt::run_cli(argc, argv);
    } catch (const wt::Cancelled&) {
        std::cerr << "Interrupted; unfinished file must be retried\n";
        return 128 + wt::stop_signal;
    } catch (const wt::UsageError& error) {
        std::cerr << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return wt::stop_signal ? 128 + wt::stop_signal : 1;
    }
}
