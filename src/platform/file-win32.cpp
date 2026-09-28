#include "platform/file.hpp"
#include <algorithm>
#include <cerrno>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// These need the definitions of windows.h.
#include <aclapi.h>

namespace wt::platform {
namespace {
constexpr DWORD share_all = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
HANDLE handle(const File& file) { return reinterpret_cast<HANDLE>(file.native()); }
File::Native native(HANDLE handle) { return reinterpret_cast<File::Native>(handle); }
// Reports a Windows error through errno, as the POSIX implementation does.
void fail(DWORD error = GetLastError()) {
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
struct LocalMemory {
    void* memory = nullptr;
    ~LocalMemory() { LocalFree(memory); }
};
// A security identifier in a buffer of its own.
using Sid = std::vector<unsigned char>;
Sid copy_sid(PSID sid) {
    Sid result(GetLengthSid(sid));
    CopySid(static_cast<DWORD>(result.size()), result.data(), sid);
    return result;
}
Sid well_known_sid(WELL_KNOWN_SID_TYPE type) {
    Sid result(SECURITY_MAX_SID_SIZE);
    DWORD size = static_cast<DWORD>(result.size());
    if (!CreateWellKnownSid(type, nullptr, result.data(), &size))
        return {};
    result.resize(size);
    return result;
}
// The users a private entry admits: this process's user and the default owner of the files it
// creates (the Administrators group for elevated administrators), administrators and the system.
struct Principals {
    Sid user, owner, administrators = well_known_sid(WinBuiltinAdministratorsSid),
                     system = well_known_sid(WinLocalSystemSid);
    Principals() {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
            return;
        auto query = [&](TOKEN_INFORMATION_CLASS kind) {
            DWORD size = 0;
            GetTokenInformation(token, kind, nullptr, 0, &size);
            std::vector<unsigned char> buffer(size);
            if (!size || !GetTokenInformation(token, kind, buffer.data(), size, &size))
                buffer.clear();
            return buffer;
        };
        auto token_user = query(TokenUser), token_owner = query(TokenOwner);
        if (!token_user.empty())
            user = copy_sid(reinterpret_cast<TOKEN_USER*>(token_user.data())->User.Sid);
        if (!token_owner.empty())
            owner = copy_sid(reinterpret_cast<TOKEN_OWNER*>(token_owner.data())->Owner);
        CloseHandle(token);
    }
    static bool same(const Sid& a, PSID b) {
        return !a.empty() && EqualSid(const_cast<unsigned char*>(a.data()), b);
    }
    bool current(PSID sid) const { return same(user, sid) || same(owner, sid); }
    bool trusted(PSID sid) const {
        return current(sid) || same(administrators, sid) || same(system, sid);
    }
};
const Principals& principals() {
    static const Principals value;
    return value;
}
// An owner-only access control list: full access for the current user, its default owner,
// administrators and the system; directories pass it on to new entries.
std::vector<unsigned char> private_acl(bool directory) {
    const auto& who = principals();
    std::vector<const Sid*> sids;
    for (const auto* sid : {&who.user, &who.owner, &who.administrators, &who.system})
        if (!sid->empty() && std::none_of(sids.begin(), sids.end(), [&](const Sid* other) {
                return EqualSid(const_cast<unsigned char*>(other->data()),
                                const_cast<unsigned char*>(sid->data()));
            }))
            sids.push_back(sid);
    DWORD size = sizeof(ACL);
    for (const auto* sid : sids)
        size += static_cast<DWORD>(sizeof(ACCESS_ALLOWED_ACE) + sid->size());
    std::vector<unsigned char> acl(size);
    auto* list = reinterpret_cast<PACL>(acl.data());
    if (!InitializeAcl(list, size, ACL_REVISION))
        return {};
    DWORD inheritance = directory ? OBJECT_INHERIT_ACE | CONTAINER_INHERIT_ACE : 0;
    for (const auto* sid : sids)
        if (!AddAccessAllowedAceEx(list, ACL_REVISION, inheritance, FILE_ALL_ACCESS,
                                   const_cast<unsigned char*>(sid->data())))
            return {};
    return acl;
}
// Security attributes creating an entry that inherits nothing from its directory.
class PrivateSecurity {
    std::vector<unsigned char> acl;
    SECURITY_DESCRIPTOR descriptor{};
    SECURITY_ATTRIBUTES security{};

  public:
    explicit PrivateSecurity(bool directory) : acl(private_acl(directory)) {
        security.nLength = sizeof(security);
        if (acl.empty() ||
            !InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) ||
            !SetSecurityDescriptorDacl(&descriptor, TRUE, reinterpret_cast<PACL>(acl.data()),
                                       FALSE) ||
            !SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED))
            return;
        security.lpSecurityDescriptor = &descriptor;
    }
    SECURITY_ATTRIBUTES* get() { return security.lpSecurityDescriptor ? &security : nullptr; }
};
bool same_file(HANDLE a, HANDLE b) {
    BY_HANDLE_FILE_INFORMATION first{}, second{};
    return GetFileInformationByHandle(a, &first) && GetFileInformationByHandle(b, &second) &&
           first.dwVolumeSerialNumber == second.dwVolumeSerialNumber &&
           first.nFileIndexHigh == second.nFileIndexHigh &&
           first.nFileIndexLow == second.nFileIndexLow;
}
// Opens `path` itself if it is a symlink or junction and fails like O_NOFOLLOW. Other reparse
// points, such as cloud-storage placeholders and deduplicated files, keep their data behind the
// reparse point and are opened through it.
File open_unfollowed(const fs::path& path, DWORD access, DWORD disposition, DWORD flags,
                     SECURITY_ATTRIBUTES* security = nullptr) {
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
int64_t ticks(const LARGE_INTEGER& time) { return time.QuadPart; }
std::optional<FileStatus> query(HANDLE file, bool with_security) {
    FILE_BASIC_INFO basic{};
    FILE_STANDARD_INFO standard{};
    BY_HANDLE_FILE_INFORMATION identity{};
    if (!GetFileInformationByHandleEx(file, FileBasicInfo, &basic, sizeof(basic)) ||
        !GetFileInformationByHandleEx(file, FileStandardInfo, &standard, sizeof(standard)) ||
        !GetFileInformationByHandle(file, &identity)) {
        fail();
        return std::nullopt;
    }
    FileStatus status;
    bool directory = basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY;
    status.type = directory ? FileType::directory : FileType::regular;
    if (basic.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
        FILE_ATTRIBUTE_TAG_INFO tag{};
        if (GetFileInformationByHandleEx(file, FileAttributeTagInfo, &tag, sizeof(tag)) &&
            IsReparseTagNameSurrogate(tag.ReparseTag))
            status.type = FileType::symlink;
    }
    if (GetFileType(file) != FILE_TYPE_DISK)
        status.type = FileType::other;
    // ReFS identifies files with 128 bits; its 64-bit index is unique only per directory
    // hierarchy it can represent, so fold the full identifier where the volume provides it.
    status.id = {identity.dwVolumeSerialNumber,
                 (uint64_t(identity.nFileIndexHigh) << 32) | identity.nFileIndexLow};
    FILE_ID_INFO wide{};
    if (GetFileInformationByHandleEx(file, FileIdInfo, &wide, sizeof(wide))) {
        uint64_t parts[2];
        static_assert(sizeof(parts) == sizeof(wide.FileId.Identifier));
        std::copy_n(wide.FileId.Identifier, sizeof(parts), reinterpret_cast<unsigned char*>(parts));
        status.id = {wide.VolumeSerialNumber, parts[0] ^ parts[1]};
    }
    status.size = standard.EndOfFile.QuadPart > 0 ? uint64_t(standard.EndOfFile.QuadPart) : 0;
    status.links = standard.NumberOfLinks;
    auto modified = ticks(basic.LastWriteTime), changed = ticks(basic.ChangeTime);
    status.times = {modified / 10000000, modified % 10000000 * 100, changed / 10000000,
                    changed % 10000000 * 100};
    // Volumes without access control lists (FAT, exFAT) and entries whose security this user
    // cannot read report no owner and access for everyone, as FAT mounts do on Linux.
    status.permissions = directory ? 0777 : 0666;
    PSID owner = nullptr;
    PACL acl = nullptr;
    LocalMemory descriptor;
    if (!with_security ||
        GetSecurityInfo(file, SE_FILE_OBJECT,
                        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr,
                        &acl, nullptr, &descriptor.memory) != ERROR_SUCCESS)
        return status;
    const auto& who = principals();
    status.owned = owner && who.current(owner);
    if (!acl)
        return status;
    for (DWORD index = 0; index < acl->AceCount; ++index) {
        void* entry = nullptr;
        if (!GetAce(acl, index, &entry))
            return status;
        auto* header = static_cast<ACE_HEADER*>(entry);
        if (header->AceFlags & INHERIT_ONLY_ACE || header->AceType == ACCESS_DENIED_ACE_TYPE)
            continue;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
            return status;
        auto* allowed = static_cast<ACCESS_ALLOWED_ACE*>(entry);
        PSID sid = &allowed->SidStart;
        if (allowed->Mask && !who.trusted(sid) && !(owner && EqualSid(owner, sid)))
            return status;
    }
    status.permissions = directory ? 0700 : 0600;
    return status;
}
std::optional<FileStatus> status_at(const fs::path& path, DWORD flags) {
    flags |= FILE_FLAG_BACKUP_SEMANTICS;
    File file(native(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES | READ_CONTROL, share_all,
                                 nullptr, OPEN_EXISTING, flags, nullptr)));
    bool with_security = bool(file);
    if (!file && GetLastError() == ERROR_ACCESS_DENIED)
        file = File(native(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, share_all, nullptr,
                                       OPEN_EXISTING, flags, nullptr)));
    if (!file) {
        fail();
        return std::nullopt;
    }
    return query(handle(file), with_security);
}
bool is_directory(HANDLE file) {
    FILE_BASIC_INFO basic{};
    return GetFileInformationByHandleEx(file, FileBasicInfo, &basic, sizeof(basic)) &&
           (basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY);
}
// Another handle to the same file with `access`, for operations the original was not opened for.
File reopen(const File& file, DWORD access) {
    File result(native(ReOpenFile(handle(file), access, share_all, 0)));
    if (!result)
        fail();
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
int File::close() noexcept {
    if (handle == invalid)
        return 0;
    if (CloseHandle(reinterpret_cast<HANDLE>(release())))
        return 0;
    fail();
    return -1;
}
File open_for_reading(const fs::path& path) {
    return open_unfollowed(path, GENERIC_READ, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL);
}
File open_private(const fs::path& path) {
    PrivateSecurity security(false);
    return open_unfollowed(path, GENERIC_READ | GENERIC_WRITE, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           security.get());
}
File open_directory(const fs::path& path) {
    File file(native(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, share_all, nullptr,
                                 OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)));
    if (!file)
        fail();
    else if (!is_directory(handle(file))) {
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
    PrivateSecurity security(false);
    for (int attempt = 0; attempt < 100; ++attempt) {
        auto name = prefix;
        for (int i = 0; i < 6; ++i)
            name += characters[pick(random)];
        auto candidate = directory / name;
        File file(native(CreateFileW(candidate.c_str(), GENERIC_READ | GENERIC_WRITE, share_all,
                                     security.get(), CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)));
        if (file) {
            path = candidate;
            return file;
        }
        if (GetLastError() != ERROR_FILE_EXISTS) {
            fail();
            return {};
        }
    }
    errno = EEXIST;
    return {};
}
bool create_private_directory(const fs::path& path) {
    PrivateSecurity security(true);
    if (CreateDirectoryW(path.c_str(), security.get()))
        return true;
    fail();
    return false;
}
std::optional<FileStatus> status(const File& file) { return query(handle(file), true); }
std::optional<FileStatus> status(const fs::path& path) { return status_at(path, 0); }
std::optional<FileStatus> link_status(const fs::path& path) {
    return status_at(path, FILE_FLAG_OPEN_REPARSE_POINT);
}
long long read(File& file, char* data, size_t size) {
    DWORD count = 0;
    if (ReadFile(handle(file), data, static_cast<DWORD>(std::min<size_t>(size, 1U << 30)), &count,
                 nullptr))
        return count;
    fail();
    return -1;
}
long long write(File& file, const char* data, size_t size) {
    DWORD count = 0;
    if (WriteFile(handle(file), data, static_cast<DWORD>(std::min<size_t>(size, 1U << 30)), &count,
                  nullptr))
        return count;
    fail();
    return -1;
}
std::optional<uint64_t> seek_end(File& file) {
    LARGE_INTEGER end{};
    if (!SetFilePointerEx(handle(file), LARGE_INTEGER{}, &end, FILE_END)) {
        fail();
        return std::nullopt;
    }
    return static_cast<uint64_t>(end.QuadPart);
}
bool truncate(File& file) {
    if (SetFilePointerEx(handle(file), LARGE_INTEGER{}, nullptr, FILE_BEGIN) &&
        SetEndOfFile(handle(file)))
        return true;
    fail();
    return false;
}
Lock try_lock(File& file) {
    OVERLAPPED start{};
    if (LockFileEx(handle(file), LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, MAXDWORD,
                   MAXDWORD, &start))
        return Lock::acquired;
    auto error = GetLastError();
    fail(error);
    return error == ERROR_LOCK_VIOLATION || error == ERROR_IO_PENDING ? Lock::busy : Lock::failed;
}
bool sync(File& file) {
    // NTFS and ReFS journal directory changes; FlushFileBuffers needs write access to them.
    if (is_directory(handle(file)))
        return true;
    if (FlushFileBuffers(handle(file)))
        return true;
    // Like fsync(), this also flushes files opened only for reading.
    if (GetLastError() == ERROR_ACCESS_DENIED) {
        auto writable = reopen(file, GENERIC_WRITE);
        if (writable && FlushFileBuffers(handle(writable)))
            return true;
    }
    fail();
    return false;
}
unsigned default_permissions() { return 0666; }
bool set_permissions(File& file, unsigned permissions) {
    auto writable = reopen(file, READ_CONTROL | WRITE_DAC);
    if (!writable)
        return false;
    DWORD result;
    if (!(permissions & 0077)) {
        auto acl = private_acl(is_directory(handle(file)));
        if (acl.empty()) {
            fail();
            return false;
        }
        result = SetSecurityInfo(handle(writable), SE_FILE_OBJECT,
                                 DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
                                 nullptr, nullptr, reinterpret_cast<PACL>(acl.data()), nullptr);
    } else {
        // An empty unprotected list takes the directory's inheritable entries, as a new file
        // would; only the named variant reads the parent directory to find them.
        std::wstring name(MAX_PATH, L'\0');
        while (true) {
            auto length = GetFinalPathNameByHandleW(handle(writable), name.data(),
                                                    static_cast<DWORD>(name.size()), 0);
            if (!length) {
                fail();
                return false;
            }
            if (length < name.size()) {
                name.resize(length);
                break;
            }
            name.resize(length);
        }
        ACL empty{};
        if (!InitializeAcl(&empty, sizeof(empty), ACL_REVISION)) {
            fail();
            return false;
        }
        result =
            SetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT,
                                  DACL_SECURITY_INFORMATION | UNPROTECTED_DACL_SECURITY_INFORMATION,
                                  nullptr, nullptr, &empty, nullptr);
    }
    if (result == ERROR_SUCCESS)
        return true;
    fail(result);
    return false;
}
bool rename_noreplace(const fs::path& from, const fs::path& to) {
    // Without MOVEFILE_REPLACE_EXISTING the filesystem refuses an existing target atomically.
    if (MoveFileExW(from.c_str(), to.c_str(), MOVEFILE_WRITE_THROUGH))
        return true;
    fail();
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
