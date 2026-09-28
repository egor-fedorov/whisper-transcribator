#include "transcript/checkpoint/journal.hpp"
#include "platform/file.hpp"
#include "support/hash.hpp"
#include "support/io.hpp"
#include "transcript/checkpoint/detail/privacy.hpp"
#include "transcript/checkpoint/detail/publication.hpp"
#include "transcript/checkpoint/detail/records.hpp"
#include "transcript/checkpoint/detail/recovery.hpp"
#include "transcript/checkpoint/detail/storage.hpp"
#include "transcript/metadata.hpp"
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <utility>

namespace wt {
fs::path checkpoint_path(const Job& job) {
    return job.outputs.begin()->second.parent_path() / ".whisper-transcribator" /
           sha256_text(job_destinations(job).dump());
}
bool has_checkpoint(const Job& job) {
    auto path = checkpoint_path(job);
    return fs::exists(path) || fs::is_symlink(path);
}
struct Journal::Lock {
    platform::File file;
    Lock(const fs::path& path, checkpoint_detail::Privacy& privacy)
        : file(platform::open_private(path)) {
        auto st = file ? platform::status(file) : std::nullopt;
        if (!st || st->type != platform::FileType::regular || !privacy.accepts(path, *st, true) ||
            st->links != 1 || platform::try_lock(file) != platform::Lock::acquired)
            throw std::runtime_error("Checkpoint is busy or lock is unsafe: " + path.string());
    }
};
Journal::Journal(const Job& job, const CheckpointOptions& options, const Json& fingerprint,
                 const Json& output_metadata)
    : directory(checkpoint_path(job)) {
    privacy = std::make_unique<checkpoint_detail::Privacy>(directory.parent_path().parent_path());
    checkpoint_detail::private_directory(directory.parent_path(), *privacy);
    checkpoint_detail::reject_legacy_outputs(job);
    lock = std::make_unique<Lock>(directory.string() + ".lock", *privacy);
    auto restored =
        checkpoint_detail::restore_checkpoint(directory, options, fingerprint, *privacy);
    persisted = restored.has_value();
    if (restored)
        state = std::move(*restored);
    else
        state = {{"schema_version", 3},
                 {"fingerprint", fingerprint},
                 {"chunks", 0},
                 {"output_metadata", output_metadata},
                 {"samples", 0},
                 {"languages", Json::array()},
                 {"last_hash", ""},
                 {"finished", false},
                 {"published_hashes", Json::object()}};
    checkpoint_detail::validate_existing_outputs(job, options, state);
}
Journal::~Journal() = default;
int64_t Journal::samples() const { return state.at("samples").get<int64_t>(); }
std::vector<std::string> Journal::languages() const {
    return state.at("languages").get<std::vector<std::string>>();
}
std::string Journal::language() const {
    auto values = languages();
    return values.size() == 1 ? values.front() : "";
}
bool Journal::finished() const { return state.at("finished").get<bool>(); }
const Json& Journal::output_metadata() const { return state.at("output_metadata"); }
fs::path Journal::chunk_path(int64_t index) const {
    return directory / ("chunk-" + std::to_string(index) + ".json");
}
void Journal::save() {
    checkpoint_detail::write_record(directory / "manifest.json", state);
    sync_directory(directory.parent_path());
}
void Journal::append(int64_t count, const Transcript& transcript) {
    auto limit =
        state.at("fingerprint").at("run").at("chunk_seconds").get<int>() * int64_t(sample_rate);
    if (finished() || count <= 0 || count > limit)
        throw std::runtime_error("Invalid checkpoint advance");
    Json segments = Json::array();
    for (auto segment : transcript.segments) {
        segment.text = trim(segment.text);
        if (segment.text.empty())
            continue;
        checkpoint_detail::validate_segment(segment, 0, count / double(sample_rate));
        segment.start += samples() / double(sample_rate);
        segment.end += samples() / double(sample_rate);
        // Avoid a rounding excess at a chunk's final sample.
        segment.end = std::min(segment.end, (samples() + count) / double(sample_rate));
        segments.push_back({{"start", segment.start},
                            {"end", segment.end},
                            {"text", segment.text},
                            {"no_speech_prob", segment.no_speech_probability}});
    }
    auto next_languages = languages();
    std::string chunk_language;
    if (!segments.empty()) {
        if (transcript.language.empty())
            throw std::runtime_error("Missing chunk language");
        chunk_language = transcript.language;
        checkpoint_detail::add_language(next_languages, chunk_language);
    }
    auto index = state.at("chunks").get<int64_t>();
    Json record = {{"index", index},
                   {"start_sample", samples()},
                   {"end_sample", samples() + count},
                   {"previous_hash", state.at("last_hash")},
                   {"language", chunk_language},
                   {"segments", segments}};
    if (!persisted) {
        checkpoint_detail::private_directory(directory, *privacy);
        save();
        persisted = true;
    }
    checkpoint_detail::write_record(chunk_path(index), record);
    state["last_hash"] = sha256_text(record.dump());
    state["samples"] = samples() + count;
    state["chunks"] = index + 1;
    state["languages"] = next_languages;
    save();
}
void Journal::finish() {
    if (!persisted || !samples())
        throw std::runtime_error("Cannot finish an empty checkpoint");
    state["finished"] = true;
    save();
}
void Journal::visit(const std::function<void(const Segment&)>& consumer) const {
    checkpoint_detail::visit_records(directory, state, *privacy, consumer);
}
} // namespace wt
