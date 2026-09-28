#pragma once
#include "support/json.hpp"
#include "transcript/options.hpp"
#include "transcript/types.hpp"

namespace wt::checkpoint_detail {
void validate_existing_outputs(const Job& job, const CheckpointOptions& options, const Json& state);
} // namespace wt::checkpoint_detail
