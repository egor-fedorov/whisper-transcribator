#include "support/test.hpp"
#include "support/cancel.hpp"
#include <cstdlib>
#include <iostream>
#include <unistd.h>

namespace wt::test {
TempDirectory::TempDirectory(const fs::path& parent) : owner(getpid()) {
    fs::create_directories(parent);
    auto pattern = (parent / "whisper-test-XXXXXX").string();
    if (!mkdtemp(pattern.data()))
        throw std::runtime_error("mkdtemp failed");
    // Resolved like the paths under test: the macOS temporary directory is behind a symlink.
    path = fs::canonical(pattern);
}
TempDirectory::~TempDirectory() {
    if (!retained && getpid() == owner) {
        std::error_code error;
        fs::remove_all(path, error);
    }
}
ScopedCurrentPath::ScopedCurrentPath(const fs::path& path) : previous(fs::current_path()) {
    fs::current_path(path);
}
ScopedCurrentPath::~ScopedCurrentPath() {
    std::error_code error;
    fs::current_path(previous, error);
}
ScopedEnv::ScopedEnv(const std::string& name, const std::optional<std::string>& value) : key(name) {
    if (const auto* old = std::getenv(key.c_str()))
        previous = old;
    if (value ? setenv(key.c_str(), value->c_str(), 1) : unsetenv(key.c_str()))
        throw std::runtime_error("Cannot set test environment: " + key);
}
ScopedEnv::~ScopedEnv() {
    if (previous)
        setenv(key.c_str(), previous->c_str(), 1);
    else
        unsetenv(key.c_str());
}
StreamCapture::StreamCapture(std::ostream& stream, std::streambuf* buffer)
    : stream(stream), previous(stream.rdbuf(buffer)) {}
StreamCapture::~StreamCapture() { stream.rdbuf(previous); }
int run_tests(std::initializer_list<TestCase> cases) {
    size_t failures = 0;
    for (const auto& item : cases) {
        fs::path fixtures;
        auto signal = stop_signal;
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
