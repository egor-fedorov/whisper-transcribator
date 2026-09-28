#include "support/platform/memory.hpp"
#include "support/test.hpp"
#ifdef _WIN32
#include <windows.h>

// Requires windows.h first.
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace wt::test {
int64_t peak_rss_kib() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    require(GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) != 0);
    return static_cast<int64_t>(counters.PeakWorkingSetSize / 1024);
#else
    rusage usage{};
    require(getrusage(RUSAGE_SELF, &usage) == 0);
#ifdef __APPLE__
    return usage.ru_maxrss / 1024;
#else
    return usage.ru_maxrss;
#endif
#endif
}
} // namespace wt::test
