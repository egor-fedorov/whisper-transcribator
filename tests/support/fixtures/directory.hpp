#pragma once
#include "support/fs.hpp"

namespace wt::test {
class TempDirectory {
    bool retained = false;

  public:
    fs::path path;
    explicit TempDirectory(const fs::path& parent = fs::temp_directory_path());
    ~TempDirectory();
    TempDirectory(const TempDirectory&) = delete;
    TempDirectory& operator=(const TempDirectory&) = delete;
    void preserve() { retained = true; }
};
} // namespace wt::test
