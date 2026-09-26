#include "support/fixtures.hpp"
#include "support/io.hpp"
#include "support/test.hpp"
#include "transcript/jobs.hpp"
#include "transcript/journal.hpp"
#include "transcript/outputs.hpp"
#include <sstream>

using namespace wt;
using namespace wt::test;
namespace {
void partial_publication_and_external_edits(const fs::path& root) {
    Fixture publish(root / "publish");
    {
        Journal journal(publish.job, publish.options, publish.fingerprint());
        journal.append(16, {"en", 0.001, {{0, 0.001, "final", 0}}});
        journal.finish();
        fs::create_directory(publish.job.outputs.at("srt"));
        rejects([&] { publish_outputs(publish.job, publish.options, journal); });
        require(fs::exists(publish.job.outputs.at("json")));
        require(!fs::exists(publish.job.outputs.at("text")));
        fs::remove(publish.job.outputs.at("srt"));
    }
    auto json = read_text(publish.job.outputs.at("json"));
    atomic_write(publish.job.outputs.at("json"), "external edit", true);
    publish.options.resume = true;
    require(prepare_jobs(publish.options).size() == 1);
    rejects([&] { Journal journal(publish.job, publish.options, publish.fingerprint()); });
    atomic_write(publish.job.outputs.at("json"), json, true);
    {
        Journal journal(publish.job, publish.options, publish.fingerprint());
        require(journal.finished());
        Audio audio;
        run(publish, journal, audio);
        require(audio.cursor == 0 && audio.recognized == 0);
    }
}
void late_publication_failure(const fs::path& root) {
    Fixture f(root);
    auto& options = f.options;
    auto& job = f.job;
    require(job.outputs.size() == 4);
    auto fingerprint = f.fingerprint();
    {
        Journal journal(job, options, fingerprint);
        journal.append(5 * sample_rate, {"en", 5, {{0, 5, "First\nline.", 0}}});
    }
    std::map<std::string, std::string> expected;
    options.resume = true;
    {
        Journal journal(job, options, fingerprint);
        journal.append(
            10 * sample_rate,
            {"en", 10, {{0, 1, "Same paragraph.", 0}, {4, 4, "New & <tag> --> cue.", 0}}});
        journal.append(sample_rate,
                       {"ru", 1, {{0, 1, u8"\u041f\u0440\u0438\u0432\u0435\u0442.", 0}}});
        journal.finish();
        for (const auto& [format, path] : job.outputs) {
            std::ostringstream out;
            render_stream(out, format, job, options, journal);
            expected[format] = out.str();
        }
        fs::create_directory(job.outputs.at("vtt"));
        rejects([&] { publish_outputs(job, options, journal); });
        require(fs::exists(job.outputs.at("text")));
        fs::remove(job.outputs.at("vtt"));
    }
    Journal journal(job, options, fingerprint);
    require(journal.finished());
    publish_outputs(job, options, journal);
    for (const auto& [format, path] : job.outputs)
        require(read_text(path) == expected.at(format), "Changed resumed " + format);
}
} // namespace
int main() {
    return run_tests({
        {"late publication failure and all-format recovery", late_publication_failure},
        {"partial publication and external edits", partial_publication_and_external_edits},
    });
}
