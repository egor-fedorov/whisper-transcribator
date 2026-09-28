#include "transcript/journal.hpp"
#include "support/fixtures.hpp"
#include "support/io.hpp"
#include "support/test.hpp"
#include "transcript/metadata.hpp"

using namespace wt;
using namespace wt::test;
namespace {
void early_failures_leave_no_saved_progress(const fs::path& root) {
    Fixture early(root / "early");
    {
        Journal journal(early.job, early.options.checkpoint, early.fingerprint(),
                        describe_output(early.options));
        Audio audio;
        rejects([&] { run(early, journal, audio, 0); }, "injected inference failure");
        require(!has_checkpoint(early.job));
        rejects(
            [&] {
                Journal busy(early.job, early.options.checkpoint, early.fingerprint(),
                             describe_output(early.options));
            },
            "Checkpoint is busy or lock is unsafe");
    }
    atomic_write(early.job.source, "replacement input", true);
    {
        Journal journal(early.job, early.options.checkpoint, early.fingerprint(),
                        describe_output(early.options));
        require(!has_checkpoint(early.job));
    }
}
void empty_checkpoint_recovery_and_unknown_contents(const fs::path& root) {
    Fixture zero(root / "zero");
    auto zero_path = checkpoint_path(zero.job);
    {
        Journal journal(zero.job, zero.options.checkpoint, zero.fingerprint(),
                        describe_output(zero.options));
        journal.append(16, {"en", 0.001, {{0, 0.001, "orphan", 0}}});
    }
    auto zero_manifest = Json::parse(read_text(zero_path / "manifest.json"));
    zero_manifest["chunks"] = zero_manifest["samples"] = 0;
    zero_manifest["languages"] = Json::array();
    zero_manifest["last_hash"] = "";
    atomic_write(zero_path / "manifest.json", zero_manifest.dump(), true);
    atomic_write(zero_path / "unknown", "preserve");
    rejects(
        [&] {
            Journal journal(zero.job, zero.options.checkpoint, zero.fingerprint(),
                            describe_output(zero.options));
        },
        "Unknown empty checkpoint contents");
    require(read_text(zero_path / "unknown") == "preserve");
    fs::remove(zero_path / "unknown");
    auto orphan = read_text(zero_path / "chunk-0.json");
    auto invalid = Json::parse(orphan);
    invalid["start_sample"] = 1;
    atomic_write(zero_path / "chunk-0.json", invalid.dump(), true);
    rejects(
        [&] {
            Journal journal(zero.job, zero.options.checkpoint, zero.fingerprint(),
                            describe_output(zero.options));
        },
        "Corrupt checkpoint sequence");
    require(read_text(zero_path / "manifest.json") == zero_manifest.dump());
    require(read_text(zero_path / "chunk-0.json") == invalid.dump());
    atomic_write(zero_path / "chunk-0.json", orphan, true);
    atomic_write(zero.job.source, "replacement", true);
    {
        Journal journal(zero.job, zero.options.checkpoint, zero.fingerprint(),
                        describe_output(zero.options));
        require(!has_checkpoint(zero.job));
    }
    fs::create_directory(zero_path);
    wt::test::permissions(zero_path, fs::perms::owner_all);
    {
        Journal journal(zero.job, zero.options.checkpoint, zero.fingerprint(),
                        describe_output(zero.options));
        require(!has_checkpoint(zero.job));
    }
}
void incompatible_and_corrupted_checkpoints(const fs::path& root) {
    Fixture corrupt(root / "corrupt");
    auto original = corrupt.fingerprint();
    {
        Journal journal(corrupt.job, corrupt.options.checkpoint, original,
                        describe_output(corrupt.options));
        journal.append(16, {"en", 0.001, {{0, 0.001, "text", 0}}});
    }
    corrupt.options.checkpoint.resume = true;
    corrupt.options.checkpoint.overwrite = true;
    for (const auto& field : {"source", "sha256", "backend", "run", "language", "model"}) {
        auto changed = original;
        changed[field] = "changed";
        rejects(
            [&] {
                Journal journal(corrupt.job, corrupt.options.checkpoint, changed,
                                describe_output(corrupt.options));
            },
            "Checkpoint is incompatible");
    }
    corrupt.options.checkpoint.overwrite = false;
    auto modified = fs::last_write_time(corrupt.job.source);
    atomic_write(corrupt.job.source, "same bytes", true);
    fs::last_write_time(corrupt.job.source, modified);
    rejects(
        [&] {
            Journal journal(corrupt.job, corrupt.options.checkpoint, corrupt.fingerprint(),
                            describe_output(corrupt.options));
        },
        "Checkpoint is incompatible");
    atomic_write(corrupt.job.source, "fake audio", true);
    {
        Journal journal(corrupt.job, corrupt.options.checkpoint, original,
                        describe_output(corrupt.options));
        Audio shortened;
        shortened.total = 8;
        rejects([&] { run(corrupt, journal, shortened); }, "Audio ends before checkpoint position");
        require(shortened.recognized == 0);
    }
    auto chunk = checkpoint_path(corrupt.job) / "chunk-0.json";
    auto manifest_path = checkpoint_path(corrupt.job) / "manifest.json";
    auto manifest_bytes = read_text(manifest_path);
    auto legacy = Json::parse(manifest_bytes);
    legacy["schema_version"] = 1;
    atomic_write(manifest_path, legacy.dump(), true);
    rejects(
        [&] {
            Journal journal(corrupt.job, corrupt.options.checkpoint, original,
                            describe_output(corrupt.options));
        },
        "Incompatible checkpoint schema");
    require(Json::parse(read_text(manifest_path))["schema_version"] == 1);
    atomic_write(manifest_path, manifest_bytes, true);
    auto saved = read_text(chunk);
    auto edited = Json::parse(saved);
    edited["segments"][0]["text"] = "tampered";
    atomic_write(chunk, edited.dump(), true);
    rejects(
        [&] {
            Journal journal(corrupt.job, corrupt.options.checkpoint, original,
                            describe_output(corrupt.options));
        },
        "Checkpoint commit mismatch");
    atomic_write(chunk, "{", true);
    rejects<Json::parse_error>(
        [&] {
            Journal journal(corrupt.job, corrupt.options.checkpoint, original,
                            describe_output(corrupt.options));
        },
        "parse error");
    atomic_write(chunk, saved, true);
    atomic_write(checkpoint_path(corrupt.job) / "chunk-1.json", "incomplete orphan");
    {
        Journal journal(corrupt.job, corrupt.options.checkpoint, original,
                        describe_output(corrupt.options));
        journal.append(16, {"en", 0.001, {{0, 0.001, "next", 0}}});
        journal.visit([](const auto&) {});
    }
    corrupt.options.checkpoint.resume = false;
    corrupt.options.checkpoint.overwrite = true;
    {
        Journal journal(corrupt.job, corrupt.options.checkpoint, original,
                        describe_output(corrupt.options));
        require(journal.samples() == 0);
    }
}
void write_failure_before_manifest_commit(const fs::path& root) {
    Fixture disk(root / "disk");
    auto manifest = checkpoint_path(disk.job) / "manifest.json";
    std::string saved_manifest;
    {
        Journal journal(disk.job, disk.options.checkpoint, disk.fingerprint(),
                        describe_output(disk.options));
        journal.append(16, {"en", 0.001, {{0, 0.001, "committed", 0}}});
        saved_manifest = read_text(manifest);
        fs::remove(manifest);
        fs::create_directory(manifest);
        rejects([&] { journal.append(16, {"en", 0.001, {{0, 0.001, "uncommitted", 0}}}); },
                "Cannot publish");
    }
    fs::remove(manifest);
    atomic_write(manifest, saved_manifest);
    disk.options.checkpoint.resume = true;
    {
        Journal journal(disk.job, disk.options.checkpoint, disk.fingerprint(),
                        describe_output(disk.options));
        require(journal.samples() == 16);
        Audio audio;
        run(disk, journal, audio);
    }
}
void checkpoint_symlink_rejection(const fs::path& root) {
    Fixture links(root / "links");
    fs::create_symlink(root, root / "links/.whisper-transcribator");
    rejects(
        [&] {
            Journal journal(links.job, links.options.checkpoint, links.fingerprint(),
                            describe_output(links.options));
        },
        "Checkpoint directory must be owned by you");
}
void legacy_output_set(const fs::path& root) {
    Fixture f(root);
    auto& options = f.options;
    auto& job = f.job;
    auto old_job = job;
    old_job.outputs.erase("vtt");
    {
        Journal old(old_job, options.checkpoint,
                    job_fingerprint(old_job, describe_run(options), options.inference.language,
                                    Json::object()),
                    describe_output(options));
        old.append(sample_rate, {"en", 1, {{0, 1, "saved", 0}}});
    }
    rejects(
        [&] {
            Journal newer(job, options.checkpoint,
                          job_fingerprint(job, describe_run(options), options.inference.language,
                                          Json::object()),
                          describe_output(options));
        },
        "Saved progress for the old three-format all exists");
    require(has_checkpoint(old_job));
}
void unsafe_temporaries(const fs::path& root) {
    Fixture f(root);
    auto directory = checkpoint_path(f.job);
    fs::create_directories(directory);
    wt::test::permissions(directory.parent_path(), fs::perms::owner_all);
    wt::test::permissions(directory, fs::perms::owner_all);
    auto temporary = directory / ".whisper-output-Ab123Z";
    atomic_write(root / "unrelated", "preserve");
    fs::create_symlink(root / "unrelated", temporary);
    rejects(
        [&] {
            Journal journal(f.job, f.options.checkpoint, f.fingerprint(),
                            describe_output(f.options));
        },
        "Unsafe checkpoint temporary");
    require(fs::is_symlink(temporary));
    fs::remove(temporary);
    fs::create_hard_link(root / "unrelated", temporary);
    rejects(
        [&] {
            Journal journal(f.job, f.options.checkpoint, f.fingerprint(),
                            describe_output(f.options));
        },
        "Unsafe checkpoint temporary");
    require(read_text(root / "unrelated") == "preserve" && fs::exists(temporary));
    fs::remove(temporary);
    atomic_write(directory / "unknown", "preserve");
    rejects(
        [&] {
            Journal journal(f.job, f.options.checkpoint, f.fingerprint(),
                            describe_output(f.options));
        },
        "Checkpoint manifest missing");
    require(read_text(directory / "unknown") == "preserve");
}
} // namespace
int main() {
    return run_tests({
        {"legacy three-format checkpoint rejection", legacy_output_set},
        {"unsafe temporary checkpoint files are preserved", unsafe_temporaries},
        {"early failures leave no saved progress", early_failures_leave_no_saved_progress},
        {"empty checkpoint recovery and unknown contents",
         empty_checkpoint_recovery_and_unknown_contents},
        {"incompatible and corrupted checkpoints", incompatible_and_corrupted_checkpoints},
        {"write failure before manifest commit", write_failure_before_manifest_commit},
        {"checkpoint symlink rejection", checkpoint_symlink_rejection},
    });
}
