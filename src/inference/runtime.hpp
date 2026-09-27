#pragma once
#include "support/json.hpp"
#include <string>

namespace wt {
void configure_inference_logging();
void validate_language(const std::string& language);
std::string inference_backend_version();
std::string select_device(const std::string& requested);
Json inference_diagnostics();
} // namespace wt
