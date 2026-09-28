#pragma once
#include "support/fs.hpp"
#include "support/json.hpp"

namespace wt::checkpoint_detail {
class Privacy;
Json read_record(const fs::path& path, Privacy& privacy);
void write_record(const fs::path& path, const Json& record);
bool exists_entry(const fs::path& path);
void discard_checkpoint(const fs::path& directory);
} // namespace wt::checkpoint_detail
