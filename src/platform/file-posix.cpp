#include "platform/file.hpp"
#include "platform/posix.hpp"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#ifdef __APPLE__
#include <sys/mount.h>
#endif

namespace wt::platform {
namespace {
FileStatus convert(const struct stat& st) {
    FileStatus status;
    status.type = S_ISREG(st.st_mode)   ? FileType::regular
                  : S_ISDIR(st.st_mode) ? FileType::directory
                  : S_ISLNK(st.st_mode) ? FileType::symlink
                                        : FileType::other;
    status.id = {static_cast<uint64_t>(st.st_dev), static_cast<uint64_t>(st.st_ino)};
    status.size = st.st_size > 0 ? static_cast<uint64_t>(st.st_size) : 0;
    status.links = static_cast<uint64_t>(st.st_nlink);
    status.owned = st.st_uid == geteuid();
    status.permissions = st.st_mode & 0777;
#ifdef __APPLE__
    const auto &modified = st.st_mtimespec, &changed = st.st_ctimespec;
#else
    const auto &modified = st.st_mtim, &changed = st.st_ctim;
#endif
    status.times = {modified.tv_sec, modified.tv_nsec, changed.tv_sec, changed.tv_nsec};
    return status;
}
// Last resort for filesystems with neither atomic no-replace renames nor hard links. The check
// and rename() are not atomic: the directory lock only excludes other publishers in this
// program, so a file another program creates at `to` in between would still be replaced.
int checked_rename(const fs::path& from, const fs::path& to) {
    auto parent = to.parent_path().empty() ? fs::path(".") : to.parent_path();
    File directory = open_directory(parent);
    int result = directory ? 0 : -1;
    while (!result && flock(directory.native(), LOCK_EX))
        result = errno == EINTR ? 0 : -1;
    struct stat status {};
    if (!result && !lstat(to.c_str(), &status)) {
        errno = EEXIST;
        result = -1;
    } else if (!result)
        result = errno == ENOENT ? rename(from.c_str(), to.c_str()) : -1;
    int error = errno;
    directory.close();
    errno = error;
    return result;
}
} // namespace
File& File::operator=(File&& other) noexcept {
    if (this != &other) {
        close();
        handle = other.release();
    }
    return *this;
}
File::Native File::release() noexcept { return std::exchange(handle, invalid); }
int File::close() noexcept { return handle == invalid ? 0 : ::close(release()); }
File open_for_reading(const fs::path& path) {
    return File(open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
}
File open_private(const fs::path& path) {
    return File(open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600));
}
File open_directory(const fs::path& path) {
    return File(open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
}
File create_temporary(const fs::path& directory, const std::string& prefix, fs::path& path) {
    auto pattern = (directory / (prefix + "XXXXXX")).string();
    File file(mkstemp(pattern.data()));
    if (file)
        path = pattern;
    return file;
}
bool create_private_directory(const fs::path& path) { return mkdir(path.c_str(), 0700) == 0; }
std::optional<FileStatus> status(const File& file) {
    struct stat st {};
    if (fstat(file.native(), &st))
        return std::nullopt;
    return convert(st);
}
std::optional<FileStatus> status(const fs::path& path) {
    struct stat st {};
    if (stat(path.c_str(), &st))
        return std::nullopt;
    return convert(st);
}
std::optional<FileStatus> link_status(const fs::path& path) {
    struct stat st {};
    if (lstat(path.c_str(), &st))
        return std::nullopt;
    return convert(st);
}
long long read(File& file, char* data, size_t size) {
    while (true) {
        auto count = ::read(file.native(), data, size);
        if (count >= 0 || errno != EINTR)
            return count;
    }
}
long long write(File& file, const char* data, size_t size) {
    while (true) {
        auto count = ::write(file.native(), data, size);
        if (count >= 0 || errno != EINTR)
            return count;
    }
}
std::optional<uint64_t> seek_end(File& file) {
    auto end = lseek(file.native(), 0, SEEK_END);
    if (end < 0)
        return std::nullopt;
    return static_cast<uint64_t>(end);
}
bool truncate(File& file) {
    return !ftruncate(file.native(), 0) && lseek(file.native(), 0, SEEK_SET) == 0;
}
Lock try_lock(File& file) {
    if (!flock(file.native(), LOCK_EX | LOCK_NB))
        return Lock::acquired;
    return errno == EWOULDBLOCK || errno == EAGAIN || errno == EINTR ? Lock::busy : Lock::failed;
}
bool sync(File& file) {
#ifdef F_FULLFSYNC
    // macOS fsync() can leave data in the drive's cache; filesystems without F_FULLFSYNC (such as
    // some network mounts) still get fsync().
    if (!fcntl(file.native(), F_FULLFSYNC))
        return true;
#endif
    return !fsync(file.native());
}
unsigned default_permissions() {
    mode_t mask = umask(0);
    umask(mask);
    return 0666 & ~mask;
}
bool set_permissions(File& file, unsigned permissions) {
    return !fchmod(file.native(), static_cast<mode_t>(permissions));
}
int exclusive_rename(const char* from, const char* to) {
#if defined(__linux__) && defined(RENAME_NOREPLACE)
    // Supported by ext4, XFS, Btrfs, tmpfs, FAT, exFAT and FUSE filesystems implementing it.
    return renameat2(AT_FDCWD, from, AT_FDCWD, to, RENAME_NOREPLACE);
#elif defined(__APPLE__)
    // Supported by APFS and HFS+; other filesystems fail with ENOTSUP.
    return renamex_np(from, to, RENAME_EXCL);
#else
    (void)from;
    (void)to;
    errno = ENOSYS;
    return -1;
#endif
}
bool rename_noreplace(const fs::path& from, const fs::path& to, const NoReplaceSteps& steps) {
    if (!steps.exclusive(from.c_str(), to.c_str()))
        return true;
    // ENOTSUP and EOPNOTSUPP are the same on Linux, not on macOS.
    if (errno != EINVAL && errno != ENOSYS && errno != ENOTSUP)
        return false;
    // A hard link cannot replace an existing name either (NFS, older kernels).
    if (!steps.link(from.c_str(), to.c_str()))
        return !unlink(from.c_str());
    // FAT and exFAT reject hard links with EPERM on Linux and ENOTSUP on macOS; FUSE and SMB
    // mounts may report the others.
    if (errno != EPERM && errno != EOPNOTSUPP && errno != ENOTSUP && errno != ENOSYS)
        return false;
    return !checked_rename(from, to);
}
bool rename_noreplace(const fs::path& from, const fs::path& to) {
    return rename_noreplace(from, to, NoReplaceSteps{});
}
bool rename_replace(const fs::path& from, const fs::path& to) {
    return !rename(from.c_str(), to.c_str());
}
bool ownership_ignored(const fs::path& path) {
#ifdef __APPLE__
    struct statfs status {};
    return !statfs(path.c_str(), &status) && (status.f_flags & MNT_IGNORE_OWNERSHIP);
#else
    (void)path;
    return false;
#endif
}
} // namespace wt::platform
