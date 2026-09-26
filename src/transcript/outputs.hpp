#pragma once
#include <iosfwd>
#include <string>

namespace wt {
struct Options;
struct Job;
class Journal;
std::string timestamp(double seconds);
void render_stream(std::ostream& stream, const std::string& format, const Job& job,
                   const Options& options, const Journal& journal);
void publish_outputs(const Job& job, const Options& options, Journal& journal);
} // namespace wt
