#include "support/permissions.hpp"
#include "platform/file.hpp"

namespace wt {
StoredPermissions probe_permissions(const fs::path& directory) {
    fs::path probe;
    auto file = platform::create_temporary(directory, ".whisper-probe-", probe);
    // Keep every check when the filesystem cannot be probed.
    if (!file)
        return {};
    bool changed = platform::set_permissions(file, 0600);
    auto status = platform::status(file);
    file.close();
    std::error_code ignored;
    fs::remove(probe, ignored);
    if (!status)
        return {};
    return {status->owned && !platform::ownership_ignored(directory),
            changed && status->permissions == 0600};
}
bool StoredPermissions::accepts(const platform::FileStatus& status, bool owner_only) const {
    return (status.owned || !owner) && (owner_only || !mode);
}
} // namespace wt
