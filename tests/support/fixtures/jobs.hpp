#pragma once
#include "app/options.hpp"
#include "support/fs.hpp"
#include <string>

namespace wt::test {
CliOptions input(const fs::path& root, const std::string& name = "a.mp4");
} // namespace wt::test
