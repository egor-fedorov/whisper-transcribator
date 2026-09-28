#include "app/commands.hpp"
#include "app/configuration.hpp"
#include "audio/audio.hpp"
#include "inference/runtime.hpp"
#include "inference/whisper.hpp"
#include "models/models.hpp"
#include "platform/cpu.hpp"
#include "support/cancel.hpp"
#include "support/error.hpp"
#include "support/report.hpp"
#include "transcript/jobs.hpp"
#include "transcript/journal.hpp"
#include "transcript/metadata.hpp"
#include "transcript/outputs.hpp"
#include "transcript/pipeline.hpp"
#include <chrono>

namespace wt {
int transcribe(CliOptions options) {
    configure_inference_logging();
    configure_audio_logging(options.verbose);
    validate_language(options.inference.language);
    const auto& audio = options.audio;
    auto jobs = prepare_jobs(options.jobs, options.checkpoint);
    if (jobs.empty()) {
        log_message(LogLevel::info, "No files to transcribe");
        return 0;
    }
    // Explicit stream selection is a command-wide usage constraint, not a model failure.
    bool failed = false;
    if (options.audio.stream >= 0) {
        std::vector<Job> valid;
        for (const auto& job : jobs) {
            try {
                AudioReader probe(job.source, audio.stream, audio.gaps, audio.errors);
                valid.push_back(job);
            } catch (const UsageError& error) {
                throw UsageError(job.source.string() + ": " + error.what());
            } catch (const std::exception& error) {
                check_cancelled();
                log_message(LogLevel::error,
                            "Failed: " + job.source.string() + ": " + error.what());
                if (!options.continue_on_error)
                    return 1;
                failed = true;
            }
        }
        jobs = std::move(valid);
        if (jobs.empty())
            return failed ? 1 : 0;
    }
    auto device = select_device(options.device);
    options.device = device.backend;
    if (!options.inference.cpu_threads)
        options.inference.cpu_threads = platform::automatic_cpu_threads();
    log_message(LogLevel::info, "CPU threads: " + std::to_string(options.inference.cpu_threads));
    auto model = prepare_model(options.model, options.cache);
    PreparedModel vad;
    if (!options.inference.no_vad)
        vad = prepare_model(options.vad_model.empty() ? "silero-v6.2.0" : options.vad_model,
                            options.cache);
    check_cancelled();
    log_message(LogLevel::info, "Model: " + model.path.string() + "; device: " + options.device +
                                    " (" + device.description + ")");
    Json backend = {{"model_sha256", model.hash},
                    {"vad_sha256", vad.hash},
                    {"ffmpeg", audio_backend_version()}};
    WhisperSession session(options.inference, model, vad, device);
    auto cut = [&](const std::vector<float>& pcm) {
        return session.choose_cut(pcm, options.chunking.chunk_min_silence_ms);
    };
    size_t job_index = 0;
    PublishedOutputs published;
    for (const auto& job : jobs) {
        ++job_index;
        check_cancelled();
        auto start = std::chrono::steady_clock::now();
        log_message(LogLevel::info, "Transcribing " + job.source.string());
        try {
            published.check(job);
            auto modified = fs::last_write_time(job.source);
            auto size = fs::file_size(job.source);
            report_progress("Reading audio streams", job.source.filename().string());
            AudioReader reader(job.source, audio.stream, audio.gaps, audio.errors);
            auto job_options = options;
            job_options.audio.stream = reader.stream_index();
            log_message(LogLevel::info, "Audio stream: " + std::to_string(reader.stream_index()));
            auto output_metadata = describe_output(job_options);
            auto fingerprint = job_fingerprint(job, output_metadata.at("run"),
                                               options.inference.language, backend);
            Journal journal(job, options.checkpoint, fingerprint, output_metadata);
            FileProgress progress(std::to_string(job_index) + "/" + std::to_string(jobs.size()),
                                  reader.duration());
            auto read = [&](size_t limit) {
                auto pcm = reader.read(limit);
                if (!reader.duration())
                    progress.invalidate_duration();
                return pcm;
            };
            auto recognize = [&](const std::vector<float>& pcm) {
                return session.recognize(
                    pcm, [&] { progress.begin_window(journal.samples(), pcm.size()); },
                    [&](int value) { progress.update(value); });
            };
            run_chunks(journal, size_t(options.chunking.chunk_seconds) * sample_rate, read,
                       recognize, cut, {{}, [&](int64_t samples) { progress.commit(samples); }});
            if (size != fs::file_size(job.source) || modified != fs::last_write_time(job.source))
                throw std::runtime_error("Input changed during transcription");
            report_progress("Publishing", job.source.filename().string());
            publish_outputs(job, options.rendering, journal, options.checkpoint.overwrite);
            published.record(job);
            log_message(LogLevel::info,
                        "Done: " + job.source.filename().string() + "; elapsed " +
                            format_seconds(std::chrono::duration<double>(
                                               std::chrono::steady_clock::now() - start)
                                               .count()) +
                            "s");
        } catch (const UsageError&) {
            throw;
        } catch (const std::exception& error) {
            check_cancelled();
            log_message(LogLevel::error, "Failed: " + job.source.string() + ": " + error.what());
            if (!options.continue_on_error)
                return 1;
            failed = true;
        }
    }
    return failed ? 1 : 0;
}
} // namespace wt
