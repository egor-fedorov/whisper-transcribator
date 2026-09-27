#include "support/fixtures.hpp"
#include "support/io.hpp"
#include "support/test.hpp"
#include "transcript/journal.hpp"
#include "transcript/metadata.hpp"
#include "transcript/outputs.hpp"

using namespace wt;
using namespace wt::test;
namespace {
void old_snapshot(const fs::path& root) {
    auto snapshot = Json::parse(read_text(fs::path(WT_TEST_FIXTURES) / "checkpoint-v2.json"));
    for (const auto* scenario : {"tail", "publication"}) {
        Fixture f(root / scenario);
        f.options.resume = true;
        auto directory = checkpoint_path(f.job);
        fs::create_directories(directory);
        fs::permissions(directory.parent_path(), fs::perms::owner_all);
        fs::permissions(directory, fs::perms::owner_all);
        auto manifest = snapshot.at(scenario).at("manifest");
        atomic_write(directory / "manifest.json", manifest.dump());
        size_t index = 0;
        for (const auto& chunk : snapshot.at(scenario).at("chunks"))
            atomic_write(directory / ("chunk-" + std::to_string(index++) + ".json"),
                         chunk.get<std::string>());
        rejects([&] { Journal journal(f.job, f.options, f.fingerprint()); },
                "Incompatible checkpoint schema");
        require(read_text(directory / "manifest.json") == manifest.dump());
        index = 0;
        for (const auto& chunk : snapshot.at(scenario).at("chunks"))
            require(read_text(directory / ("chunk-" + std::to_string(index++) + ".json")) ==
                    chunk.get<std::string>());
    }
}
void execution_settings(const fs::path& root) {
    Fixture f(root);
    f.options.model = "./weights.bin";
    f.options.cpu_threads = 8;
    auto saved = f.fingerprint();
    Json metadata;
    {
        Journal journal(f.job, f.options, saved);
        journal.append(16, {"en", 0.001, {{0, 0.001, "saved", 0}}});
        metadata = journal.output_metadata();
    }
    f.options.resume = true;
    for (const auto* name : {"/absolute/weights.bin", "large", "large-v3"}) {
        f.options.model = name;
        f.options.cpu_threads = 1;
        require(saved == f.fingerprint());
        Journal journal(f.job, f.options, f.fingerprint());
        require(journal.samples() == 16 && journal.output_metadata() == metadata);
    }
    for (const auto* key : {"audio_stream", "beam_size", "audio_timeline_version",
                            "rendering_version", "chunking_version"}) {
        auto changed = saved;
        changed["run"][key] = 999;
        rejects([&] { Journal journal(f.job, f.options, changed); }, std::string("/run/") + key);
    }
    auto changed = saved;
    changed["backend"]["model_sha256"] = "different weights";
    rejects([&] { Journal journal(f.job, f.options, changed); }, "/backend/model_sha256");
    f.options.timestamp_gaps = "preserve";
    rejects([&] { Journal journal(f.job, f.options, f.fingerprint()); }, "/run/timestamp_gaps");
}
Json install_snapshot(Fixture& f, const Json& snapshot) {
    atomic_write(f.job.source, "deterministic input", true);
    f.options.chunk_seconds = 120;
    f.options.resume = true;
    f.options.cpu_threads = 1;
    f.options.model = "/other/spelling/weights.bin";
    auto manifest = snapshot.at("manifest");
    manifest["fingerprint"]["source"] = f.job.source.string();
    manifest["fingerprint"]["outputs"] = job_destinations(f.job);
    auto directory = checkpoint_path(f.job);
    fs::create_directories(directory);
    fs::permissions(directory.parent_path(), fs::perms::owner_all);
    fs::permissions(directory, fs::perms::owner_all);
    atomic_write(directory / "manifest.json", manifest.dump());
    atomic_write(directory / "chunk-0.json", snapshot.at("chunk").get<std::string>());
    return manifest;
}
void old_timeline_snapshot(const fs::path& root) {
    auto snapshot = Json::parse(read_text(fs::path(WT_TEST_FIXTURES) / "checkpoint-v3.json"));
    Fixture f(root);
    auto manifest = install_snapshot(f, snapshot);
    rejects([&] { Journal journal(f.job, f.options, f.fingerprint()); },
            "/run/audio_timeline_version");
    auto directory = checkpoint_path(f.job);
    require(read_text(directory / "manifest.json") == manifest.dump());
    require(read_text(directory / "chunk-0.json") == snapshot.at("chunk"));
}
void current_snapshot(const fs::path& root) {
    auto snapshot =
        Json::parse(read_text(fs::path(WT_TEST_FIXTURES) / "checkpoint-v3-timeline-v2.json"));
    Fixture f(root);
    auto manifest = install_snapshot(f, snapshot);
    require(manifest.at("fingerprint") == f.fingerprint());
    Journal journal(f.job, f.options, f.fingerprint());
    require(journal.samples() == 160000 && !journal.finished());
    journal.finish();
    publish_outputs(f.job, f.options, journal);
    for (const auto* format : {"text", "srt", "vtt"})
        require(read_text(f.job.outputs.at(format)) == snapshot.at(format));
    auto json = Json::parse(read_text(f.job.outputs.at("json")));
    require(json.at("model") == manifest.at("output_metadata").at("model"));
    require(json.at("run") == manifest.at("output_metadata").at("run"));
    require(json.at("duration") == 10.0 && json.at("text") == "First line.");
}
} // namespace
int main() {
    return run_tests({{"preserve and reject schema-2 snapshots", old_snapshot},
                      {"preserve and reject previous timeline snapshots", old_timeline_snapshot},
                      {"resume independent schema-3 snapshot", current_snapshot},
                      {"execution settings are not inference identity", execution_settings}});
}
