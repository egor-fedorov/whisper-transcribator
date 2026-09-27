#pragma once
#include "support/fs.hpp"
#include <functional>
#include <iosfwd>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace wt {
fs::path resolve_path(const fs::path& path);
// The running executable, or an empty path if the platform cannot report it.
fs::path executable_path();
bool same_file(const fs::path& a, const fs::path& b);
void probe_directory(const fs::path& path);
void atomic_write(const fs::path& path, const std::string& content, bool overwrite = false);
void atomic_write_stream(const fs::path& path, const std::function<void(std::ostream&)>& write,
                         bool overwrite = false, bool private_file = false);
// Flushes a file or directory descriptor to stable storage; returns 0 or -1 like fsync().
int sync_file(int fd);
void sync_directory(const fs::path& path);
void publish_file(const fs::path& temporary, const fs::path& target, bool overwrite);
// The platform's atomic rename that fails with EEXIST instead of replacing `to`. It fails with
// EINVAL, ENOSYS or ENOTSUP where the kernel or filesystem cannot provide it.
int exclusive_rename(const char* from, const char* to);
// Primitives of rename_noreplace(), each returning 0 or -1 with errno set; replaceable in tests.
struct NoReplaceSteps {
    int (*exclusive)(const char* from, const char* to) = exclusive_rename;
    int (*link)(const char* from, const char* to) = ::link;
};
// Renames `from` to `to` unless `to` exists (EEXIST), returning 0 or -1 with errno set like
// rename(). Use rename() where replacement is intended. If the hard-link fallback cannot remove
// `from` afterwards, this fails although `to` was published.
int rename_noreplace(const fs::path& from, const fs::path& to, const NoReplaceSteps& steps = {});
// Which POSIX attributes a file created in a directory keeps. FAT, exFAT and some FUSE or SMB
// mounts report a fixed owner and mode from mount options instead.
struct StoredPermissions {
    bool owner = true, mode = true;
    // Whether `status` is owned by this user and `owner_only` holds for its mode, ignoring
    // attributes the filesystem cannot store.
    bool accepts(const struct stat& status, bool owner_only) const;
};
// Whether the filesystem of `path` ignores ownership, so that every local user acts as the owner
// of its entries. macOS mounts external FAT and exFAT volumes this way by default.
bool ownership_ignored(const fs::path& path);
// Probes `directory` with a temporary file; tests replace it to simulate such filesystems.
extern StoredPermissions (*probe_permissions)(const fs::path& directory);
std::string read_text(const fs::path& path);
std::string trim(const std::string& value);
std::string env(const char* key);
} // namespace wt
