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
inline void require(bool value, const std::string& message = "assertion failed") {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void rejects(F action) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed, "expected failure");
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
