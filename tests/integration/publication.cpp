#include "transcript/publication.hpp"
#include "support/atomic.hpp"
#include "support/files.hpp"
#include "support/fixtures.hpp"
#include "support/test.hpp"
#include "transcript/checkpoint/journal.hpp"
#include "transcript/jobs.hpp"
#include "transcript/render/render.hpp"
#include <sstream>

using namespace wt;
using namespace wt::test;
namespace {
void partial_publication_and_external_edits(const fs::path& root) {
    Fixture publish(root / "publish");
    {
        Journal journal(publish.job, publish.options.checkpoint, publish.fingerprint(),
                        describe_output(publish.options));
        journal.append(16, {"en", 0.001, {{0, 0.001, "final", 0}}});
        journal.finish();
        fs::create_directory(publish.job.outputs.at("srt"));
        rejects(
            [&] {
                publish_outputs(publish.job, publish.options.rendering, journal,
                                publish.options.checkpoint.overwrite);
            },
            "Unsafe output");
        require(fs::exists(publish.job.outputs.at("json")));
        require(!fs::exists(publish.job.outputs.at("text")));
        fs::remove(publish.job.outputs.at("srt"));
    }
    auto json = read_text(publish.job.outputs.at("json"));
    atomic_write(publish.job.outputs.at("json"), "external edit", true);
    publish.options.checkpoint.resume = true;
    publish.options.inference.cpu_threads = 1;
    publish.options.model = "/different/spelling/same-weights.bin";
    require(prepare_jobs(publish.options.jobs, publish.options.checkpoint).size() == 1);
    rejects(
        [&] {
            Journal journal(publish.job, publish.options.checkpoint, publish.fingerprint(),
                            describe_output(publish.options));
        },
        "Output exists and is not a verified resumed result");
    atomic_write(publish.job.outputs.at("json"), json, true);
    {
        Journal journal(publish.job, publish.options.checkpoint, publish.fingerprint(),
                        describe_output(publish.options));
        require(journal.finished());
        Audio audio;
        run(publish, journal, audio);
        require(audio.cursor == 0 && audio.recognized == 0);
    }
}
void late_publication_failure(const fs::path& root) {
    Fixture f(root);
    auto& options = f.options;
    auto& job = f.job;
    require(job.outputs.size() == 4);
    auto fingerprint = f.fingerprint();
    {
        Journal journal(job, options.checkpoint, fingerprint, describe_output(options));
        journal.append(5 * sample_rate, {"en", 5, {{0, 5, "First\nline.", 0}}});
    }
    std::map<std::string, std::string> expected;
    options.checkpoint.resume = true;
    {
        Journal journal(job, options.checkpoint, fingerprint, describe_output(options));
        journal.append(
            10 * sample_rate,
            {"en", 10, {{0, 1, "Same paragraph.", 0}, {4, 4, "New & <tag> --> cue.", 0}}});
        journal.append(sample_rate,
                       {"ru", 1, {{0, 1, u8"\u041f\u0440\u0438\u0432\u0435\u0442.", 0}}});
        journal.finish();
        TranscriptSource source{journal.samples(), journal.languages(),
                                [&](const SegmentConsumer& consume) { journal.visit(consume); }};
        source.metadata = journal.output_metadata();
        for (const auto& [format, path] : job.outputs) {
            std::ostringstream out;
            render_stream(out, format, job, options.rendering, source);
            expected[format] = out.str();
        }
        fs::create_directory(job.outputs.at("vtt"));
        rejects(
            [&] { publish_outputs(job, options.rendering, journal, options.checkpoint.overwrite); },
            "Unsafe output");
        require(fs::exists(job.outputs.at("text")));
        fs::remove(job.outputs.at("vtt"));
    }
    Journal journal(job, options.checkpoint, fingerprint, describe_output(options));
    require(journal.finished());
    publish_outputs(job, options.rendering, journal, options.checkpoint.overwrite);
    for (const auto& [format, path] : job.outputs)
        require(read_text(path) == expected.at(format), "Changed resumed " + format);
}
void staging_failure_preserves_committed_state(const fs::path& root) {
    Fixture f(root);
    const auto directory = checkpoint_path(f.job);
    std::string manifest;
    {
        Journal journal(f.job, f.options.checkpoint, f.fingerprint(), describe_output(f.options));
        journal.append(sample_rate, {"en", 1, {{0, 1, "Saved.", 0}}});
        journal.finish();
        manifest = read_text(directory / "manifest.json");
        int rendered = 0;
        rejects(
            [&] {
                journal.publish(f.job, false, [&](auto& out, const auto& format) {
                    require(read_text(directory / "manifest.json") == manifest);
                    for (const auto& [name, path] : f.job.outputs)
                        require(!fs::exists(path), "published before all formats were staged");
                    out << format << "\n";
                    if (++rendered == 2)
                        throw std::runtime_error("injected renderer failure");
                });
            },
            "injected renderer failure");
        require(rendered == 2 && journal.finished() && journal.samples() == sample_rate);
        require(read_text(directory / "manifest.json") == manifest);
    }
    f.options.checkpoint.resume = true;
    Journal resumed(f.job, f.options.checkpoint, f.fingerprint(), describe_output(f.options));
    require(resumed.finished() && resumed.samples() == sample_rate);
    resumed.publish(f.job, false, [](auto& out, const auto& format) { out << format << "\n"; });
    for (const auto& [format, path] : f.job.outputs)
        require(read_text(path) == format + "\n");
    require(!has_checkpoint(f.job) && !fs::exists(directory.string() + ".completed"));
}
void changed_reconstruction_does_not_publish(const fs::path& root) {
    Fixture f(root);
    auto render = [](auto& out, const auto& format) { out << format << "\n"; };
    {
        Journal journal(f.job, f.options.checkpoint, f.fingerprint(), describe_output(f.options));
        journal.append(sample_rate, {"en", 1, {{0, 1, "Saved.", 0}}});
        journal.finish();
        fs::create_directory(f.job.outputs.at("srt"));
        rejects([&] { journal.publish(f.job, false, render); }, "Unsafe output");
        fs::remove(f.job.outputs.at("srt"));
    }
    auto manifest_path = checkpoint_path(f.job) / "manifest.json";
    auto manifest = read_text(manifest_path);
    require(Json::parse(manifest).at("published_hashes").size() == f.job.outputs.size());
    f.options.checkpoint.resume = true;
    Journal resumed(f.job, f.options.checkpoint, f.fingerprint(), describe_output(f.options));
    rejects(
        [&] {
            resumed.publish(f.job, false, [](auto& out, const auto& format) {
                out << format << (format == "text" ? "changed\n" : "\n");
            });
        },
        "Reconstructed outputs differ from checkpoint");
    require(read_text(manifest_path) == manifest);
    require(read_text(f.job.outputs.at("json")) == "json\n");
    require(!fs::exists(f.job.outputs.at("srt")) && !fs::exists(f.job.outputs.at("text")) &&
            !fs::exists(f.job.outputs.at("vtt")));
    resumed.publish(f.job, false, render);
    require(!has_checkpoint(f.job));
}
} // namespace
int main() {
    return run_tests({
        {"late publication failure and all-format recovery", late_publication_failure},
        {"partial publication and external edits", partial_publication_and_external_edits},
        {"renderer failure preserves the committed checkpoint",
         staging_failure_preserves_committed_state},
        {"changed reconstruction cannot publish or replace saved hashes",
         changed_reconstruction_does_not_publish},
    });
}
