#pragma once
#include "support/permissions.hpp"
#include <cstdint>
#include <optional>

namespace wt::checkpoint_detail {
class Privacy {
    fs::path output_directory;
    PermissionProbe probe;
    std::optional<StoredPermissions> stored;
    uint64_t device = 0;

  public:
    Privacy(fs::path directory, PermissionProbe permissions);
    bool accepts(const fs::path& path, const platform::FileStatus& status, bool owner_only);
};
void private_directory(const fs::path& path, Privacy& privacy);
bool safe_temporary(const fs::path& path, Privacy& privacy);
} // namespace wt::checkpoint_detail
