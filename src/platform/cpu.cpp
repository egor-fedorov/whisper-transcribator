#include "platform/cpu.hpp"
#include <algorithm>
#include <cerrno>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#if defined(__APPLE__)
#include <sys/sysctl.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sched.h>
#endif

namespace wt::platform {
namespace {
namespace fs = std::filesystem;
std::string read_optional(const fs::path& path) {
    std::ifstream input(path);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
long long integer(const std::string& value) {
    std::istringstream input(value);
    long long result = -1;
    char extra = 0;
    return (input >> result) && !(input >> extra) ? result : -1;
}
bool contains_controller(const std::string& value, const std::string& name) {
    return ("," + value + ",").find("," + name + ",") != std::string::npos;
}
std::string unescape(const std::string& value) {
    std::string result;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '\\' && i + 3 < value.size() && value[i + 1] >= '0' &&
            value[i + 1] <= '7' && value[i + 2] >= '0' && value[i + 2] <= '7' &&
            value[i + 3] >= '0' && value[i + 3] <= '7') {
            result += static_cast<char>((value[i + 1] - '0') * 64 + (value[i + 2] - '0') * 8 +
                                        value[i + 3] - '0');
            i += 3;
        } else
            result += value[i];
    }
    return result;
}
} // namespace
int cpu_threads_for(const std::vector<int>& allowed, const fs::path& root) {
    std::set<int> cpus(allowed.begin(), allowed.end());
    std::set<std::pair<long long, long long>> cores;
    bool complete = !cpus.empty();
    for (auto cpu : cpus) {
        auto path = root / "sys/devices/system/cpu" / ("cpu" + std::to_string(cpu)) / "topology";
        auto package = integer(read_optional(path / "physical_package_id"));
        auto core = integer(read_optional(path / "core_id"));
        complete &= package >= 0 && core >= 0;
        cores.emplace(package, core);
    }
    int threads = static_cast<int>(std::max<size_t>(1, complete ? cores.size() : cpus.size()));
    std::istringstream memberships(read_optional(root / "proc/self/cgroup"));
    std::string member;
    while (std::getline(memberships, member)) {
        auto first = member.find(':');
        auto second = member.find(':', first == std::string::npos ? 0 : first + 1);
        if (first == std::string::npos || second == std::string::npos)
            continue;
        auto controllers = member.substr(first + 1, second - first - 1);
        bool unified = controllers.empty();
        if (!unified && !contains_controller(controllers, "cpu"))
            continue;
        auto member_path = fs::path(member.substr(second + 1)).lexically_normal();
        std::istringstream mounts(read_optional(root / "proc/self/mountinfo"));
        std::string mount;
        while (std::getline(mounts, mount)) {
            auto divider = mount.find(" - ");
            if (divider == std::string::npos)
                continue;
            std::istringstream left(mount.substr(0, divider)), right(mount.substr(divider + 3));
            std::string id, parent, device, mount_root, mount_point, type, source, options;
            if (!(left >> id >> parent >> device >> mount_root >> mount_point) ||
                !(right >> type >> source >> options))
                continue;
            if (unified ? type != "cgroup2"
                        : (type != "cgroup" || !contains_controller(options, "cpu")))
                continue;
            auto relative =
                member_path.lexically_relative(fs::path(unescape(mount_root)).lexically_normal());
            if (relative.empty() || *relative.begin() == "..")
                continue;
            auto base = (root / fs::path(unescape(mount_point)).relative_path()).lexically_normal();
            auto directory = (base / relative).lexically_normal();
            while (true) {
                long long quota = -1, period = -1;
                if (unified) {
                    std::istringstream limit(read_optional(directory / "cpu.max"));
                    std::string value;
                    if (limit >> value >> period)
                        quota = integer(value);
                } else {
                    quota = integer(read_optional(directory / "cpu.cfs_quota_us"));
                    period = integer(read_optional(directory / "cpu.cfs_period_us"));
                }
                if (quota > 0 && period > 0)
                    threads = static_cast<int>(
                        std::min<long long>(threads, std::max<long long>(1, quota / period)));
                if (directory == base || directory == directory.parent_path())
                    break;
                directory = directory.parent_path();
            }
        }
    }
    return threads;
}
#ifdef __APPLE__
int automatic_cpu_threads() {
    // Performance cores only, as llama.cpp chooses: ggml threads meet at a barrier after each
    // operation, where slower efficiency cores tend to keep the others waiting. Intel Macs
    // report no performance levels.
    for (const char* name : {"hw.perflevel0.physicalcpu", "hw.physicalcpu"}) {
        int count = 0;
        size_t size = sizeof(count);
        if (!sysctlbyname(name, &count, &size, nullptr, 0) && count > 0)
            return count;
    }
    return static_cast<int>(std::max(1U, std::thread::hardware_concurrency()));
}
#elif defined(_WIN32)
int automatic_cpu_threads() {
    // Physical cores the process may run on, preferring performance cores as on macOS: hybrid
    // processors give them the highest efficiency class. A process confined to one processor
    // group has an affinity mask there; one spanning groups may run on every processor.
    DWORD_PTR process = 0, system = 0;
    USHORT group = 0, groups = 1;
    bool confined = GetProcessAffinityMask(GetCurrentProcess(), &process, &system) && process &&
                    GetProcessGroupAffinity(GetCurrentProcess(), &groups, &group);
    DWORD size = 0;
    GetLogicalProcessorInformationEx(RelationProcessorCore, nullptr, &size);
    std::vector<unsigned char> buffer(size);
    std::map<int, int> cores;
    if (size &&
        GetLogicalProcessorInformationEx(
            RelationProcessorCore,
            reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data()), &size))
        for (DWORD offset = 0; offset < size;) {
            const auto& core =
                *reinterpret_cast<SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*>(buffer.data() + offset);
            offset += core.Size;
            bool usable = !confined;
            for (WORD i = 0; i < core.Processor.GroupCount; ++i)
                usable |= core.Processor.GroupMask[i].Group == group &&
                          (core.Processor.GroupMask[i].Mask & process);
            // EfficiencyClass follows Flags; older headers, such as MinGW's, call it Reserved[0].
            if (usable)
                ++cores[reinterpret_cast<const BYTE*>(&core.Processor.Flags)[1]];
        }
    if (cores.empty())
        return static_cast<int>(std::max(1U, std::thread::hardware_concurrency()));
    return cores.rbegin()->second;
}
#else
int automatic_cpu_threads() {
    std::vector<int> allowed;
    for (size_t count = 1024; count <= (1U << 20); count *= 2) {
        auto bytes = CPU_ALLOC_SIZE(count);
        cpu_set_t* mask = CPU_ALLOC(count);
        if (!mask)
            break;
        CPU_ZERO_S(bytes, mask);
        int status = sched_getaffinity(0, bytes, mask);
        int error = errno;
        if (status == 0)
            for (size_t cpu = 0; cpu < count; ++cpu)
                if (CPU_ISSET_S(cpu, bytes, mask))
                    allowed.push_back(static_cast<int>(cpu));
        CPU_FREE(mask);
        if (status == 0 || error != EINVAL)
            break;
    }
    if (allowed.empty()) {
        auto count = std::max(1U, std::thread::hardware_concurrency());
        for (unsigned cpu = 0; cpu < count; ++cpu)
            allowed.push_back(static_cast<int>(cpu));
    }
    return cpu_threads_for(allowed, "/");
}
#endif
} // namespace wt::platform
