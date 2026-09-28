#pragma once
#include "audio/options.hpp"
#include "inference/options.hpp"
#include "support/json.hpp"
#include "transcript/options.hpp"

namespace wt {
struct Job;
constexpr int chunking_version = 4;
Json run_metadata(const InferenceOptions& inference, const AudioOptions& audio,
                  const ChunkOptions& chunking, const RenderOptions& rendering,
                  const std::string& device);
Json job_destinations(const Job& job);
Json job_fingerprint(const Job& job, const Json& run, const std::string& language,
                     const Json& backend);
} // namespace wt
