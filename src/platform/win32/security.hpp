#pragma once
#include "platform/win32/handles.hpp"
#include <vector>

namespace wt::platform::win32 {
// Owns the buffers referenced by the native attributes; it must not be copied or moved.
class PrivateSecurity {
    std::vector<unsigned char> acl;
    SECURITY_DESCRIPTOR descriptor{};
    SECURITY_ATTRIBUTES security{};

  public:
    explicit PrivateSecurity(bool directory);
    PrivateSecurity(const PrivateSecurity&) = delete;
    PrivateSecurity& operator=(const PrivateSecurity&) = delete;
    SECURITY_ATTRIBUTES* get() { return security.lpSecurityDescriptor ? &security : nullptr; }
};
bool read_permissions(HANDLE file, bool with_security, bool directory, FileStatus& status);
bool set_access(File& file, unsigned permissions);
} // namespace wt::platform::win32
