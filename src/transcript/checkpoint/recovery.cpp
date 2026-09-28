#include "transcript/checkpoint/detail/recovery.hpp"
#include "support/hash.hpp"
#include "transcript/checkpoint/detail/privacy.hpp"
#include "transcript/checkpoint/detail/records.hpp"
#include "transcript/checkpoint/detail/storage.hpp"
#include "transcript/checkpoint/journal.hpp"
#include <stdexcept>

namespace wt::checkpoint_detail {
namespace {
void validate_empty_checkpoint(const fs::path& directory, const Json& state, Privacy& privacy) {
    // Validate a possible orphan against a candidate state, never the live journal.
    for (const auto& entry : fs::directory_iterator(directory)) {
        if (entry.path() == directory / "manifest.json")
            continue;
        if (safe_temporary(entry.path(), privacy))
            continue;
        if (entry.path() != directory / "chunk-0.json")
            throw std::runtime_error("Unknown empty checkpoint contents; use --overwrite");
        auto record = read_record(entry.path(), privacy);
        auto candidate = state;
        candidate["chunks"] = 1;
        candidate["samples"] = record.at("end_sample");
        candidate["languages"] = Json::array();
        if (!record.at("language").get<std::string>().empty())
            candidate["languages"].push_back(record.at("language"));
        candidate["last_hash"] = sha256_text(record.dump());
        visit_records(directory, candidate, privacy, [](const Segment&) {});
    }
}
} // namespace
std::optional<Json> restore_checkpoint(const fs::path& directory, const CheckpointOptions& options,
                                       const Json& fingerprint, Privacy& privacy) {
    auto completed = fs::path(directory.string() + ".completed");
    if (exists_entry(completed)) {
        private_directory(completed, privacy);
        discard_checkpoint(completed);
    }
    if (exists_entry(directory)) {
        private_directory(directory, privacy);
        if (options.overwrite && !options.resume)
            discard_checkpoint(directory);
    }
    auto manifest = directory / "manifest.json";
    if (!exists_entry(manifest)) {
        if (exists_entry(directory)) {
            for (const auto& entry : fs::directory_iterator(directory))
                if (!safe_temporary(entry.path(), privacy))
                    throw std::runtime_error(
                        "Checkpoint manifest missing; use --overwrite to restart");
            discard_checkpoint(directory);
        }
        return std::nullopt;
    }
    auto state = read_record(manifest, privacy);
    validate_manifest(state);
    visit_records(directory, state, privacy, [](const Segment&) {});
    if (state.at("chunks") == 0 && !state.at("finished").get<bool>() &&
        state.at("published_hashes").empty()) {
        validate_empty_checkpoint(directory, state, privacy);
        discard_checkpoint(directory);
        return std::nullopt;
    }
    if (!options.resume)
        throw std::runtime_error("Saved progress exists; use --resume or --overwrite: " +
                                 directory.string());
    auto differences = nlohmann::json::diff(state.at("fingerprint"), fingerprint);
    if (!differences.empty()) {
        std::string fields;
        for (const auto& difference : differences)
            fields += (fields.empty() ? "" : ", ") + difference.at("path").get<std::string>();
        throw std::runtime_error("Checkpoint is incompatible; changed fields: " + fields +
                                 ". Finish with the original settings/binary or explicitly "
                                 "restart with --overwrite without --resume");
    }
    return state;
}
void reject_legacy_outputs(const Job& job) {
    if (job.outputs.size() != 4 || !job.outputs.count("vtt"))
        return;
    auto legacy = job;
    legacy.outputs.erase("vtt");
    if (has_checkpoint(legacy))
        throw std::runtime_error("Saved progress for the old three-format all exists: " +
                                 checkpoint_path(legacy).string() +
                                 "; finish with the previous binary or explicitly move that "
                                 "checkpoint aside before restarting");
}
} // namespace wt::checkpoint_detail
