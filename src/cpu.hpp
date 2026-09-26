#pragma once
#include <filesystem>
#include <vector>

namespace wt {
int automatic_cpu_threads();
int cpu_threads_for(const std::vector<int>& allowed, const std::filesystem::path& root);
} // namespace wt
