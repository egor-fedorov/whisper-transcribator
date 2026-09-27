#include "models/download.hpp"
#include "models/models.hpp"
#include "platform/file.hpp"
#include "support/fixtures.hpp"
#include "support/io.hpp"
#include "support/test.hpp"
#include "transcript/journal.hpp"
#include <iostream>
#include <sstream>

using namespace wt;
using namespace wt::test;
namespace {
StoredPermissions without_modes(const fs::path&) { return {true, false}; }
bool stores_permissions(const fs::path& directory) {
    auto stored = probe_permissions(directory);
    return stored.owner && stored.mode;
}
// Simulates a FAT/exFAT mount owned by this user, which reports mode 0777 for every entry. With
// TMPDIR on such a filesystem (tests/integration/exfat.sh) the real one is used instead.
class PermissionlessFilesystem {
    decltype(probe_permissions) previous = probe_permissions;

  public:
    bool simulated;
    explicit PermissionlessFilesystem(const fs::path& root) : simulated(stores_permissions(root)) {
        if (simulated)
            probe_permissions = without_modes;
    }
    ~PermissionlessFilesystem() { probe_permissions = previous; }
    PermissionlessFilesystem(const PermissionlessFilesystem&) = delete;
    PermissionlessFilesystem& operator=(const PermissionlessFilesystem&) = delete;
    // Applies the mount's fixed mode to entries this program created with private modes.
    void expose(const fs::path& path) const {
        if (!simulated)
            return;
        fs::permissions(path, fs::perms::all);
        if (fs::is_directory(path))
            for (const auto& entry : fs::recursive_directory_iterator(path))
                fs::permissions(entry.path(), fs::perms::all);
    }
};
size_t occurrences(const std::string& text, const std::string& value) {
    size_t count = 0;
    for (auto at = text.find(value); at != std::string::npos; at = text.find(value, at + 1))
        ++count;
    return count;
}
void checkpoints_resume_and_publish(const fs::path& root) {
    PermissionlessFilesystem filesystem(root);
    Fixture reference(root / "reference"), f(root / "job");
    Audio complete;
    {
        Journal journal(reference.job, reference.options, reference.fingerprint());
        run(reference, journal, complete);
    }
    auto checkpoints = root / "job/.whisper-transcribator";
    fs::create_directory(checkpoints);
    filesystem.expose(checkpoints);
    std::ostringstream log;
    {
        StreamCapture capture(std::cerr, log.rdbuf());
        {
            Journal journal(f.job, f.options, f.fingerprint());
            Audio audio;
            rejects([&] { run(f, journal, audio, 1); }, "injected inference failure");
        }
        require(fs::exists(checkpoint_path(f.job) / "chunk-0.json"));
        filesystem.expose(checkpoints);
        f.options.resume = true;
        Journal journal(f.job, f.options, f.fingerprint());
        require(journal.samples() > 0);
        Audio audio;
        run(f, journal, audio);
        require(audio.recognized < complete.recognized, "resume repeated committed inference");
    }
    require(occurrences(log.str(), "Checkpoint privacy cannot be enforced on this filesystem") == 1,
            "expected one privacy warning: " + log.str());
    require(occurrences(log.str(), checkpoint_path(f.job).parent_path().string()) == 1);
    for (const auto& format : {"text", "srt", "vtt"})
        require(read_text(f.job.outputs.at(format)) == read_text(reference.job.outputs.at(format)));
    require(fs::file_size(f.job.outputs.at("json")) > 0 && !has_checkpoint(f.job));
    rejects([&] { atomic_write(f.job.outputs.at("text"), "replacement"); }, "File exists");
}
void interrupted_first_write_is_recovered(const fs::path& root) {
    PermissionlessFilesystem filesystem(root);
    Fixture f(root);
    auto directory = checkpoint_path(f.job);
    fs::create_directories(directory);
    atomic_write(directory / ".whisper-output-Ab123Z", "{\"index\"");
    filesystem.expose(root / ".whisper-transcribator");
    {
        std::ostringstream log;
        StreamCapture capture(std::cerr, log.rdbuf());
        Journal journal(f.job, f.options, f.fingerprint());
        require(journal.samples() == 0 && !has_checkpoint(f.job));
    }
    fs::create_directories(directory);
    atomic_write(directory / "unknown", "preserve");
    filesystem.expose(directory);
    rejects([&] { Journal journal(f.job, f.options, f.fingerprint()); },
            "Checkpoint manifest missing");
    require(read_text(directory / "unknown") == "preserve");
}
void link_protections_remain(const fs::path& root) {
    PermissionlessFilesystem filesystem(root);
    // Such filesystems cannot create symlinks or hard links at all.
    if (!filesystem.simulated)
        return;
    std::ostringstream log;
    StreamCapture capture(std::cerr, log.rdbuf());
    Fixture links(root / "links");
    fs::create_directory(root / "elsewhere");
    fs::permissions(root / "elsewhere", fs::perms::all);
    fs::create_directory_symlink(root / "elsewhere", root / "links/.whisper-transcribator");
    rejects([&] { Journal journal(links.job, links.options, links.fingerprint()); },
            "Checkpoint directory must be owned by you");
    Fixture f(root / "hardlink");
    auto directory = checkpoint_path(f.job);
    fs::create_directories(directory);
    atomic_write(root / "unrelated", "preserve");
    fs::create_hard_link(root / "unrelated", directory / ".whisper-output-Ab123Z");
    filesystem.expose(root / "hardlink/.whisper-transcribator");
    rejects([&] { Journal journal(f.job, f.options, f.fingerprint()); },
            "Unsafe checkpoint temporary");
    require(read_text(root / "unrelated") == "preserve");
    auto partial = partial_model_path(model_fixture(), root / "models");
    fs::create_directories(partial.parent_path());
    fs::create_hard_link(root / "unrelated", partial);
    fs::permissions(partial, fs::perms::all);
    rejects([&] { open_partial_model(partial); }, "Unsafe partial model file");
}
void strict_checks_where_permissions_are_stored(const fs::path& root) {
    Fixture f(root);
    auto checkpoints = root / ".whisper-transcribator";
    fs::create_directory(checkpoints);
    auto partial = partial_model_path(model_fixture(), root / "models");
    fs::create_directories(partial.parent_path());
    atomic_write(partial, "a");
    // A mount for another owner refuses chmod but already reports mode 0777.
    std::error_code refused;
    fs::permissions(checkpoints, fs::perms::all, refused);
    fs::permissions(partial, fs::perms::all, refused);
    if (stores_permissions(root)) {
        rejects([&] { Journal journal(f.job, f.options, f.fingerprint()); },
                "Checkpoint directory must be owned by you with mode 0700");
        rejects([&] { open_partial_model(partial); }, "Unsafe partial model file");
    } else {
        std::ostringstream log;
        StreamCapture capture(std::cerr, log.rdbuf());
        Journal journal(f.job, f.options, f.fingerprint());
        open_partial_model(partial);
    }
    require(fs::exists(checkpoints) && read_text(partial) == "a");
}
void model_cache_downloads_and_publishes(const fs::path& root) {
    PermissionlessFilesystem filesystem(root);
    auto model = model_fixture();
    auto partial = partial_model_path(model, root);
    atomic_write(partial, "a");
    filesystem.expose(partial);
    int calls = 0;
    auto prepared = ensure_cached(model, root, false, [&](const Model&, const fs::path& path) {
        ++calls;
        auto file = open_partial_model(path);
        require(platform::seek_end(file) == uint64_t(1) && platform::write(file, "bc", 2) == 2);
    });
    require(calls == 1 && prepared.path == root / model.file && read_text(prepared.path) == "abc");
    require(!fs::exists(partial) && ensure_cached(model, root, true).hash == model.hash);
}
} // namespace
int main() {
    return run_tests({
        {"checkpoints resume and publish without POSIX permissions",
         checkpoints_resume_and_publish},
        {"interrupted first checkpoint write is recovered", interrupted_first_write_is_recovered},
        {"symlink and hard-link protections remain", link_protections_remain},
        {"strict checks where permissions are stored", strict_checks_where_permissions_are_stored},
        {"model cache downloads and publishes without POSIX permissions",
         model_cache_downloads_and_publishes},
    });
}
