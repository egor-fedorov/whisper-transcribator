#include "transcript/jobs.hpp"
#include "support/atomic.hpp"
#include "support/error.hpp"
#include "support/files.hpp"
#include "support/fixtures/jobs.hpp"
#include "support/json.hpp"
#include "support/platform/permissions.hpp"
#include "support/test.hpp"

using namespace wt;
using namespace wt::test;
namespace {
void source_protection(const fs::path& root) {

    auto o = input(root);
    o.jobs.output = o.jobs.inputs.front();
    o.checkpoint.overwrite = true;
    rejects([&] { prepare_jobs(o.jobs, o.checkpoint); }, "Output would overwrite input");
}
void hardlink_protection(const fs::path& root) {

    auto o = input(root);
    fs::create_hard_link(o.jobs.inputs[0], root / "a.txt");
    o.checkpoint.overwrite = true;
    rejects([&] { prepare_jobs(o.jobs, o.checkpoint); }, "Output would overwrite input");
}
void symlink_protection(const fs::path& root) {

    auto o = input(root);
    fs::create_symlink(o.jobs.inputs[0], root / "a.txt");
    o.checkpoint.overwrite = true;
    rejects([&] { prepare_jobs(o.jobs, o.checkpoint); }, "Unsafe output symlink");
}
void duplicate_stems(const fs::path& root) {

    auto o = input(root);
    atomic_write(root / "a.wav", "wav");
    o.jobs.inputs.push_back((root / "a.wav").string());
    rejects([&] { prepare_jobs(o.jobs, o.checkpoint); }, "Output collision");
}
void outputs_published_earlier_in_the_run(const fs::path& root) {
    Job first{root / "Lecture.wav", {{"text", root / "Lecture.txt"}}};
    Job second{root / "lecture.wav", {{"text", root / "lecture.txt"}}};
    Job unrelated{root / "other.wav", {{"text", root / "other.txt"}}};
    PublishedOutputs published;
    published.check(first);
    atomic_write(root / "Lecture.txt", "first");
    published.record(first);
    // A hard link stands in for a filesystem that ignores letter case, as macOS does by default.
    if (!fs::exists(root / "lecture.txt"))
        fs::create_hard_link(root / "Lecture.txt", root / "lecture.txt");
    rejects([&] { published.check(second); },
            "Output collision: " + (root / "lecture.txt").string() + " is the same file as " +
                (root / "Lecture.txt").string());
    // Files not published by this run are left to the existing-output rules of planning.
    atomic_write(root / "other.txt", "existing");
    published.check(unrelated);
}
void output_symlinks_are_rejected_before_skip_and_overwrite(const fs::path& root) {

    auto o = input(root);
    atomic_write(root / "target", "preserve");
    fs::create_symlink(root / "target", root / "a.txt");
    for (bool explicit_output : {false, true}) {
        o.jobs.output = explicit_output ? (root / "a.txt").string() : "";
        for (bool overwrite : {false, true}) {
            o.checkpoint.overwrite = overwrite;
            o.jobs.skip_existing = true;
            rejects([&] { prepare_jobs(o.jobs, o.checkpoint); }, "Unsafe output symlink");
        }
    }
    require(read_text(root / "target") == "preserve");
    fs::remove(root / "a.txt");
    fs::create_symlink(root / "missing", root / "a.txt");
    rejects([&] { prepare_jobs(o.jobs, o.checkpoint); }, "Unsafe output symlink");
}
void directory_symlinks_and_output_inode_collisions(const fs::path& root) {

    auto o = input(root);
    fs::create_directory(root / "out");
    fs::create_directory_symlink(root / "out", root / "alias");
    o.jobs.output_dir = (root / "alias").string();
    require(prepare_jobs(o.jobs, o.checkpoint)[0].outputs.at("text") == root / "out/a.txt");
    atomic_write(root / "b.mp4", "media");
    o.jobs.inputs.push_back((root / "b.mp4").string());
    atomic_write(root / "out/a.txt", "old");
    fs::create_hard_link(root / "out/a.txt", root / "out/b.txt");
    o.checkpoint.overwrite = true;
    rejects([&] { prepare_jobs(o.jobs, o.checkpoint); }, "Output collision");
}
void numbered_mapping_symlink_is_rejected(const fs::path& root) {

    auto o = input(root);
    o.jobs.naming = "numbered";
    o.jobs.output_dir = root.string();
    prepare_jobs(o.jobs, o.checkpoint);
    fs::rename(root / "result_files.json", root / "mapping");
    fs::create_symlink(root / "mapping", root / "result_files.json");
    rejects([&] { prepare_jobs(o.jobs, o.checkpoint); }, "Unsafe output symlink");
}
void output_versus_another_input(const fs::path& root) {

    auto o = input(root);
    atomic_write(root / "a.txt", "text input");
    o.jobs.inputs.push_back((root / "a.txt").string());
    o.checkpoint.overwrite = true;
    rejects([&] { prepare_jobs(o.jobs, o.checkpoint); }, "Output would overwrite input");
}
void existing_outputs_are_not_silently_skipped(const fs::path& root) {

    auto o = input(root);
    atomic_write(root / "a.txt", "text");
    rejects([&] { prepare_jobs(o.jobs, o.checkpoint); }, "Output exists; use --overwrite");
    o.jobs.skip_existing = true;
    require(prepare_jobs(o.jobs, o.checkpoint).empty());
    o.checkpoint.overwrite = true;
    require(prepare_jobs(o.jobs, o.checkpoint).size() == 1);
}
void completed_results_do_not_need_directory_write_access(const fs::path& root) {

    auto o = input(root);
    atomic_write(root / "a.txt", "complete");
    o.jobs.skip_existing = true;
    wt::test::permissions(root, fs::perms::owner_read | fs::perms::owner_exec);
    try {
        require(prepare_jobs(o.jobs, o.checkpoint).empty());
    } catch (...) {
        wt::test::permissions(root, fs::perms::owner_all);
        throw;
    }
    wt::test::permissions(root, fs::perms::owner_all);
}
void empty_output_is_not_complete(const fs::path& root) {

    auto o = input(root);
    atomic_write(root / "a.txt", "");
    o.jobs.skip_existing = true;
    rejects([&] { prepare_jobs(o.jobs, o.checkpoint); }, "Output exists; use --overwrite");
}
void all_requires_complete_set(const fs::path& root) {

    auto o = input(root);
    o.jobs.output_dir = root.string();
    o.jobs.format = "all";
    o.jobs.skip_existing = true;
    atomic_write(root / "a.txt", "text");
    rejects([&] { prepare_jobs(o.jobs, o.checkpoint); }, "Output exists; use --overwrite");
    atomic_write(root / "a.srt", "srt");
    atomic_write(root / "a.json", "{}");
    rejects([&] { prepare_jobs(o.jobs, o.checkpoint); }, "Output exists; use --overwrite");
    atomic_write(root / "a.vtt", "WEBVTT");
    require(prepare_jobs(o.jobs, o.checkpoint).empty());
}
void explicit_ordering(const fs::path& root) {

    auto o = input(root, "z.mp4");
    atomic_write(root / "a.wav", "a");
    o.jobs.inputs.push_back((root / "a.wav").string());
    require(prepare_jobs(o.jobs, o.checkpoint)[0].source.filename() == "z.mp4");
}
void directory_ordering_and_discovery(const fs::path& root) {

    input(root, "z.mp4");
    input(root, "a.MP4");
    input(root, "ignored.txt");
    fs::create_directory(root / "nested");
    input(root / "nested");
    CliOptions o;
    o.jobs.input_dir = root.string();
    auto jobs = prepare_jobs(o.jobs, o.checkpoint);
    require(jobs.size() == 2);
    require(jobs[0].source.filename() == "a.MP4");
    for (const auto* extension : {"ts", "MTS", "m2ts", "mka", "aif", "3gp", "asf"})
        input(root, std::string("file-") + extension + "." + extension);
    jobs = prepare_jobs(o.jobs, o.checkpoint);
    require(jobs.size() == 9);
    for (size_t i = 1; i < jobs.size(); ++i)
        require(jobs[i - 1].source.filename().native() < jobs[i].source.filename().native());
}
void numbered_mapping_stability(const fs::path& root) {

    auto o = input(root);
    o.jobs.naming = "numbered";
    o.jobs.output_dir = (root / "out").string();
    auto jobs = prepare_jobs(o.jobs, o.checkpoint);
    require(jobs[0].outputs.at("text").filename() == "result_001.txt");
    auto path = root / "out/result_files.json";
    auto mapping = read_text(path);
    require(prepare_jobs(o.jobs, o.checkpoint).size() == 1 && read_text(path) == mapping);
    atomic_write(root / "b.mp4", "b");
    o.jobs.inputs.insert(o.jobs.inputs.begin(), (root / "b.mp4").string());
    o.checkpoint.overwrite = true;
    rejects([&] { prepare_jobs(o.jobs, o.checkpoint); }, "Input list changed");
    require(read_text(path) == mapping);
}
void numbered_orphan_results(const fs::path& root) {

    auto o = input(root);
    o.jobs.naming = "numbered";
    o.jobs.output_dir = root.string();
    o.jobs.skip_existing = true;
    atomic_write(root / "result_001.txt", "text");
    rejects([&] { prepare_jobs(o.jobs, o.checkpoint); },
            "Numbered results exist without their mapping");
}
void numbered_mapping_ignores_object_key_order(const fs::path& root) {

    auto o = input(root);
    o.jobs.naming = "numbered";
    o.jobs.output_dir = root.string();
    o.jobs.format = "all";
    prepare_jobs(o.jobs, o.checkpoint);
    auto path = root / "result_files.json";
    nlohmann::json mapping = nlohmann::json::parse(read_text(path));
    atomic_write(path, mapping.dump(), true);
    require(prepare_jobs(o.jobs, o.checkpoint).size() == 1);
}
void invalid_option_combinations(const fs::path& root) {

    auto o = input(root);
    o.jobs.input_dir = root.string();
    rejects<UsageError>([&] { prepare_jobs(o.jobs, o.checkpoint); }, "Use files or --input-dir");
    o.jobs.input_dir.clear();
    o.jobs.format = "all";
    rejects<UsageError>([&] { prepare_jobs(o.jobs, o.checkpoint); }, "--format all requires");
    o.jobs.format = "text";
    o.jobs.prefix = "../escape";
    rejects<UsageError>([&] { prepare_jobs(o.jobs, o.checkpoint); }, "Invalid --prefix");
}
void missing_input_and_empty_directory(const fs::path& root) {

    CliOptions o;
    o.jobs.inputs = {(root / "absent").string()};
    rejects([&] { prepare_jobs(o.jobs, o.checkpoint); }, "Input file not found");
    o.jobs.inputs.clear();
    o.jobs.input_dir = root.string();
    require(prepare_jobs(o.jobs, o.checkpoint).empty());
}
void invalid_plan_does_not_prepare_outputs(const fs::path& root) {
    auto options = input(root);
    atomic_write(root / "a.wav", "second input");
    options.jobs.inputs.push_back((root / "a.wav").string());
    options.jobs.output_dir = (root / "not-created").string();
    rejects([&] { prepare_jobs(options.jobs, options.checkpoint); }, "Output collision");
    require(!fs::exists(options.jobs.output_dir));
}
void mapping_validation_precedes_write_probe(const fs::path& root) {
    auto options = input(root);
    options.jobs.naming = "numbered";
    options.jobs.output_dir = (root / "out").string();
    prepare_jobs(options.jobs, options.checkpoint);
    auto mapping = read_text(root / "out/result_files.json");
    atomic_write(root / "b.mp4", "other input");
    options.jobs.inputs.push_back((root / "b.mp4").string());
    wt::test::permissions(root / "out", fs::perms::owner_read | fs::perms::owner_exec);
    try {
        rejects([&] { prepare_jobs(options.jobs, options.checkpoint); }, "Input list changed");
    } catch (...) {
        wt::test::permissions(root / "out", fs::perms::owner_all);
        throw;
    }
    wt::test::permissions(root / "out", fs::perms::owner_all);
    require(read_text(root / "out/result_files.json") == mapping);
}
} // namespace
int main() {
    return run_tests({
        {"invalid plan leaves no output directories", invalid_plan_does_not_prepare_outputs},
        {"mapping is validated before checking write access",
         mapping_validation_precedes_write_probe},
        {"source protection", source_protection},
        {"hardlink protection", hardlink_protection},
        {"symlink protection", symlink_protection},
        {"duplicate stems", duplicate_stems},
        {"outputs published earlier in the run", outputs_published_earlier_in_the_run},
        {"output symlinks are rejected before skip and overwrite",
         output_symlinks_are_rejected_before_skip_and_overwrite},
        {"directory symlinks and output inode collisions",
         directory_symlinks_and_output_inode_collisions},
        {"numbered mapping symlink is rejected", numbered_mapping_symlink_is_rejected},
        {"output versus another input", output_versus_another_input},
        {"existing outputs are not silently skipped", existing_outputs_are_not_silently_skipped},
        {"completed results do not need directory write access",
         completed_results_do_not_need_directory_write_access},
        {"empty output is not complete", empty_output_is_not_complete},
        {"all requires complete set", all_requires_complete_set},
        {"explicit ordering", explicit_ordering},
        {"directory ordering and discovery", directory_ordering_and_discovery},
        {"numbered mapping stability", numbered_mapping_stability},
        {"numbered orphan results", numbered_orphan_results},
        {"numbered mapping ignores object key order", numbered_mapping_ignores_object_key_order},
        {"invalid option combinations", invalid_option_combinations},
        {"missing input and empty directory", missing_input_and_empty_directory},
    });
}
