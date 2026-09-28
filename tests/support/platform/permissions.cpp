#include "support/platform/permissions.hpp"
#include "platform/file.hpp"
#include <cerrno>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>

// Windows SDK extensions require windows.h first.
#include <aclapi.h>
#include <sddl.h>
#endif

namespace wt::test {
void permissions(const fs::path& path, fs::perms mode, std::error_code& error) {
#ifdef _WIN32
    if ((mode & fs::perms::others_all) != fs::perms::none) {
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:P(A;OICI;FA;;;WD)", SDDL_REVISION_1, &descriptor, nullptr)) {
            error.assign(static_cast<int>(GetLastError()), std::system_category());
            return;
        }
        BOOL present = FALSE, defaulted = FALSE;
        PACL acl = nullptr;
        DWORD result = ERROR_INVALID_SECURITY_DESCR;
        if (GetSecurityDescriptorDacl(descriptor, &present, &acl, &defaulted) && present)
            result = SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
                                           DACL_SECURITY_INFORMATION |
                                               PROTECTED_DACL_SECURITY_INFORMATION,
                                           nullptr, nullptr, acl, nullptr);
        LocalFree(descriptor);
        error.assign(static_cast<int>(result), std::system_category());
        return;
    }
    auto file =
        fs::is_directory(path) ? platform::open_directory(path) : platform::open_for_reading(path);
    if (!file || !platform::set_permissions(file, static_cast<unsigned>(mode)))
        error.assign(errno, std::generic_category());
    else if ((mode & fs::perms::owner_write) == fs::perms::none) {
        // A private ACL is normally full access. Read-only fixtures must actually deny writes.
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        PACL acl = nullptr;
        DWORD result = GetSecurityInfo(reinterpret_cast<HANDLE>(file.native()), SE_FILE_OBJECT,
                                       DACL_SECURITY_INFORMATION, nullptr, nullptr, &acl, nullptr,
                                       &descriptor);
        if (result == ERROR_SUCCESS && acl) {
            for (DWORD i = 0; i < acl->AceCount; ++i) {
                void* entry = nullptr;
                if (!GetAce(acl, i, &entry)) {
                    result = GetLastError();
                    break;
                }
                auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(entry);
                if (ace->Header.AceType == ACCESS_ALLOWED_ACE_TYPE)
                    ace->Mask = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE | WRITE_DAC;
            }
            if (result == ERROR_SUCCESS)
                result = SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
                                               DACL_SECURITY_INFORMATION |
                                                   PROTECTED_DACL_SECURITY_INFORMATION,
                                               nullptr, nullptr, acl, nullptr);
        } else if (result == ERROR_SUCCESS)
            result = ERROR_INVALID_ACL;
        LocalFree(descriptor);
        error.assign(static_cast<int>(result), std::system_category());
    } else
        error.clear();
#else
    fs::permissions(path, mode, error);
#endif
}
void permissions(const fs::path& path, fs::perms mode) {
    std::error_code error;
    wt::test::permissions(path, mode, error);
    if (error)
        throw std::runtime_error("Cannot set fixture permissions: " + error.message());
}
} // namespace wt::test
