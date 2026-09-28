#pragma once
#include "support/fs.hpp"
#include <functional>

namespace wt::platform {
struct FileStatus;
}
namespace wt {
// Which POSIX attributes a file created in a directory keeps. FAT, exFAT and some FUSE or SMB
// mounts report a fixed owner and mode from mount options instead.
struct StoredPermissions {
    bool owner = true, mode = true;
    // Whether `status` is owned by this user and `owner_only` holds for its mode, ignoring
    // attributes the filesystem cannot store.
    bool accepts(const platform::FileStatus& status, bool owner_only) const;
};
// Probes with a temporary file; unavailable attributes never relax unrelated checks.
StoredPermissions probe_permissions(const fs::path& directory);
using PermissionProbe = std::function<StoredPermissions(const fs::path&)>;
} // namespace wt
