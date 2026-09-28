#pragma once
// Operating-system file access, implemented by file-posix.cpp for Linux and macOS and by
// file-win32.cpp for Windows. Failures are reported like the C library: false, -1 or an empty
// result, with errno describing the error.
#include "support/fs.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <tuple>

namespace wt::platform {
// Equal for every path of one file, including hard links.
struct FileId {
    uint64_t device = 0, file = 0;
    bool operator==(const FileId& other) const {
        return device == other.device && file == other.file;
    }
    bool operator!=(const FileId& other) const { return !(*this == other); }
    bool operator<(const FileId& other) const {
        return std::tie(device, file) < std::tie(other.device, other.file);
    }
};
enum class FileType { regular, directory, symlink, other };
struct FileStatus {
    FileType type = FileType::other;
    FileId id;
    uint64_t size = 0, links = 0;
    // Whether the current user owns the entry, and its POSIX permission bits. Windows reports
    // 0600 (0700 for directories) when only the owner, administrators and the system may access
    // the entry, and 0666 (0777) otherwise.
    bool owned = false;
    unsigned permissions = 0;
    // Modification and status-change times (seconds, nanoseconds): any change alters them.
    std::array<int64_t, 4> times{};
};
// An open file or directory, closed on destruction.
class File {
  public:
    // A descriptor on POSIX systems, a HANDLE on Windows.
#ifdef _WIN32
    using Native = std::intptr_t;
#else
    using Native = int;
#endif
    File() noexcept = default;
    explicit File(Native handle) noexcept : handle(handle) {}
    ~File() { close(); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
    File(File&& other) noexcept : handle(other.release()) {}
    File& operator=(File&& other) noexcept;
    explicit operator bool() const noexcept { return handle != invalid; }
    Native native() const noexcept { return handle; }
    Native release() noexcept;
    // Never retried: the system may already have released the handle.
    int close() noexcept;

  private:
    static constexpr Native invalid = -1;
    Native handle = invalid;
};
// Opens an existing file for reading. None of these opens follows a final symlink (or Windows
// junction) or waits for special files such as FIFOs.
File open_for_reading(const fs::path& path);
// Opens a file for reading and writing, creating it for the owner only.
File open_private(const fs::path& path);
File open_directory(const fs::path& path);
// Creates a new owner-only file named `prefix` plus six random characters in `directory`.
File create_temporary(const fs::path& directory, const std::string& prefix, fs::path& path);
// Creates an owner-only directory; fails with EEXIST if the name exists.
bool create_private_directory(const fs::path& path);
std::optional<FileStatus> status(const File& file);
std::optional<FileStatus> status(const fs::path& path);
// Status of `path` itself when it is a symlink.
std::optional<FileStatus> link_status(const fs::path& path);
// Read or write at least one byte, repeating interrupted calls: the count, 0 at the end of the
// file, or -1.
long long read(File& file, char* data, size_t size);
long long write(File& file, const char* data, size_t size);
// Moves to the end and returns the size.
std::optional<uint64_t> seek_end(File& file);
// Empties the file and moves to its start.
bool truncate(File& file);
enum class Lock { acquired, busy, failed };
// Tries to lock a file exclusively, without waiting for another process.
Lock try_lock(File& file);
// Flushes a file's data, or a directory's entries, to stable storage. Windows filesystems commit
// directory entries themselves, so syncing a directory succeeds without doing anything there.
bool sync(File& file);
// Permission bits of files created by default (umask applied).
unsigned default_permissions();
// Windows restricts the file to its owner, administrators and the system when `permissions`
// grant nothing to group and others, and otherwise lets it inherit its directory's permissions.
bool set_permissions(File& file, unsigned permissions);
// Renames `from` to `to` unless `to` exists (EEXIST), atomically where the filesystem supports
// it. If a fallback cannot remove `from` afterwards, this fails although `to` was published.
bool rename_noreplace(const fs::path& from, const fs::path& to);
// Whether the filesystem of `path` ignores ownership, so that every local user acts as the owner
// of its entries. macOS mounts external FAT and exFAT volumes this way by default; on Windows
// these are the volumes without access control lists.
bool ownership_ignored(const fs::path& path);
} // namespace wt::platform
