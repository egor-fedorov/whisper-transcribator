#include "support/io.hpp"
#include "support/cancel.hpp"
#include "support/error.hpp"
#include "support/fd.hpp"
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <streambuf>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace wt {
std::string env(const char* key) {
    const char* value = std::getenv(key);
    return value ? value : "";
}
fs::path resolve_path(const fs::path& path) {
    auto text = path.string();
    if (text == "~" || text.rfind("~/", 0) == 0) {
        if (env("HOME").empty())
            throw UsageError("HOME is not set");
        text = env("HOME") + text.substr(1);
    }
    return fs::weakly_canonical(fs::absolute(text));
}
fs::path executable_path() {
    std::error_code error;
#ifdef __APPLE__
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size))
        return {};
    auto result = fs::canonical(path.c_str(), error);
#else
    auto result = fs::read_symlink("/proc/self/exe", error);
#endif
    return error ? fs::path{} : result;
}
bool same_file(const fs::path& a, const fs::path& b) {
    return resolve_path(a) == resolve_path(b) ||
           (fs::exists(a) && fs::exists(b) && fs::equivalent(a, b));
}
std::string read_text(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Cannot read: " + path.string());
    std::ostringstream data;
    data << stream.rdbuf();
    if (stream.bad())
        throw std::runtime_error("Read failed: " + path.string());
    return data.str();
}
void probe_directory(const fs::path& path) {
    fs::create_directories(path);
    auto pattern = (path / ".whisper-probe-XXXXXX").string();
    UniqueFd fd(mkstemp(pattern.data()));
    if (fd.get() < 0)
        throw std::runtime_error("Cannot write directory: " + path.string());
    fd.close();
    unlink(pattern.c_str());
}
namespace {
class FileBuffer : public std::streambuf {
    int fd;
    char buffer[65536];
    int sync() override {
        size_t offset = 0, size = static_cast<size_t>(pptr() - pbase());
        while (offset < size) {
            check_cancelled();
            auto count = ::write(fd, buffer + offset, size - offset);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                throw std::runtime_error("Output write failed: " +
                                         std::string(std::strerror(errno)));
            offset += static_cast<size_t>(count);
        }
        setp(buffer, buffer + sizeof(buffer));
        return 0;
    }
    int_type overflow(int_type value) override {
        sync();
        if (!traits_type::eq_int_type(value, traits_type::eof())) {
            *pptr() = traits_type::to_char_type(value);
            pbump(1);
        }
        return traits_type::not_eof(value);
    }

  public:
    explicit FileBuffer(int descriptor) : fd(descriptor) { setp(buffer, buffer + sizeof(buffer)); }
};
mode_t output_mode(const fs::path& path, bool overwrite) {
    struct stat status {};
    mode_t mask = umask(0);
    umask(mask);
    return overwrite && stat(path.c_str(), &status) == 0 ? status.st_mode & 0777 : 0666 & ~mask;
}
// Last resort for filesystems with neither atomic no-replace renames nor hard links. The check
// and rename() are not atomic: the directory lock only excludes other publishers in this
// program, so a file another program creates at `to` in between would still be replaced.
int checked_rename(const fs::path& from, const fs::path& to) {
    auto parent = to.parent_path().empty() ? fs::path(".") : to.parent_path();
    UniqueFd directory(open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    int result = directory.get() < 0 ? -1 : 0;
    while (!result && flock(directory.get(), LOCK_EX))
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
// Sets the mode of a file this program created. A filesystem reporting another owner for it
// cannot store owners or modes and may refuse (exFAT mounted for another user); its mount
// options then decide the mode.
bool set_mode(int fd, mode_t mode) {
    struct stat status {};
    return !fchmod(fd, mode) || (!fstat(fd, &status) && status.st_uid != geteuid());
}
StoredPermissions probe_with_file(const fs::path& directory) {
    auto pattern = (directory / ".whisper-probe-XXXXXX").string();
    UniqueFd fd(mkstemp(pattern.data()));
    // Keep every check when the filesystem cannot be probed.
    if (fd.get() < 0)
        return {};
    struct stat status {};
    bool changed = fchmod(fd.get(), 0600) == 0, inspected = fstat(fd.get(), &status) == 0;
    fd.close();
    unlink(pattern.c_str());
    if (!inspected)
        return {};
    return {status.st_uid == geteuid(), changed && (status.st_mode & 0777) == 0600};
}
} // namespace
StoredPermissions (*probe_permissions)(const fs::path&) = probe_with_file;
bool StoredPermissions::accepts(const struct stat& status, bool owner_only) const {
    return (status.st_uid == geteuid() || !owner) && (owner_only || !mode);
}
int exclusive_rename(const char* from, const char* to) {
#if defined(__linux__) && defined(RENAME_NOREPLACE)
    // Supported by ext4, XFS, Btrfs, tmpfs, FAT, exFAT and FUSE filesystems implementing it.
    return renameat2(AT_FDCWD, from, AT_FDCWD, to, RENAME_NOREPLACE);
#elif defined(__APPLE__)
    // Supported by APFS and HFS+; other filesystems fail with ENOTSUP.
    return renamex_np(from, to, RENAME_EXCL);
#else
    // Add MoveFileExW without MOVEFILE_REPLACE_EXISTING for Windows here.
    (void)from;
    (void)to;
    errno = ENOSYS;
    return -1;
#endif
}
int rename_noreplace(const fs::path& from, const fs::path& to, const NoReplaceSteps& steps) {
    if (!steps.exclusive(from.c_str(), to.c_str()))
        return 0;
    // ENOTSUP and EOPNOTSUPP are the same on Linux, not on macOS.
    if (errno != EINVAL && errno != ENOSYS && errno != ENOTSUP)
        return -1;
    // A hard link cannot replace an existing name either (NFS, older kernels).
    if (!steps.link(from.c_str(), to.c_str()))
        return unlink(from.c_str());
    // FAT and exFAT reject hard links with EPERM on Linux and ENOTSUP on macOS; FUSE and SMB
    // mounts may report the others.
    if (errno != EPERM && errno != EOPNOTSUPP && errno != ENOTSUP && errno != ENOSYS)
        return -1;
    return checked_rename(from, to);
}
int sync_file(int fd) {
#ifdef F_FULLFSYNC
    // macOS fsync() can leave data in the drive's cache; filesystems without F_FULLFSYNC (such as
    // some network mounts) still get fsync().
    if (!fcntl(fd, F_FULLFSYNC))
        return 0;
#endif
    return fsync(fd);
}
void sync_directory(const fs::path& path) {
    UniqueFd fd(open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (fd.get() < 0)
        throw std::runtime_error("Cannot open directory for sync: " + path.string());
    int status = sync_file(fd.get());
    fd.close();
    if (status)
        throw std::runtime_error("Cannot sync directory: " + path.string());
}
void publish_file(const fs::path& temporary, const fs::path& target, bool overwrite) {
    check_cancelled();
    UniqueFd fd(open(temporary.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC));
    if (fd.get() < 0)
        throw std::runtime_error("Cannot open staged output");
    int status = set_mode(fd.get(), output_mode(target, overwrite)) ? sync_file(fd.get()) : -1;
    fd.close();
    if (status)
        throw std::runtime_error("Cannot flush staged output");
    status =
        overwrite ? rename(temporary.c_str(), target.c_str()) : rename_noreplace(temporary, target);
    if (status)
        throw std::runtime_error("Cannot publish " + target.string() + ": " + std::strerror(errno));
    sync_directory(target.parent_path());
    sync_directory(temporary.parent_path());
}
void atomic_write_stream(const fs::path& path, const std::function<void(std::ostream&)>& write,
                         bool overwrite, bool private_file) {
    check_cancelled();
    auto pattern = (path.parent_path() / ".whisper-output-XXXXXX").string();
    UniqueFd fd(mkstemp(pattern.data()));
    if (fd.get() < 0)
        throw std::runtime_error("Cannot create temporary output: " + path.string());
    try {
        FileBuffer buffer(fd.get());
        std::ostream stream(&buffer);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        write(stream);
        stream.flush();
        if (!set_mode(fd.get(), private_file ? 0600 : output_mode(path, overwrite)) ||
            sync_file(fd.get()))
            throw std::runtime_error("Cannot flush output");
        if (fd.close())
            throw std::runtime_error("Cannot close output");
        check_cancelled();
        int result =
            overwrite ? rename(pattern.c_str(), path.c_str()) : rename_noreplace(pattern, path);
        if (result)
            throw std::runtime_error("Cannot publish " + path.string() + ": " +
                                     std::strerror(errno));
    } catch (...) {
        fd.close();
        unlink(pattern.c_str());
        throw;
    }
    sync_directory(path.parent_path());
}
void atomic_write(const fs::path& path, const std::string& content, bool overwrite) {
    atomic_write_stream(path, [&](auto& stream) { stream << content; }, overwrite);
}
std::string trim(const std::string& value) {
    auto first = value.find_first_not_of(" \t\r\n");
    return first == std::string::npos
               ? ""
               : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
} // namespace wt
