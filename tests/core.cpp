#include "models/models.hpp"
#include "support/cancel.hpp"
#include "support/hash.hpp"
#include "support/io.hpp"
#include "support/options.hpp"
#include "transcript/jobs.hpp"
#include "transcript/journal.hpp"
#include "transcript/metadata.hpp"
#include "transcript/outputs.hpp"
#include <cmath>
#include <fcntl.h>
#include <iostream>
#include <limits>
#include <sys/file.h>
#include <unistd.h>

using namespace wt;
namespace {
int count = 0;
void require(bool value) {
    if (!value)
        throw std::runtime_error("assertion failed");
}
template <class F> void rejects(F action) {
    bool failed = false;
    try {
        action();
    } catch (const std::exception&) {
        failed = true;
    }
    require(failed);
}
struct Temporary {
    fs::path path;
    Temporary() {
        char name[] = "/tmp/whisper-test-XXXXXX";
        auto created = mkdtemp(name);
        if (!created)
            throw std::runtime_error("mkdtemp failed");
        path = created;
    }
    ~Temporary() {
        std::error_code error;
        fs::remove_all(path, error);
    }
};
template <class F> void test(const char* name, F action) {
    try {
        Temporary directory;
        action(directory.path);
        ++count;
    } catch (...) {
        std::cerr << "FAILED: " << name << '\n';
        throw;
    }
}
Options input(const fs::path& root, const std::string& name = "a.mp4") {
    atomic_write(root / name, "media");
    Options o;
    o.inputs = {(root / name).string()};
    return o;
}
Model fixture() {
    return {"fixture", "fixture.bin", "https://example.invalid/model",
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 3};
}
void write_outputs(const Job& job, const Options& options, const Transcript& result) {
    Journal journal(job, options, job_fingerprint(job, options, Json::object()));
    journal.append(static_cast<int64_t>(std::llround(result.duration * sample_rate)), result);
    journal.finish();
    publish_outputs(job, options, journal);
}
} // namespace
int main() {
    test("unicode filenames and transcript", [](auto root) {
        auto o = input(root, u8"\u041b\u0435\u043a\u0446\u0438\u044f (1).mp4");
        auto job = prepare_jobs(o)[0];
        write_outputs(job, o, {"ru", 1, {{0, 1, u8"\u0422\u0435\u043a\u0441\u0442.", 0}}});
        require(read_text(job.outputs.at("text")) == u8"\u0422\u0435\u043a\u0441\u0442.\n");
    });
    test("atomic no clobber and replacement", [](auto root) {
        auto path = root / "result";
        atomic_write(path, "original");
        rejects([&] { atomic_write(path, "new"); });
        require(read_text(path) == "original");
        atomic_write(path, "new", true);
        require(read_text(path) == "new");
        require(std::distance(fs::directory_iterator(root), fs::directory_iterator{}) == 1);
    });
    test("cancellation preserves output", [](auto root) {
        auto path = root / "result";
        atomic_write(path, "old");
        stop_signal = SIGTERM;
        bool stopped = false;
        try {
            atomic_write(path, "new", true);
        } catch (const Cancelled&) {
            stopped = true;
        }
        stop_signal = 0;
        require(stopped && read_text(path) == "old");
        require(std::distance(fs::directory_iterator(root), fs::directory_iterator{}) == 1);
    });
    test("source protection", [](auto root) {
        auto o = input(root);
        o.output = o.inputs.front();
        o.overwrite = true;
        rejects([&] { prepare_jobs(o); });
    });
    test("hardlink protection", [](auto root) {
        auto o = input(root);
        fs::create_hard_link(o.inputs[0], root / "a.txt");
        o.overwrite = true;
        rejects([&] { prepare_jobs(o); });
    });
    test("symlink protection", [](auto root) {
        auto o = input(root);
        fs::create_symlink(o.inputs[0], root / "a.txt");
        o.overwrite = true;
        rejects([&] { prepare_jobs(o); });
    });
    test("duplicate stems", [](auto root) {
        auto o = input(root);
        atomic_write(root / "a.wav", "wav");
        o.inputs.push_back((root / "a.wav").string());
        rejects([&] { prepare_jobs(o); });
    });
    test("output symlinks are rejected before skip and overwrite", [](auto root) {
        auto o = input(root);
        atomic_write(root / "target", "preserve");
        fs::create_symlink(root / "target", root / "a.txt");
        for (bool explicit_output : {false, true}) {
            o.output = explicit_output ? (root / "a.txt").string() : "";
            for (bool overwrite : {false, true}) {
                o.overwrite = overwrite;
                o.skip_existing = true;
                rejects([&] { prepare_jobs(o); });
            }
        }
        require(read_text(root / "target") == "preserve");
        fs::remove(root / "a.txt");
        fs::create_symlink(root / "missing", root / "a.txt");
        rejects([&] { prepare_jobs(o); });
    });
    test("directory symlinks and output inode collisions", [](auto root) {
        auto o = input(root);
        fs::create_directory(root / "out");
        fs::create_directory_symlink(root / "out", root / "alias");
        o.output_dir = (root / "alias").string();
        require(prepare_jobs(o)[0].outputs.at("text") == root / "out/a.txt");
        atomic_write(root / "b.mp4", "media");
        o.inputs.push_back((root / "b.mp4").string());
        atomic_write(root / "out/a.txt", "old");
        fs::create_hard_link(root / "out/a.txt", root / "out/b.txt");
        o.overwrite = true;
        rejects([&] { prepare_jobs(o); });
    });
    test("numbered mapping symlink is rejected", [](auto root) {
        auto o = input(root);
        o.naming = "numbered";
        o.output_dir = root.string();
        prepare_jobs(o);
        fs::rename(root / "result_files.json", root / "mapping");
        fs::create_symlink(root / "mapping", root / "result_files.json");
        rejects([&] { prepare_jobs(o); });
    });
    test("output versus another input", [](auto root) {
        auto o = input(root);
        atomic_write(root / "a.txt", "text input");
        o.inputs.push_back((root / "a.txt").string());
        o.overwrite = true;
        rejects([&] { prepare_jobs(o); });
    });
    test("existing outputs are not silently skipped", [](auto root) {
        auto o = input(root);
        atomic_write(root / "a.txt", "text");
        rejects([&] { prepare_jobs(o); });
        o.skip_existing = true;
        require(prepare_jobs(o).empty());
        o.overwrite = true;
        require(prepare_jobs(o).size() == 1);
    });
    test("completed results do not need directory write access", [](auto root) {
        auto o = input(root);
        atomic_write(root / "a.txt", "complete");
        o.skip_existing = true;
        fs::permissions(root, fs::perms::owner_read | fs::perms::owner_exec);
        try {
            require(prepare_jobs(o).empty());
        } catch (...) {
            fs::permissions(root, fs::perms::owner_all);
            throw;
        }
        fs::permissions(root, fs::perms::owner_all);
    });
    test("empty output is not complete", [](auto root) {
        auto o = input(root);
        atomic_write(root / "a.txt", "");
        o.skip_existing = true;
        rejects([&] { prepare_jobs(o); });
    });
    test("all requires complete set", [](auto root) {
        auto o = input(root);
        o.output_dir = root.string();
        o.format = "all";
        o.skip_existing = true;
        atomic_write(root / "a.txt", "text");
        rejects([&] { prepare_jobs(o); });
        atomic_write(root / "a.srt", "srt");
        atomic_write(root / "a.json", "{}");
        rejects([&] { prepare_jobs(o); });
        atomic_write(root / "a.vtt", "WEBVTT");
        require(prepare_jobs(o).empty());
    });
    test("explicit ordering", [](auto root) {
        auto o = input(root, "z.mp4");
        atomic_write(root / "a.wav", "a");
        o.inputs.push_back((root / "a.wav").string());
        require(prepare_jobs(o)[0].source.filename() == "z.mp4");
    });
    test("directory ordering and discovery", [](auto root) {
        input(root, "z.mp4");
        input(root, "a.MP4");
        input(root, "ignored.txt");
        fs::create_directory(root / "nested");
        input(root / "nested");
        Options o;
        o.input_dir = root.string();
        auto jobs = prepare_jobs(o);
        require(jobs.size() == 2);
        require(jobs[0].source.filename() == "a.MP4");
    });
    test("numbered mapping stability", [](auto root) {
        auto o = input(root);
        o.naming = "numbered";
        o.output_dir = (root / "out").string();
        auto jobs = prepare_jobs(o);
        require(jobs[0].outputs.at("text").filename() == "result_001.txt");
        auto path = root / "out/result_files.json";
        auto mapping = read_text(path);
        require(prepare_jobs(o).size() == 1 && read_text(path) == mapping);
        atomic_write(root / "b.mp4", "b");
        o.inputs.insert(o.inputs.begin(), (root / "b.mp4").string());
        o.overwrite = true;
        rejects([&] { prepare_jobs(o); });
        require(read_text(path) == mapping);
    });
    test("numbered orphan results", [](auto root) {
        auto o = input(root);
        o.naming = "numbered";
        o.output_dir = root.string();
        o.skip_existing = true;
        atomic_write(root / "result_001.txt", "text");
        rejects([&] { prepare_jobs(o); });
    });
    test("numbered mapping ignores object key order", [](auto root) {
        auto o = input(root);
        o.naming = "numbered";
        o.output_dir = root.string();
        o.format = "all";
        prepare_jobs(o);
        auto path = root / "result_files.json";
        nlohmann::json mapping = nlohmann::json::parse(read_text(path));
        atomic_write(path, mapping.dump(), true);
        require(prepare_jobs(o).size() == 1);
    });
    test("invalid option combinations", [](auto root) {
        auto o = input(root);
        o.input_dir = root.string();
        rejects([&] { prepare_jobs(o); });
        o.input_dir.clear();
        o.format = "all";
        rejects([&] { prepare_jobs(o); });
        o.format = "text";
        o.prefix = "../escape";
        rejects([&] { prepare_jobs(o); });
    });
    test("missing input and empty directory", [](auto root) {
        Options o;
        o.inputs = {(root / "absent").string()};
        rejects([&] { prepare_jobs(o); });
        o.inputs.clear();
        o.input_dir = root.string();
        require(prepare_jobs(o).empty());
    });
    test("render outputs once", [](auto root) {
        auto o = input(root);
        o.output_dir = root.string();
        o.format = "all";
        auto job = prepare_jobs(o)[0];
        Transcript result{"en", 2, {{0, 1.234, " Hello.", 0.1}, {1.234, 2, " World! ", 0.2}}};
        write_outputs(job, o, result);
        require(read_text(root / "a.txt") == "Hello. World!\n");
        require(read_text(root / "a.srt") ==
                "1\n00:00:00,000 --> 00:00:01,234\nHello.\n\n2\n00:00:01,234 --> "
                "00:00:02,000\nWorld!\n\n");
        auto data = Json::parse(read_text(root / "a.json"));
        require(data["schema_version"] == 2 && data["segments"].size() == 2);
        require(data["text"] == "Hello. World!" && data["run"]["backend"] == "whisper.cpp");
    });
    test("empty transcript never publishes output", [](auto root) {
        auto o = input(root);
        auto job = prepare_jobs(o)[0];
        rejects([&] { write_outputs(job, o, {"en", 1, {{0, 1, "  ", 0}}}); });
        require(!fs::exists(root / "a.txt"));
    });
    test("invalid timestamps", [](auto root) {
        auto o = input(root);
        auto job = prepare_jobs(o)[0];
        rejects([&] { write_outputs(job, o, {"en", 2, {{2, 1, "bad", 0}}}); });
        rejects([&] { timestamp(std::numeric_limits<double>::infinity()); });
        require(timestamp(3661.234) == "01:01:01,234");
    });
    test("download hash and offline reuse", [](auto root) {
        int calls = 0;
        auto fetch = [&](const auto&, const auto& path) {
            ++calls;
            atomic_write(path, "abc", true);
        };
        auto path = ensure_cached(fixture(), root, false, fetch);
        require(path.hash == fixture().hash && sha256(path.path) == path.hash && calls == 1);
        require(ensure_cached(fixture(), root, true, fetch).path == path.path && calls == 1);
        require(ensure_cached(fixture(), root, false, fetch).path == path.path && calls == 1);
    });
    test("offline missing has no side effects", [](auto root) {
        auto absent = root / "absent";
        rejects([&] {
            ensure_cached(fixture(), absent, true,
                          [](auto&, auto&) { throw std::logic_error("network"); });
        });
        require(!fs::exists(absent));
    });
    test("corrupt download not published", [](auto root) {
        rejects([&] {
            ensure_cached(fixture(), root, false,
                          [](const auto&, const auto& path) { atomic_write(path, "xyz", true); });
        });
        require(!fs::exists(root / fixture().file));
        for (const auto& item : fs::directory_iterator(root))
            require(item.path().extension() == ".lock");
    });
    test("corrupt cache not deleted", [](auto root) {
        atomic_write(root / fixture().file, "bad");
        rejects([&] { ensure_cached(fixture(), root, true); });
        rejects([&] { ensure_cached(fixture(), root, false); });
        require(read_text(root / fixture().file) == "bad");
    });
    test("cached model does not acquire download lock", [](auto root) {
        int fd = open((root / "fixture.bin.lock").c_str(), O_CREAT | O_RDWR, 0600);
        require(fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0);
        atomic_write(root / fixture().file, "abc");
        require(ensure_cached(fixture(), root, false).hash == fixture().hash);
        close(fd);
    });
    test("local model and invalid names", [](auto root) {
        Options o;
        o.local_files_only = true;
        o.download_root = root.string();
        atomic_write(root / "local.bin", "weights");
        auto prepared = prepare_model((root / "local.bin").string(), o);
        require(prepared.path == root / "local.bin" && prepared.hash == sha256(prepared.path));
        rejects([&] { prepare_model(root.string(), o); });
        rejects([&] { prepare_model("unknown", o); });
    });
    test("catalog and listing no downloads", [](auto root) {
        Options o;
        o.download_root = (root / "absent").string();
        auto items = list_models(o);
        require(items.size() == 9 && !fs::exists(o.download_root));
        for (const auto& model : model_catalog())
            require(model.hash.size() == 64 && model.bytes > 0);
    });
    std::cout << count << " core tests passed\n";
}
