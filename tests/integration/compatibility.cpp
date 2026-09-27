#include "support/hash.hpp"
#include "support/io.hpp"
#include "support/options.hpp"
#include "support/test.hpp"
#include "transcript/jobs.hpp"
#include "transcript/journal.hpp"
#include "transcript/metadata.hpp"
#include "transcript/outputs.hpp"

using namespace wt;
using namespace wt::test;
namespace {
void resume_snapshot(const fs::path& root, const std::string& scenario) {
    auto snapshot = Json::parse(read_text(fs::path(WT_TEST_FIXTURES) / "checkpoint-v2.json"));
    Options options;
    options.inputs = {(root / "source.wav").string()};
    options.output_dir = root.string();
    options.format = "all";
    options.language = "auto";
    options.resume = true;
    atomic_write(options.inputs[0], "deterministic input");
    auto job = prepare_jobs(options).at(0);
    auto manifest = snapshot.at(scenario).at("manifest");
    auto& fingerprint = manifest.at("fingerprint");
    fingerprint["source"] = job.source.string();
    fingerprint["outputs"] = job_destinations(job);
    require(fingerprint == job_fingerprint(job, options, {{"model_sha256", "fixture"}}),
            "Saved fingerprint contract changed");
    auto expected = snapshot.at("outputs").get<std::map<std::string, std::string>>();
    auto json = Json::parse(expected.at("json"));
    json["source"] = job.source.string();
    expected["json"] = json.dump() + "\n";
    if (scenario == "publication") {
        // Only the source path changes the JSON hash when relocating this frozen fixture.
        manifest["published_hashes"]["json"] = sha256_text(expected.at("json"));
        for (const auto& [format, bytes] : expected)
            require(manifest.at("published_hashes").at(format) == sha256_text(bytes));
        for (const auto* format : {"json", "srt"})
            atomic_write(job.outputs.at(format), expected.at(format));
    }
    auto directory = checkpoint_path(job);
    fs::create_directories(directory);
    fs::permissions(directory.parent_path(), fs::perms::owner_all);
    fs::permissions(directory, fs::perms::owner_all);
    atomic_write(directory / "manifest.json", manifest.dump());
    size_t index = 0;
    for (const auto& chunk : snapshot.at(scenario).at("chunks"))
        atomic_write(directory / ("chunk-" + std::to_string(index++) + ".json"),
                     chunk.get<std::string>());
    Journal journal(job, options, fingerprint);
    if (scenario == "tail") {
        require(journal.samples() == 10 * sample_rate && !journal.finished());
        journal.append(10 * sample_rate,
                       {"ru", 10, {{0, 2, "Second & <line>.", 0.2}, {5, 7, "Last line.", 0.3}}});
        journal.finish();
        require(read_text(directory / "chunk-1.json") ==
                    snapshot.at("publication").at("chunks").at(1).get<std::string>(),
                "New checkpoint record differs from the frozen writer");
    } else
        require(journal.finished() && journal.samples() == 20 * sample_rate);
    publish_outputs(job, options, journal);
    for (const auto& [format, path] : job.outputs)
        require(read_text(path) == expected.at(format), "Changed frozen " + format + " output");
    require(!has_checkpoint(job));
}
} // namespace
int main() {
    return run_tests({
        {"resume frozen schema-2 audio tail",
         [](const fs::path& root) { resume_snapshot(root, "tail"); }},
        {"resume frozen schema-2 partial publication",
         [](const fs::path& root) { resume_snapshot(root, "publication"); }},
    });
}
