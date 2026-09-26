#include "cpu.hpp"
#include "app.hpp"
#include <iostream>
#include <unistd.h>

using namespace wt;
namespace {
void require(bool value) {
    if (!value)
        throw std::runtime_error("CPU limit assertion failed");
}
void file(const fs::path& path, const std::string& value) {
    fs::create_directories(path.parent_path());
    atomic_write(path, value, true);
}
} // namespace
int main() {
    char pattern[] = "/tmp/whisper-cpu-XXXXXX";
    auto* created = mkdtemp(pattern);
    if (!created)
        return 1;
    fs::path root = created;
    try {
        require(cpu_threads_for({}, root) == 1);
        require(cpu_threads_for({0, 1, 2, 3}, root) == 4);
        for (int cpu = 0; cpu < 8; ++cpu) {
            auto path =
                root / "sys/devices/system/cpu" / ("cpu" + std::to_string(cpu)) / "topology";
            file(path / "physical_package_id", "0");
            file(path / "core_id", std::to_string(cpu / 2));
        }
        require(cpu_threads_for({0, 1, 2, 3}, root) == 2);
        require(cpu_threads_for({0, 2, 4, 6}, root) == 4);
        file(root / "proc/self/cgroup", "0::/parent/job\n");
        file(root / "proc/self/mountinfo", "1 0 0:1 / /sys/fs/cgroup rw - cgroup2 cgroup rw\n");
        file(root / "sys/fs/cgroup/cpu.max", "max 100000");
        file(root / "sys/fs/cgroup/parent/cpu.max", "250000 100000");
        file(root / "sys/fs/cgroup/parent/job/cpu.max", "max 100000");
        require(cpu_threads_for({0, 2, 4, 6}, root) == 2);
        file(root / "sys/fs/cgroup/parent/job/cpu.max", "50000 100000");
        require(cpu_threads_for({0, 2, 4, 6}, root) == 1);
        file(root / "proc/self/cgroup", "2:cpu,cpuacct:/container/job\n");
        file(root / "proc/self/mountinfo",
             "1 0 0:1 /container /sys/fs/cgroup/cpu rw - cgroup cgroup rw,cpu,cpuacct\n");
        file(root / "sys/fs/cgroup/cpu/cpu.cfs_quota_us", "300000");
        file(root / "sys/fs/cgroup/cpu/cpu.cfs_period_us", "100000");
        file(root / "sys/fs/cgroup/cpu/job/cpu.cfs_quota_us", "-1");
        file(root / "sys/fs/cgroup/cpu/job/cpu.cfs_period_us", "100000");
        require(cpu_threads_for({0, 2, 4, 6}, root) == 3);
        require(cpu_threads_for({0, 1}, root) == 1);
        file(root / "sys/fs/cgroup/cpu/job/cpu.cfs_quota_us", "150000");
        require(cpu_threads_for({0, 2, 4, 6}, root) == 1);
        require(automatic_cpu_threads() >= 1);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    fs::remove_all(root);
    std::cout << "CPU topology, affinity subsets and hierarchical v1/v2 quotas passed\n";
}
