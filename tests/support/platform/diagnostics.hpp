#pragma once

namespace wt::test {
// Retains the failing DLL and offset in Windows CI logs; a no-op on other systems.
class CrashDiagnostics {
#ifdef _WIN32
    void* handler = nullptr;
#endif

  public:
    CrashDiagnostics();
    ~CrashDiagnostics();
    CrashDiagnostics(const CrashDiagnostics&) = delete;
    CrashDiagnostics& operator=(const CrashDiagnostics&) = delete;
};
} // namespace wt::test
