#pragma once
#include "support/json.hpp"
#include "transcript/options.hpp"
#include "transcript/types.hpp"
#include <optional>

namespace wt::checkpoint_detail {
class Privacy;
std::optional<Json> restore_checkpoint(const fs::path& directory, const CheckpointOptions& options,
                                       const Json& fingerprint, Privacy& privacy);
void reject_legacy_outputs(const Job& job);
} // namespace wt::checkpoint_detail
