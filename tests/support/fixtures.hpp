#pragma once
#include "models/models.hpp"
#include "support/json.hpp"
#include "support/options.hpp"
#include "transcript/types.hpp"

namespace wt {
class Journal;
}
namespace wt::test {
Options input(const fs::path& root, const std::string& name = "a.mp4");
Model model_fixture();
void write_outputs(const Job& job, const Options& options, const Transcript& result);
struct Fixture {
    fs::path root;
    Options options;
    Job job;
    explicit Fixture(const fs::path& directory);
    Json fingerprint() const;
};
struct Audio {
    int64_t total = 35, cursor = 0, recognized = 0;
    size_t peak = 0;
    std::vector<float> read(size_t limit);
    Transcript recognize(const std::vector<float>& pcm, const std::string& language);
};
void run(Fixture& fixture, Journal& journal, Audio& audio, int fail_at = -1);
} // namespace wt::test
