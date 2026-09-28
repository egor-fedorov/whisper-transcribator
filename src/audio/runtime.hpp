#pragma once
#include "support/json.hpp"
#include <string>

namespace wt {
std::string audio_backend_version();
void configure_audio_logging(bool verbose = false);
Json audio_diagnostics();
} // namespace wt
