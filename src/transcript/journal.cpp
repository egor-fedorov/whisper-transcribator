#include "transcript/journal.hpp"
#include "support/cancel.hpp"
#include "support/fd.hpp"
#include "support/hash.hpp"
#include "support/io.hpp"
#include "support/options.hpp"
#include "support/report.hpp"
#include "transcript/metadata.hpp"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <fcntl.h>
#include <iostream>
#include <optional>
#include <regex>
#include <set>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace wt {
// Checkpoint entries must be owned by this user with owner-only modes. Filesystems without POSIX
// permissions (FAT, exFAT, some FUSE/SMB mounts) report a fixed owner and mode from mount
// options, so a failed check is accepted only for attributes a probe file in the output
// directory cannot keep either, and only on that filesystem. Where an entry's filesystem ignores
// ownership (macOS external volumes), every user appears to own it, so the owner check alone
// never accepts it. Entry types, symlinks and hard links are always checked.
struct CheckpointPrivacy {
    fs::path output_directory;
    std::optional<StoredPermissions> stored;
    dev_t device = 0;
    explicit CheckpointPrivacy(fs::path directory) : output_directory(std::move(directory)) {}
    bool accepts(const fs::path& path, const struct stat& st, bool owner_only) {
        if (st.st_uid == geteuid() && owner_only && !ownership_ignored(path))
            return true;
        if (!stored) {
            struct stat output {};
            stored = stat(output_directory.c_str(), &output) ? StoredPermissions{}
                                                             : probe_permissions(output_directory);
            device = output.st_dev;
            static std::set<fs::path> warned;
            if ((!stored->owner || !stored->mode) && warned.insert(output_directory).second)
                log_message(
                    LogLevel::warning,
                    "Checkpoint privacy cannot be enforced on this filesystem because it "
                    "does not store POSIX owners and modes; saved transcript fragments in " +
                        (output_directory / ".whisper-transcribator").string() +
                        " are protected only by its mount options");
        }
        return st.st_dev == device && stored->accepts(st, owner_only);
    }
};
namespace {
void private_directory(const fs::path& path, CheckpointPrivacy& privacy) {
    bool created = mkdir(path.c_str(), 0700) == 0;
    if (!created && errno != EEXIST)
        throw std::runtime_error("Cannot create checkpoint directory: " + path.string());
    struct stat st {};
    if (lstat(path.c_str(), &st) || !S_ISDIR(st.st_mode) ||
        !privacy.accepts(path, st, !(st.st_mode & 0077)))
        throw std::runtime_error("Checkpoint directory must be owned by you with mode 0700: " +
                                 path.string());
    if (created)
        sync_directory(path.parent_path());
}
Json read_record(const fs::path& path, CheckpointPrivacy& privacy) {
    UniqueFd fd(open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK));
    if (fd.get() < 0)
        throw std::runtime_error("Cannot read checkpoint: " + path.string());
    struct stat st {};
    if (fstat(fd.get(), &st) || !S_ISREG(st.st_mode) || !privacy.accepts(path, st, true) ||
        st.st_size < 0 || st.st_size > 16 * 1024 * 1024)
        throw std::runtime_error("Invalid checkpoint file: " + path.string());
    std::string bytes(static_cast<size_t>(st.st_size), '\0');
    size_t offset = 0;
    while (offset < bytes.size()) {
        check_cancelled();
        auto n = read(fd.get(), bytes.data() + offset, bytes.size() - offset);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            throw std::runtime_error("Checkpoint read failed");
        offset += static_cast<size_t>(n);
    }
    return Json::parse(bytes);
}
void write_record(const fs::path& path, const Json& record) {
    auto bytes = record.dump();
    if (bytes.size() > 16 * 1024 * 1024)
        throw std::runtime_error("Checkpoint record exceeds size limit");
    atomic_write_stream(path, [&](auto& out) { out << bytes; }, true, true);
}
void validate_segment(const Segment& segment, double from, double to) {
    if (!std::isfinite(segment.start) || !std::isfinite(segment.end) ||
        !std::isfinite(segment.no_speech_probability) || segment.start < from ||
        segment.end < segment.start || segment.end > to)
        throw std::runtime_error("Invalid checkpoint segment");
}
void add_language(std::vector<std::string>& languages, const std::string& language) {
    if (language.empty())
        return;
    if (language.size() > 16 || !std::all_of(language.begin(), language.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || c == '-';
        }))
        throw std::runtime_error("Invalid checkpoint language");
    if (std::find(languages.begin(), languages.end(), language) == languages.end())
        languages.push_back(language);
    if (languages.size() > 256)
        throw std::runtime_error("Too many checkpoint languages");
}
bool exists_entry(const fs::path& path) { return fs::exists(path) || fs::is_symlink(path); }
void discard_checkpoint(const fs::path& directory) {
    fs::remove_all(directory);
    sync_directory(directory.parent_path());
}
void validate_manifest(const Json& state) {
    if (state.at("schema_version") != 3)
        throw std::runtime_error("Incompatible checkpoint schema; finish with the previous "
                                 "binary or restart explicitly with --overwrite");
    if (!state.at("fingerprint").is_object() || !state.at("chunks").is_number_integer() ||
        state.at("chunks").get<int64_t>() < 0 || !state.at("samples").is_number_integer() ||
        state.at("samples").get<int64_t>() < 0 || !state.at("languages").is_array() ||
        !state.at("finished").is_boolean() || !state.at("published_hashes").is_object() ||
        !state.at("output_metadata").at("model").is_string() ||
        !state.at("output_metadata").at("run").is_object())
        throw std::runtime_error("Invalid checkpoint manifest");
}
void visit_records(const fs::path& directory, const Json& state, CheckpointPrivacy& privacy,
                   const std::function<void(const Segment&)>& consumer) {
    int64_t position = 0;
    std::string hash;
    std::vector<std::string> detected;
    auto limit =
        state.at("fingerprint").at("run").at("chunk_seconds").get<int>() * int64_t(sample_rate);
    for (int64_t i = 0; i < state.at("chunks").get<int64_t>(); ++i) {
        check_cancelled();
        auto record = read_record(directory / ("chunk-" + std::to_string(i) + ".json"), privacy);
        auto end = record.at("end_sample").get<int64_t>();
        auto lang = record.at("language").get<std::string>();
        if (record.at("index") != i || record.at("start_sample") != position || end <= position ||
            end - position > limit || record.at("previous_hash") != hash ||
            !record.at("segments").is_array())
            throw std::runtime_error("Corrupt checkpoint sequence");
        for (const auto& item : record.at("segments")) {
            Segment segment{item.at("start").get<double>(), item.at("end").get<double>(),
                            item.at("text").get<std::string>(),
                            item.at("no_speech_prob").get<double>(), lang};
            validate_segment(segment, position / double(sample_rate), end / double(sample_rate));
            if (segment.text.empty() || lang.empty())
                throw std::runtime_error("Corrupt checkpoint text");
            consumer(segment);
        }
        if (record.at("segments").empty() && !lang.empty())
            throw std::runtime_error("Language attached to a silent chunk");
        add_language(detected, lang);
        hash = sha256_text(record.dump());
        position = end;
    }
    if (position != state.at("samples").get<int64_t>() || hash != state.at("last_hash") ||
        detected != state.at("languages").get<std::vector<std::string>>())
        throw std::runtime_error("Checkpoint commit mismatch");
}
bool safe_temporary(const fs::path& path, CheckpointPrivacy& privacy) {
    static const std::regex pattern("\\.whisper-output-[A-Za-z0-9]{6}");
    if (!std::regex_match(path.filename().string(), pattern))
        return false;
    struct stat st {};
    if (lstat(path.c_str(), &st) || !S_ISREG(st.st_mode) || st.st_nlink != 1 ||
        !privacy.accepts(path, st, (st.st_mode & 0777) == 0600) || st.st_size < 0 ||
        st.st_size > 16 * 1024 * 1024)
        throw std::runtime_error("Unsafe checkpoint temporary file: " + path.string());
    return true;
}
void validate_empty_checkpoint(const fs::path& directory, const Json& state,
                               CheckpointPrivacy& privacy) {
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
std::optional<Json> restore_checkpoint(const fs::path& directory, const Options& options,
                                       const Json& fingerprint, CheckpointPrivacy& privacy) {
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
void validate_existing_outputs(const Job& job, const Options& options, const Json& state) {
    for (const auto& [format, path] : job.outputs) {
        if (!exists_entry(path))
            continue;
        if (!fs::is_regular_file(path) || fs::is_symlink(path))
            throw std::runtime_error("Unsafe output: " + path.string());
        const auto& hashes = state.at("published_hashes");
        if (options.resume && state.at("finished").get<bool>() && hashes.contains(format) &&
            sha256(path) == hashes.at(format).get<std::string>())
            continue;
        if (!options.overwrite)
            throw std::runtime_error("Output exists and is not a verified resumed result; use "
                                     "--overwrite: " +
                                     path.string());
    }
}
} // namespace
fs::path checkpoint_path(const Job& job) {
    return job.outputs.begin()->second.parent_path() / ".whisper-transcribator" /
           sha256_text(job_destinations(job).dump());
}
bool has_checkpoint(const Job& job) {
    auto path = checkpoint_path(job);
    return fs::exists(path) || fs::is_symlink(path);
}
struct Journal::Lock {
    UniqueFd fd;
    Lock(const fs::path& path, CheckpointPrivacy& privacy)
        : fd(open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600)) {
        struct stat st {};
        if (fd.get() < 0 || fstat(fd.get(), &st) || !S_ISREG(st.st_mode) ||
            !privacy.accepts(path, st, true) || st.st_nlink != 1 ||
            flock(fd.get(), LOCK_EX | LOCK_NB))
            throw std::runtime_error("Checkpoint is busy or lock is unsafe: " + path.string());
    }
};
Journal::Journal(const Job& job, const Options& options, const Json& fingerprint)
    : directory(checkpoint_path(job)) {
    privacy = std::make_unique<CheckpointPrivacy>(directory.parent_path().parent_path());
    private_directory(directory.parent_path(), *privacy);
    reject_legacy_outputs(job);
    lock = std::make_unique<Lock>(directory.string() + ".lock", *privacy);
    auto restored = restore_checkpoint(directory, options, fingerprint, *privacy);
    persisted = restored.has_value();
    if (restored)
        state = std::move(*restored);
    else
        state = {{"schema_version", 3},
                 {"fingerprint", fingerprint},
                 {"chunks", 0},
                 {"output_metadata", {{"model", options.model}, {"run", run_metadata(options)}}},
                 {"samples", 0},
                 {"languages", Json::array()},
                 {"last_hash", ""},
                 {"finished", false},
                 {"published_hashes", Json::object()}};
    validate_existing_outputs(job, options, state);
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
    write_record(directory / "manifest.json", state);
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
        validate_segment(segment, 0, count / double(sample_rate));
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
        add_language(next_languages, chunk_language);
    }
    auto index = state.at("chunks").get<int64_t>();
    Json record = {{"index", index},
                   {"start_sample", samples()},
                   {"end_sample", samples() + count},
                   {"previous_hash", state.at("last_hash")},
                   {"language", chunk_language},
                   {"segments", segments}};
    if (!persisted) {
        private_directory(directory, *privacy);
        save();
        persisted = true;
    }
    write_record(chunk_path(index), record);
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
    visit_records(directory, state, *privacy, consumer);
}
void Journal::publish(const Job& job, const Options& options, const RenderOutput& render) {
    if (!finished())
        throw std::runtime_error("Cannot publish unfinished transcript");
    Json hashes = Json::object();
    for (const auto& [format, path] : job.outputs) {
        auto staged = directory / ("output." + format);
        atomic_write_stream(
            staged, [&, format = format](auto& out) { render(out, format); }, true, true);
        hashes[format] = sha256(staged);
    }
    if (!state.at("published_hashes").empty() && hashes != state.at("published_hashes"))
        throw std::runtime_error("Reconstructed outputs differ from checkpoint");
    state["published_hashes"] = hashes;
    save();
    for (const auto& [format, path] : job.outputs) {
        check_cancelled();
        if (fs::exists(path) || fs::is_symlink(path)) {
            if (!fs::is_regular_file(path) || fs::is_symlink(path) || same_file(path, job.source))
                throw std::runtime_error("Unsafe output: " + path.string());
            if (sha256(path) == hashes.at(format).get<std::string>())
                continue;
            if (!options.overwrite)
                throw std::runtime_error("Output changed during transcription: " + path.string());
        }
        publish_file(directory / ("output." + format), path, options.overwrite);
    }
    // Retire the checkpoint atomically before deleting its individual records.
    auto completed = fs::path(directory.string() + ".completed");
    fs::rename(directory, completed);
    sync_directory(directory.parent_path());
    fs::remove_all(completed);
    sync_directory(directory.parent_path());
}
} // namespace wt
