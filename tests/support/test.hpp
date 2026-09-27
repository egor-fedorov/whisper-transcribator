#pragma once
#include "support/fs.hpp"
#include <functional>
#include <initializer_list>
#include <iosfwd>
#include <optional>
#include <stdexcept>
#include <string>
#include <sys/types.h>

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
class TempDirectory {
    pid_t owner;
    bool retained = false;

  public:
    fs::path path;
    explicit TempDirectory(const fs::path& parent = fs::temp_directory_path());
    ~TempDirectory();
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
    void preserve() { retained = true; }
};
class ScopedCurrentPath {
    fs::path previous;

  public:
    explicit ScopedCurrentPath(const fs::path& path);
    ~ScopedCurrentPath();
    ScopedCurrentPath(const ScopedCurrentPath&) = delete;
    ScopedCurrentPath& operator=(const ScopedCurrentPath&) = delete;
};
class ScopedEnv {
    std::string key;
    std::optional<std::string> previous;

  public:
    ScopedEnv(const std::string& key, const std::optional<std::string>& value);
    ~ScopedEnv();
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;
};
class StreamCapture {
    std::ostream& stream;
    std::streambuf* previous;

  public:
    StreamCapture(std::ostream& stream, std::streambuf* buffer);
    ~StreamCapture();
    StreamCapture(const StreamCapture&) = delete;
    StreamCapture& operator=(const StreamCapture&) = delete;
};
struct TestCase {
    const char* name;
    std::function<void(const fs::path&)> action;
};
int run_tests(std::initializer_list<TestCase> cases);
} // namespace wt::test
