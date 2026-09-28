#pragma once

namespace wt {
struct Job;
struct RenderOptions;
class Journal;
void publish_outputs(const Job& job, const RenderOptions& options, Journal& journal,
                     bool overwrite);
} // namespace wt
