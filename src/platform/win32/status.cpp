#include "platform/win32/status.hpp"
#include "platform/win32/security.hpp"
#include <algorithm>

namespace wt::platform::win32 {
namespace {
int64_t ticks(const LARGE_INTEGER& time) { return time.QuadPart; }
} // namespace
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
    if (!read_permissions(file, with_security, directory, status))
        return std::nullopt;
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
} // namespace wt::platform::win32
