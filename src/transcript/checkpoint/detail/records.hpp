#pragma once
#include "support/json.hpp"
#include "transcript/types.hpp"
#include <functional>

namespace wt::checkpoint_detail {
class Privacy;
void validate_segment(const Segment& segment, double from, double to);
void add_language(std::vector<std::string>& languages, const std::string& language);
void validate_manifest(const Json& state);
void visit_records(const fs::path& directory, const Json& state, Privacy& privacy,
                   const std::function<void(const Segment&)>& consumer);
} // namespace wt::checkpoint_detail
