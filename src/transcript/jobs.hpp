#pragma once
#include "transcript/types.hpp"

namespace wt {
struct Options;
std::vector<Job> prepare_jobs(const Options& options);
} // namespace wt
