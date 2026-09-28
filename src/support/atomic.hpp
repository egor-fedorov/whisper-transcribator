#pragma once
#include "support/fs.hpp"
#include <functional>
#include <iosfwd>
#include <string>

namespace wt {
void atomic_write(const fs::path& path, const std::string& content, bool overwrite = false);
void atomic_write_stream(const fs::path& path, const std::function<void(std::ostream&)>& write,
                         bool overwrite = false, bool private_file = false);
void sync_directory(const fs::path& path);
void publish_file(const fs::path& temporary, const fs::path& target, bool overwrite);
} // namespace wt
