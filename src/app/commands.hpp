#pragma once
#include "support/json.hpp"

namespace wt {
struct CliOptions;
Json doctor(const CliOptions& options);
int transcribe(CliOptions options);
} // namespace wt
