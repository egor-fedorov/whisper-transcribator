#include "transcript/outputs.hpp"
#include "support/io.hpp"
#include "support/options.hpp"
#include "transcript/jobs.hpp"
#include "transcript/journal.hpp"
#include "transcript/metadata.hpp"
#include <iostream>
#include <unistd.h>

using namespace wt;
namespace {
void require(bool condition) {
    if (!condition)
        throw std::runtime_error("output assertion failed");
}
} // namespace
int main() {
    char pattern[] = "/tmp/whisper-outputs-XXXXXX";
    auto* created = mkdtemp(pattern);
    if (!created)
        return 1;
    fs::path root = created;
    try {
        Options options;
        atomic_write(root / "source.wav", "fixture");
        options.inputs = {(root / "source.wav").string()};
        options.output_dir = root.string();
        options.format = "all";
        auto job = prepare_jobs(options).at(0);
        require(job.outputs.size() == 4);
        auto fingerprint = job_fingerprint(job, options, Json::object());
        {
            Journal journal(job, options, fingerprint);
            journal.append(5 * sample_rate, {"en", 5, {{0, 5, "First\nline.", 0}}});
        }
        options.resume = true;
        {
            Journal journal(job, options, fingerprint);
            journal.append(
                10 * sample_rate,
                {"en", 10, {{0, 1, "Same paragraph.", 0}, {4, 4, "New & <tag> --> cue.", 0}}});
            journal.append(sample_rate,
                           {"ru", 1, {{0, 1, u8"\u041f\u0440\u0438\u0432\u0435\u0442.", 0}}});
            journal.finish();
            // Every publication position can fail and recover without inference.
            fs::create_directory(job.outputs.at("vtt"));
            bool failed = false;
            try {
                publish_outputs(job, options, journal);
            } catch (const std::exception&) {
                failed = true;
            }
            require(failed && fs::exists(job.outputs.at("text")));
            fs::remove(job.outputs.at("vtt"));
        }
        {
            Journal journal(job, options, fingerprint);
            require(journal.finished());
            publish_outputs(job, options, journal);
        }
        auto text = read_text(job.outputs.at("text"));
        require(text == u8"First line. Same paragraph.\n\nNew & <tag> --> "
                        u8"cue.\n\n\u041f\u0440\u0438\u0432\u0435\u0442.\n");
        auto json = Json::parse(read_text(job.outputs.at("json")));
        require(json["text"].get<std::string>() + "\n" == text && json["language"].is_null());
        require(json["segments"][0]["text"] == "First\nline.");
        auto vtt = read_text(job.outputs.at("vtt"));
        require(vtt.rfind("WEBVTT\n\n", 0) == 0);
        require(vtt.find("00:00:09.000 --> 00:00:09.001") != std::string::npos);
        require(vtt.find("New &amp; &lt;tag&gt; --&gt; cue.") != std::string::npos);

        for (const auto& [format, path] : job.outputs)
            fs::remove(path);
        options.resume = false;
        options.text_layout = "single-line";
        {
            Journal journal(job, options, job_fingerprint(job, options, Json::object()));
            journal.append(10 * sample_rate, {"en", 10, {{0, 1, "One.", 0}, {8, 9, "Two.", 0}}});
            journal.finish();
            publish_outputs(job, options, journal);
        }
        require(read_text(job.outputs.at("text")) == "One. Two.\n");
        for (const auto& [format, path] : job.outputs)
            fs::remove(path);
        options.text_layout = "paragraphs";
        {
            Journal journal(job, options, job_fingerprint(job, options, Json::object()));
            std::string unicode;
            for (int i = 0; i < 599; ++i)
                unicode += u8"\u044f";
            journal.append(3 * sample_rate,
                           {"ru", 3, {{0, 1, unicode + ".", 0}, {1, 2, "Next.", 0}}});
            journal.finish();
            publish_outputs(job, options, journal);
            require(read_text(job.outputs.at("text")) == unicode + ".\n\nNext.\n");
        }
        for (const auto& [format, path] : job.outputs)
            fs::remove(path);
        auto old_job = job;
        old_job.outputs.erase("vtt");
        {
            Journal old(old_job, options, job_fingerprint(old_job, options, Json::object()));
            old.append(sample_rate, {"en", 1, {{0, 1, "saved", 0}}});
        }
        bool rejected = false;
        try {
            Journal newer(job, options, job_fingerprint(job, options, Json::object()));
        } catch (const std::exception&) {
            rejected = true;
        }
        require(rejected && has_checkpoint(old_job));
    } catch (const std::exception& error) {
        std::cerr << error.what() << " (fixtures: " << root << ")\n";
        return 1;
    }
    fs::remove_all(root);
    std::cout << "Paragraphs, multilingual text, WebVTT and publication recovery passed\n";
}
