#pragma once
#include "support/fs.hpp"
#include <functional>
#include <initializer_list>
#include <stdexcept>
#include <string>

namespace wt::test {
struct AssertionFailure : std::logic_error {
    using std::logic_error::logic_error;
};
inline void require(bool value, const std::string& message = "assertion failed",
                    const char* file = __builtin_FILE(), int line = __builtin_LINE()) {
    if (!value)
        throw AssertionFailure(std::string(file) + ":" + std::to_string(line) + ": " + message);
}
template <class Error = std::runtime_error, class F>
void rejects(F action, const std::string& reason, const char* file = __builtin_FILE(),
             int line = __builtin_LINE()) {
    require(!reason.empty(), "expected error reason must not be empty", file, line);
    try {
        action();
    } catch (const Error& error) {
        require(std::string(error.what()).find(reason) != std::string::npos,
                "expected error containing '" + reason + "', got: " + error.what(), file, line);
        return;
    } catch (const std::exception& error) {
        require(false, "unexpected exception type: " + std::string(error.what()), file, line);
    } catch (...) {
        require(false, "unexpected non-standard exception", file, line);
    }
    require(false, "expected failure containing: " + reason, file, line);
}
struct TestCase {
    const char* name;
    std::function<void(const fs::path&)> action;
};
int run_tests(std::initializer_list<TestCase> cases);
} // namespace wt::test
