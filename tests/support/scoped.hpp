#pragma once
#include "support/fs.hpp"
#include <iosfwd>
#include <optional>
#include <string>

namespace wt::test {
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
} // namespace wt::test
