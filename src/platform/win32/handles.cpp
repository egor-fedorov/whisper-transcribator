#include "platform/win32/handles.hpp"
#include <cerrno>

namespace wt::platform::win32 {
// Reports a Windows error through errno, as the POSIX implementation does.
void fail(DWORD error) {
    switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_DRIVE:
    case ERROR_BAD_NETPATH:
    case ERROR_BAD_NET_NAME:
    case ERROR_BAD_PATHNAME:
        errno = ENOENT;
        break;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_DELETE_PENDING:
        errno = EACCES;
        break;
    case ERROR_PRIVILEGE_NOT_HELD:
        errno = EPERM;
        break;
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
        errno = EEXIST;
        break;
    case ERROR_DIRECTORY:
        errno = ENOTDIR;
        break;
    case ERROR_DIR_NOT_EMPTY:
        errno = ENOTEMPTY;
        break;
    case ERROR_NOT_SAME_DEVICE:
        errno = EXDEV;
        break;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
        errno = ENOSPC;
        break;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
        errno = ENOMEM;
        break;
    case ERROR_INVALID_FUNCTION:
    case ERROR_NOT_SUPPORTED:
        errno = ENOTSUP;
        break;
    case ERROR_INVALID_PARAMETER:
    case ERROR_INVALID_NAME:
    case ERROR_NEGATIVE_SEEK:
        errno = EINVAL;
        break;
    case ERROR_FILENAME_EXCED_RANGE:
        errno = ENAMETOOLONG;
        break;
    case ERROR_TOO_MANY_LINKS:
        errno = EMLINK;
        break;
    case ERROR_CANT_RESOLVE_FILENAME:
        errno = ELOOP;
        break;
    case ERROR_WRITE_PROTECT:
        errno = EROFS;
        break;
    case ERROR_BUSY:
        errno = EBUSY;
        break;
    case ERROR_INVALID_HANDLE:
        errno = EBADF;
        break;
    default:
        errno = EIO;
    }
}
namespace {
bool same_file(HANDLE a, HANDLE b) {
    BY_HANDLE_FILE_INFORMATION first{}, second{};
    return GetFileInformationByHandle(a, &first) && GetFileInformationByHandle(b, &second) &&
           first.dwVolumeSerialNumber == second.dwVolumeSerialNumber &&
           first.nFileIndexHigh == second.nFileIndexHigh &&
           first.nFileIndexLow == second.nFileIndexLow;
}
} // namespace
// Opens `path` itself if it is a symlink or junction and fails like O_NOFOLLOW. Other reparse
// points, such as cloud-storage placeholders and deduplicated files, keep their data behind the
// reparse point and are opened through it.
File open_unfollowed(const fs::path& path, DWORD access, DWORD disposition, DWORD flags,
                     SECURITY_ATTRIBUTES* security) {
    File file(native(CreateFileW(path.c_str(), access, share_all, security, disposition,
                                 flags | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)));
    if (!file) {
        fail();
        return file;
    }
    FILE_ATTRIBUTE_TAG_INFO tag{};
    if (!GetFileInformationByHandleEx(handle(file), FileAttributeTagInfo, &tag, sizeof(tag))) {
        fail();
        return {};
    }
    if (!(tag.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return file;
    if (IsReparseTagNameSurrogate(tag.ReparseTag)) {
        errno = ELOOP;
        return {};
    }
    File target(native(
        CreateFileW(path.c_str(), access, share_all, nullptr, OPEN_EXISTING, flags, nullptr)));
    if (!target) {
        fail();
        return target;
    }
    if (!same_file(handle(file), handle(target))) {
        errno = ELOOP;
        return {};
    }
    return target;
}
bool is_directory(HANDLE file) {
    FILE_BASIC_INFO basic{};
    return GetFileInformationByHandleEx(file, FileBasicInfo, &basic, sizeof(basic)) &&
           (basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}
std::wstring final_path(HANDLE file) {
    std::wstring name(MAX_PATH, L'\0');
    while (true) {
        auto length =
            GetFinalPathNameByHandleW(file, name.data(), static_cast<DWORD>(name.size()), 0);
        if (!length) {
            fail();
            return {};
        }
        if (length < name.size()) {
            name.resize(length);
            return name;
        }
        name.resize(length);
    }
}
// Another handle to the same file with `access`, for operations the original was not opened for.
File reopen(const File& file, DWORD access) {
    File result;
    if (is_directory(handle(file))) {
        auto path = final_path(handle(file));
        if (path.empty())
            return {};
        result = File(native(CreateFileW(path.c_str(), access, share_all, nullptr, OPEN_EXISTING,
                                         FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                         nullptr)));
        // A path replacement between lookup and open must never redirect an ACL change.
        if (result && !same_file(handle(file), handle(result))) {
            errno = ELOOP;
            return {};
        }
    } else
        result = File(native(ReOpenFile(handle(file), access, share_all, 0)));
    if (!result)
        fail();
    return result;
}
} // namespace wt::platform::win32
