#include "support/fixtures/directory.hpp"
#include "platform/file.hpp"
#include <cerrno>
#include <random>
#include <stdexcept>

namespace wt::test {
TempDirectory::TempDirectory(const fs::path& parent) {
    fs::create_directories(parent);
    std::random_device random;
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto candidate = parent / ("whisper-test-" + std::to_string(random()));
        if (platform::create_private_directory(candidate)) {
            path = fs::canonical(candidate);
            return;
        }
        if (errno != EEXIST)
            break;
    }
    throw std::runtime_error("Cannot create test directory");
}
TempDirectory::~TempDirectory() {
    if (!retained) {
        std::error_code error;
        fs::remove_all(path, error);
    }
}
} // namespace wt::test
