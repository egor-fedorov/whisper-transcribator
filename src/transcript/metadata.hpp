#pragma once
#include "support/json.hpp"

namespace wt {
struct Options;
struct Job;
constexpr int chunking_version = 4;
Json run_metadata(const Options& options);
Json job_destinations(const Job& job);
Json job_fingerprint(const Job& job, const Options& options, const Json& backend);
} // namespace wt
