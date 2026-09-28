#include "transcript/checkpoint/detail/privacy.hpp"
#include "platform/file.hpp"
#include "support/report.hpp"
#include <cerrno>
#include <regex>
#include <set>
#include <stdexcept>
#include <utility>

namespace wt::checkpoint_detail {
// Checkpoint entries must be owned by this user with owner-only modes. Filesystems without POSIX
// permissions (FAT, exFAT, some FUSE/SMB mounts) report a fixed owner and mode from mount
// options, so a failed check is accepted only for attributes a probe file in the output
// directory cannot keep either, and only on that filesystem. Where an entry's filesystem ignores
// ownership (macOS external volumes), every user appears to own it, so the owner check alone
// never accepts it. Entry types, symlinks and hard links are always checked.
Privacy::Privacy(fs::path directory) : output_directory(std::move(directory)) {}
bool Privacy::accepts(const fs::path& path, const platform::FileStatus& st, bool owner_only) {
    if (st.owned && owner_only && !platform::ownership_ignored(path))
        return true;
    if (!stored) {
        auto output = platform::status(output_directory);
        stored = output ? probe_permissions(output_directory) : StoredPermissions{};
        device = output ? output->id.device : 0;
        static std::set<fs::path> warned;
        if ((!stored->owner || !stored->mode) && warned.insert(output_directory).second)
            log_message(LogLevel::warning,
                        "Checkpoint privacy cannot be enforced on this filesystem because it "
                        "does not store POSIX owners and modes; saved transcript fragments in " +
                            (output_directory / ".whisper-transcribator").string() +
                            " are protected only by the volume's access settings");
    }
    return st.id.device == device && stored->accepts(st, owner_only);
}
void private_directory(const fs::path& path, Privacy& privacy) {
    bool created = platform::create_private_directory(path);
    if (!created && errno != EEXIST)
        throw std::runtime_error("Cannot create checkpoint directory: " + path.string());
    auto st = platform::link_status(path);
    if (!st || st->type != platform::FileType::directory ||
        !privacy.accepts(path, *st, !(st->permissions & 0077)))
        throw std::runtime_error("Checkpoint directory must be owned by you with mode 0700: " +
                                 path.string());
    if (created)
        sync_directory(path.parent_path());
}
bool safe_temporary(const fs::path& path, Privacy& privacy) {
    static const std::regex pattern("\\.whisper-output-[A-Za-z0-9]{6}");
    if (!std::regex_match(path.filename().string(), pattern))
        return false;
    auto st = platform::link_status(path);
    if (!st || st->type != platform::FileType::regular || st->links != 1 ||
        !privacy.accepts(path, *st, st->permissions == 0600) || st->size > 16 * 1024 * 1024)
        throw std::runtime_error("Unsafe checkpoint temporary file: " + path.string());
    return true;
}
} // namespace wt::checkpoint_detail
