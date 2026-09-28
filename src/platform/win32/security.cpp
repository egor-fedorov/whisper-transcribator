#include "platform/win32/security.hpp"
#include <algorithm>
#include <cerrno>

// Requires windows.h from handles.hpp.
#include <aclapi.h>

namespace wt::platform::win32 {
namespace {
struct LocalMemory {
    void* memory = nullptr;
    LocalMemory() = default;
    LocalMemory(const LocalMemory&) = delete;
    LocalMemory& operator=(const LocalMemory&) = delete;
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
} // namespace
PrivateSecurity::PrivateSecurity(bool directory) : acl(private_acl(directory)) {
    security.nLength = sizeof(security);
    if (principals().user.empty() || principals().owner.empty() || acl.empty() ||
        !InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION) ||
        !SetSecurityDescriptorDacl(&descriptor, TRUE, reinterpret_cast<PACL>(acl.data()), FALSE) ||
        !SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED))
        return;
    security.lpSecurityDescriptor = &descriptor;
}
bool read_permissions(HANDLE file, bool with_security, bool directory, FileStatus& status) {
    // Volumes without access control lists (FAT, exFAT) and entries whose security this user
    // cannot read report no owner and access for everyone, as FAT mounts do on Linux.
    status.permissions = directory ? 0777 : 0666;
    PSID owner = nullptr;
    PACL acl = nullptr;
    LocalMemory descriptor;
    if (!with_security ||
        GetSecurityInfo(file, SE_FILE_OBJECT,
                        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr,
                        &acl, nullptr, &descriptor.memory) != ERROR_SUCCESS) {
        DWORD flags = 0;
        if (GetVolumeInformationByHandleW(file, nullptr, 0, nullptr, nullptr, &flags, nullptr, 0) &&
            !(flags & FILE_PERSISTENT_ACLS))
            return true;
        errno = EACCES;
        return false;
    }
    const auto& who = principals();
    status.owned = owner && who.current(owner);
    if (!acl)
        return true;
    for (DWORD index = 0; index < acl->AceCount; ++index) {
        void* entry = nullptr;
        if (!GetAce(acl, index, &entry))
            return true;
        auto* header = static_cast<ACE_HEADER*>(entry);
        if (header->AceFlags & INHERIT_ONLY_ACE || header->AceType == ACCESS_DENIED_ACE_TYPE)
            continue;
        if (header->AceType != ACCESS_ALLOWED_ACE_TYPE)
            return true;
        auto* allowed = static_cast<ACCESS_ALLOWED_ACE*>(entry);
        PSID sid = &allowed->SidStart;
        if (allowed->Mask && !who.trusted(sid) && !(owner && EqualSid(owner, sid)))
            return true;
    }
    status.permissions = directory ? 0700 : 0600;
    return true;
}
bool set_access(File& file, unsigned permissions) {
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
        auto name = final_path(handle(writable));
        if (name.empty())
            return false;
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
} // namespace wt::platform::win32
