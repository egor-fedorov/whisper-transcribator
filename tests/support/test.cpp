#include "support/test.hpp"
#include "support/cancel.hpp"
#include "support/fixtures/directory.hpp"
#include "support/platform/diagnostics.hpp"
#include <iostream>

namespace wt::test {
int run_tests(std::initializer_list<TestCase> cases) {
    CrashDiagnostics diagnostics;
    size_t failures = 0;
    for (const auto& item : cases) {
        fs::path fixtures;
        auto signal = stop_signal.load();
        try {
            TempDirectory directory;
            fixtures = directory.path;
            try {
                item.action(directory.path);
            } catch (...) {
                directory.preserve();
                throw;
            }
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAILED " << item.name << ": " << error.what()
                      << " (fixtures: " << fixtures << ")\n";
        } catch (...) {
            ++failures;
            std::cerr << "FAILED " << item.name << ": unexpected exception (fixtures: " << fixtures
                      << ")\n";
        }
        stop_signal = signal;
    }
    std::cout << cases.size() - failures << '/' << cases.size() << " scenarios passed\n";
    return failures ? 1 : 0;
}
} // namespace wt::test
