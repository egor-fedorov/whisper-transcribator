#include "support/scoped.hpp"
#include <cstdlib>
#include <ostream>
#include <stdexcept>

namespace wt::test {
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
#ifdef _WIN32
    auto result = _putenv_s(key.c_str(), value ? value->c_str() : "");
#else
    auto result = value ? setenv(key.c_str(), value->c_str(), 1) : unsetenv(key.c_str());
#endif
    if (result)
        throw std::runtime_error("Cannot set test environment: " + key);
}
ScopedEnv::~ScopedEnv() {
#ifdef _WIN32
    _putenv_s(key.c_str(), previous ? previous->c_str() : "");
#else
    if (previous)
        setenv(key.c_str(), previous->c_str(), 1);
    else
        unsetenv(key.c_str());
#endif
}
StreamCapture::StreamCapture(std::ostream& stream, std::streambuf* buffer)
    : stream(stream), previous(stream.rdbuf(buffer)) {}
StreamCapture::~StreamCapture() { stream.rdbuf(previous); }
} // namespace wt::test
