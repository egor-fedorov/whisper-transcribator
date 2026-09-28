#include "platform/file.hpp"
#include "platform/win32/handles.hpp"
#include "platform/win32/security.hpp"
#include "platform/win32/status.hpp"
#include <algorithm>
#include <cerrno>
#include <random>
#include <utility>

namespace wt::platform {
File& File::operator=(File&& other) noexcept {
    if (this != &other) {
        close();
        handle = other.release();
    }
    return *this;
}
File::Native File::release() noexcept { return std::exchange(handle, invalid); }
int File::close() noexcept {
    if (handle == invalid)
        return 0;
    if (CloseHandle(reinterpret_cast<HANDLE>(release())))
        return 0;
    win32::fail();
    return -1;
}
File open_for_reading(const fs::path& path) {
    return win32::open_unfollowed(path, GENERIC_READ, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
}
File open_private(const fs::path& path) {
    win32::PrivateSecurity security(false);
    if (!security.get()) {
        errno = EACCES;
        return {};
    }
    return win32::open_unfollowed(path, GENERIC_READ | GENERIC_WRITE, OPEN_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, security.get());
}
File open_directory(const fs::path& path) {
    File file(win32::native(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL,
                                        win32::share_all, nullptr, OPEN_EXISTING,
                                        FILE_FLAG_BACKUP_SEMANTICS, nullptr)));
    if (!file)
        win32::fail();
    else if (!win32::is_directory(win32::handle(file))) {
        errno = ENOTDIR;
        return {};
    }
    return file;
}
File create_temporary(const fs::path& directory, const std::string& prefix, fs::path& path) {
    static const char characters[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device random;
    std::uniform_int_distribution<size_t> pick(0, sizeof(characters) - 2);
    win32::PrivateSecurity security(false);
    if (!security.get()) {
        errno = EACCES;
        return {};
    }
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto name = prefix;
        for (int i = 0; i < 6; ++i)
            name += characters[pick(random)];
        auto candidate = directory / name;
        File file(win32::native(CreateFileW(candidate.c_str(), GENERIC_READ | GENERIC_WRITE,
                                            win32::share_all, security.get(), CREATE_NEW,
                                            FILE_ATTRIBUTE_NORMAL, nullptr)));
        if (file) {
            path = candidate;
            return file;
        }
        if (GetLastError() != ERROR_FILE_EXISTS) {
            win32::fail();
            return {};
        }
    }
    errno = EEXIST;
    return {};
}
bool create_private_directory(const fs::path& path) {
    win32::PrivateSecurity security(true);
    if (!security.get()) {
        errno = EACCES;
        return false;
    }
    if (CreateDirectoryW(path.c_str(), security.get()))
        return true;
    win32::fail();
    return false;
}
std::optional<FileStatus> status(const File& file) {
    return win32::query(win32::handle(file), true);
}
std::optional<FileStatus> status(const fs::path& path) { return win32::status_at(path, 0); }
std::optional<FileStatus> link_status(const fs::path& path) {
    return win32::status_at(path, FILE_FLAG_OPEN_REPARSE_POINT);
}
long long read(File& file, char* data, size_t size) {
    DWORD count = 0;
    if (ReadFile(win32::handle(file), data, static_cast<DWORD>(std::min<size_t>(size, 1U << 30)),
                 &count, nullptr))
        return count;
    win32::fail();
    return -1;
}
long long write(File& file, const char* data, size_t size) {
    DWORD count = 0;
    if (WriteFile(win32::handle(file), data, static_cast<DWORD>(std::min<size_t>(size, 1U << 30)),
                  &count, nullptr))
        return count;
    win32::fail();
    return -1;
}
std::optional<uint64_t> seek_end(File& file) {
    LARGE_INTEGER end{};
    if (!SetFilePointerEx(win32::handle(file), LARGE_INTEGER{}, &end, FILE_END)) {
        win32::fail();
        return std::nullopt;
    }
    return static_cast<uint64_t>(end.QuadPart);
}
bool truncate(File& file) {
    if (SetFilePointerEx(win32::handle(file), LARGE_INTEGER{}, nullptr, FILE_BEGIN) &&
        SetEndOfFile(win32::handle(file)))
        return true;
    win32::fail();
    return false;
}
Lock try_lock(File& file) {
    OVERLAPPED start{};
    if (LockFileEx(win32::handle(file), LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0,
                   MAXDWORD, MAXDWORD, &start))
        return Lock::acquired;
    auto error = GetLastError();
    win32::fail(error);
    return error == ERROR_LOCK_VIOLATION || error == ERROR_IO_PENDING ? Lock::busy : Lock::failed;
}
bool sync(File& file) {
    // NTFS and ReFS journal directory changes; FlushFileBuffers needs write access to them.
    if (win32::is_directory(win32::handle(file)))
        return true;
    if (FlushFileBuffers(win32::handle(file)))
        return true;
    // Like fsync(), this also flushes files opened only for reading.
    if (GetLastError() == ERROR_ACCESS_DENIED) {
        auto writable = win32::reopen(file, GENERIC_WRITE);
        if (writable && FlushFileBuffers(win32::handle(writable)))
            return true;
    }
    win32::fail();
    return false;
}
unsigned default_permissions() { return 0666; }
bool set_permissions(File& file, unsigned permissions) {
    return win32::set_access(file, permissions);
}
bool rename_noreplace(const fs::path& from, const fs::path& to) {
    // Without MOVEFILE_REPLACE_EXISTING the filesystem refuses an existing target atomically.
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH))
        return true;
    auto error = GetLastError();
    if (error == ERROR_ACCESS_DENIED && GetFileAttributesW(to.c_str()) != INVALID_FILE_ATTRIBUTES)
        errno = EEXIST;
    else
        win32::fail(error);
    return false;
}
bool rename_replace(const fs::path& from, const fs::path& to) {
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        return true;
    win32::fail();
    return false;
}
bool ownership_ignored(const fs::path& path) {
    std::wstring root(MAX_PATH + 1, L'\0');
    DWORD flags = 0;
    return GetVolumePathNameW(path.c_str(), root.data(), static_cast<DWORD>(root.size())) &&
           GetVolumeInformationW(root.c_str(), nullptr, 0, nullptr, nullptr, &flags, nullptr, 0) &&
           !(flags & FILE_PERSISTENT_ACLS);
}
} // namespace wt::platform
