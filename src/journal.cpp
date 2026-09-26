#include "pipeline.hpp"
#include "version.hpp"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <fcntl.h>
#include <iostream>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

namespace wt {
namespace {
void private_directory(const fs::path& path) {
    bool created = mkdir(path.c_str(), 0700) == 0;
    if (!created && errno != EEXIST)
        throw std::runtime_error("Cannot create checkpoint directory: " + path.string());
    struct stat st {};
    if (lstat(path.c_str(), &st) || !S_ISDIR(st.st_mode) || st.st_uid != geteuid() ||
        (st.st_mode & 0077))
        throw std::runtime_error("Checkpoint directory must be owned by you with mode 0700: " +
                                 path.string());
    if (created)
        sync_directory(path.parent_path());
}
Json read_record(const fs::path& path) {
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0)
        throw std::runtime_error("Cannot read checkpoint: " + path.string());
    try {
        struct stat st {};
        if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() || st.st_size < 0 ||
            st.st_size > 16 * 1024 * 1024)
            throw std::runtime_error("Invalid checkpoint file: " + path.string());
        std::string bytes(static_cast<size_t>(st.st_size), '\0');
        size_t offset = 0;
        while (offset < bytes.size()) {
            check_cancelled();
            auto n = read(fd, bytes.data() + offset, bytes.size() - offset);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                throw std::runtime_error("Checkpoint read failed");
            offset += static_cast<size_t>(n);
        }
        auto result = Json::parse(bytes);
        close(fd);
        return result;
    } catch (...) {
        close(fd);
        throw;
    }
}
void write_record(const fs::path& path, const Json& record) {
    auto bytes = record.dump();
    if (bytes.size() > 16 * 1024 * 1024)
        throw std::runtime_error("Checkpoint record exceeds size limit");
    atomic_write_stream(path, [&](auto& out) { out << bytes; }, true, true);
}
Json destinations(const Job& job) {
    Json result = Json::object();
    for (const auto& [format, path] : job.outputs)
        result[format] = resolve_path(path).string();
    return result;
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
} // namespace
fs::path checkpoint_path(const Job& job) {
    return job.outputs.begin()->second.parent_path() / ".whisper-transcribator" /
           sha256_text(destinations(job).dump());
}
bool has_checkpoint(const Job& job) {
    auto path = checkpoint_path(job);
    return fs::exists(path) || fs::is_symlink(path);
}
Json job_fingerprint(const Job& job, const Options& options, const Json& backend) {
    auto size = fs::file_size(job.source);
    auto modified = fs::last_write_time(job.source);
    auto hash = sha256(job.source);
    if (size != fs::file_size(job.source) || modified != fs::last_write_time(job.source))
        throw std::runtime_error("Input changed while hashing");
    return {{"source", job.source.string()},
            {"size", size},
            {"sha256", hash},
            {"outputs", destinations(job)},
            {"run", run_metadata(options)},
            {"language", options.language},
            {"model", options.model},
            {"backend", backend}};
}
struct Journal::Lock {
    int fd = -1;
    explicit Lock(const fs::path& path) {
        fd = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
        struct stat st {};
        if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
            st.st_nlink != 1 || flock(fd, LOCK_EX | LOCK_NB)) {
            if (fd >= 0)
                close(fd);
            throw std::runtime_error("Checkpoint is busy or lock is unsafe: " + path.string());
        }
    }
    ~Lock() { close(fd); }
};
Journal::Journal(const Job& job, const Options& options, const Json& fingerprint)
    : directory(checkpoint_path(job)) {
    private_directory(directory.parent_path());
    lock = std::make_unique<Lock>(directory.string() + ".lock");
    auto completed = fs::path(directory.string() + ".completed");
    if (fs::exists(completed) || fs::is_symlink(completed)) {
        private_directory(completed);
        fs::remove_all(completed);
        sync_directory(directory.parent_path());
    }
    if (has_checkpoint(job)) {
        private_directory(directory);
        if (options.overwrite && !options.resume) {
            fs::remove_all(directory);
            sync_directory(directory.parent_path());
        }
    }
    auto manifest = directory / "manifest.json";
    if (fs::exists(manifest) || fs::is_symlink(manifest)) {
        state = read_record(manifest);
        if (state.at("schema_version") != 2)
            throw std::runtime_error("Incompatible checkpoint schema; finish with the previous "
                                     "binary or restart explicitly with --overwrite");
        if (!state.at("fingerprint").is_object() || !state.at("chunks").is_number_integer() ||
            state.at("chunks").get<int64_t>() < 0 || !state.at("samples").is_number_integer() ||
            samples() < 0 || !state.at("languages").is_array() ||
            !state.at("finished").is_boolean() || !state.at("published_hashes").is_object())
            throw std::runtime_error("Invalid checkpoint manifest");
        visit([](const Segment&) {});
        if (state.at("chunks") == 0 && !finished() && state.at("published_hashes").empty()) {
            // Only a known, uncommitted first record may accompany an empty manifest.
            for (const auto& entry : fs::directory_iterator(directory)) {
                if (entry.path() == manifest)
                    continue;
                if (entry.path() != chunk_path(0))
                    throw std::runtime_error("Unknown empty checkpoint contents; use --overwrite");
                auto record = read_record(entry.path());
                auto empty = state;
                state["chunks"] = 1;
                state["samples"] = record.at("end_sample");
                state["languages"] = Json::array();
                if (!record.at("language").get<std::string>().empty())
                    state["languages"].push_back(record.at("language"));
                state["last_hash"] = sha256_text(record.dump());
                visit([](const Segment&) {});
                state = empty;
            }
            fs::remove_all(directory);
            sync_directory(directory.parent_path());
        } else {
            if (!options.resume)
                throw std::runtime_error("Saved progress exists; use --resume or --overwrite: " +
                                         directory.string());
            if (nlohmann::json(state.at("fingerprint")) != nlohmann::json(fingerprint))
                throw std::runtime_error(
                    "Checkpoint is incompatible with input, models or settings; "
                    "use --overwrite without --resume to restart");
            persisted = true;
        }
    } else if (has_checkpoint(job)) {
        if (!fs::is_empty(directory))
            throw std::runtime_error("Checkpoint manifest missing; use --overwrite to restart");
        fs::remove(directory);
        sync_directory(directory.parent_path());
    }
    if (!persisted) {
        state = {{"schema_version", 2}, {"fingerprint", fingerprint},        {"chunks", 0},
                 {"samples", 0},        {"languages", Json::array()},        {"last_hash", ""},
                 {"finished", false},   {"published_hashes", Json::object()}};
    }
    for (const auto& [format, path] : job.outputs) {
        if (!fs::exists(path) && !fs::is_symlink(path))
            continue;
        if (!fs::is_regular_file(path) || fs::is_symlink(path))
            throw std::runtime_error("Unsafe output: " + path.string());
        const auto& hashes = state.at("published_hashes");
        if (options.resume && finished() && hashes.contains(format) &&
            sha256(path) == hashes.at(format).get<std::string>())
            continue;
        if (!options.overwrite)
            throw std::runtime_error("Output exists and is not a verified resumed result; use "
                                     "--overwrite: " +
                                     path.string());
    }
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
        private_directory(directory);
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
    int64_t position = 0;
    std::string hash;
    std::vector<std::string> detected;
    auto limit =
        state.at("fingerprint").at("run").at("chunk_seconds").get<int>() * int64_t(sample_rate);
    for (int64_t i = 0; i < state.at("chunks").get<int64_t>(); ++i) {
        check_cancelled();
        auto record = read_record(chunk_path(i));
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
    if (position != samples() || hash != state.at("last_hash") || detected != languages())
        throw std::runtime_error("Checkpoint commit mismatch");
}
void Journal::publish(const Job& job, const Options& options) {
    if (!finished())
        throw std::runtime_error("Cannot publish unfinished transcript");
    Json hashes = Json::object();
    for (const auto& [format, path] : job.outputs) {
        auto staged = directory / ("output." + format);
        atomic_write_stream(
            staged,
            [&, format = format](auto& out) { render_stream(out, format, job, options, *this); },
            true, true);
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
