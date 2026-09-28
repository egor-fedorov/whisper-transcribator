#pragma once
#include "platform/file.hpp"
#include "transcript/options.hpp"
#include "transcript/types.hpp"

namespace wt {
std::vector<Job> prepare_jobs(const JobOptions& options, const CheckpointOptions& checkpoint);
// Outputs published earlier in this run, by file identity. Different names can be one file on
// filesystems that ignore letter case or Unicode normalization (APFS and HFS+ by default, exFAT,
// FAT), which planning cannot detect before the first of them exists.
class PublishedOutputs {
    std::map<platform::FileId, fs::path> files;

  public:
    // Rejects a job whose output already is the published output of an earlier job.
    void check(const Job& job) const;
    void record(const Job& job);
};
} // namespace wt
