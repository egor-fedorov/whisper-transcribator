#pragma once
#include "transcript/types.hpp"
#include <sys/types.h>
#include <utility>

namespace wt {
struct Options;
std::vector<Job> prepare_jobs(const Options& options);
// Outputs published earlier in this run, by file identity. Different names can be one file on
// filesystems that ignore letter case or Unicode normalization (APFS and HFS+ by default, exFAT,
// FAT), which planning cannot detect before the first of them exists.
class PublishedOutputs {
    std::map<std::pair<dev_t, ino_t>, fs::path> files;

  public:
    // Rejects a job whose output already is the published output of an earlier job.
    void check(const Job& job) const;
    void record(const Job& job);
};
} // namespace wt
