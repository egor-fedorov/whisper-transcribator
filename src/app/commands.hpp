#pragma once
#include "support/json.hpp"

namespace wt {
struct Options;
Json doctor(const Options& options);
int transcribe(Options options);
} // namespace wt
