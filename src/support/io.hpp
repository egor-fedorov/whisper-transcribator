#pragma once
#include "support/fs.hpp"
#include <functional>
#include <iosfwd>
#include <string>

namespace wt::platform {
struct FileStatus;
}
namespace wt {
fs::path resolve_path(const fs::path& path);
bool same_file(const fs::path& a, const fs::path& b);
void probe_directory(const fs::path& path);
void atomic_write(const fs::path& path, const std::string& content, bool overwrite = false);
void atomic_write_stream(const fs::path& path, const std::function<void(std::ostream&)>& write,
                         bool overwrite = false, bool private_file = false);
void sync_directory(const fs::path& path);
void publish_file(const fs::path& temporary, const fs::path& target, bool overwrite);
// Which POSIX attributes a file created in a directory keeps. FAT, exFAT and some FUSE or SMB
// mounts report a fixed owner and mode from mount options instead.
struct StoredPermissions {
    bool owner = true, mode = true;
    // Whether `status` is owned by this user and `owner_only` holds for its mode, ignoring
    // attributes the filesystem cannot store.
    bool accepts(const platform::FileStatus& status, bool owner_only) const;
};
// Probes `directory` with a temporary file; tests replace it to simulate such filesystems.
extern StoredPermissions (*probe_permissions)(const fs::path& directory);
std::string read_text(const fs::path& path);
std::string trim(const std::string& value);
std::string env(const char* key);
} // namespace wt
