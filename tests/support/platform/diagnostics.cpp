#include "support/platform/diagnostics.hpp"
#include <cstdint>
#include <cstdio>
#ifdef _WIN32
#include <windows.h>
#endif

namespace wt::test {
#ifdef _WIN32
namespace {
LONG WINAPI report_native_crash(EXCEPTION_POINTERS* exception) {
    const auto* record = exception->ExceptionRecord;
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION)
        return EXCEPTION_CONTINUE_SEARCH;
    std::fprintf(stderr, "Native access violation at %p\n", record->ExceptionAddress);
    auto report_address = [](void* address) {
        MEMORY_BASIC_INFORMATION region{};
        char module[32768]{};
        VirtualQuery(address, &region, sizeof(region));
        GetModuleFileNameA(static_cast<HMODULE>(region.AllocationBase), module, sizeof(module));
        auto offset = reinterpret_cast<uintptr_t>(address) -
                      reinterpret_cast<uintptr_t>(region.AllocationBase);
        std::fprintf(stderr, "  %s + 0x%llx\n", module, static_cast<unsigned long long>(offset));
    };
    report_address(record->ExceptionAddress);
#ifdef _M_ARM64
    report_address(reinterpret_cast<void*>(exception->ContextRecord->Lr));
#endif
    void* frames[16]{};
    auto count = CaptureStackBackTrace(0, 16, frames, nullptr);
    for (USHORT i = 0; i < count; ++i)
        report_address(frames[i]);
    std::fflush(stderr);
    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace
#endif
CrashDiagnostics::CrashDiagnostics() {
#ifdef _WIN32
    handler = AddVectoredExceptionHandler(1, report_native_crash);
#endif
}
CrashDiagnostics::~CrashDiagnostics() {
#ifdef _WIN32
    if (handler)
        RemoveVectoredExceptionHandler(handler);
#endif
}
} // namespace wt::test
