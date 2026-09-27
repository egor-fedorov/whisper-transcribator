#pragma once
#include <filesystem>
#include <vector>

namespace wt::platform {
// Physical cores within Linux process affinity and cgroup CPU quotas, or macOS performance cores.
int automatic_cpu_threads();
// Linux: cores of the `allowed` CPUs within the quotas visible under the filesystem at `root`.
int cpu_threads_for(const std::vector<int>& allowed, const std::filesystem::path& root);
} // namespace wt::platform
