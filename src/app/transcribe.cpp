#include "app/commands.hpp"
#include "audio/audio.hpp"
#include "inference/runtime.hpp"
#include "inference/whisper.hpp"
#include "models/models.hpp"
#include "support/cancel.hpp"
#include "support/cpu.hpp"
#include "support/options.hpp"
#include "support/report.hpp"
#include "transcript/jobs.hpp"
#include "transcript/journal.hpp"
#include "transcript/metadata.hpp"
#include "transcript/outputs.hpp"
#include "transcript/pipeline.hpp"
#include <chrono>

namespace wt {
int transcribe(Options options) {
    configure_inference_logging();
    configure_audio_logging();
    validate_language(options.language);
    auto jobs = prepare_jobs(options);
    if (jobs.empty()) {
        log_message(LogLevel::info, "No files to transcribe");
        return 0;
    }
    options.device = select_device(options.device);
    if (!options.cpu_threads)
        options.cpu_threads = automatic_cpu_threads();
    log_message(LogLevel::info, "CPU threads: " + std::to_string(options.cpu_threads));
    auto model = prepare_model(options.model, options);
    PreparedModel vad;
    if (!options.no_vad)
        vad =
            prepare_model(options.vad_model.empty() ? "silero-v6.2.0" : options.vad_model, options);
    check_cancelled();
    log_message(LogLevel::info, "Model: " + model.path.string() + "; device: " + options.device);
    Json backend = {{"model_sha256", model.hash},
                    {"vad_sha256", vad.hash},
                    {"ffmpeg", audio_backend_version()}};
    WhisperSession session(options, model, vad);
    auto cut = [&](const std::vector<float>& pcm) { return session.choose_cut(pcm); };
    bool failed = false;
    size_t job_index = 0;
    for (const auto& job : jobs) {
        ++job_index;
        check_cancelled();
        auto start = std::chrono::steady_clock::now();
        log_message(LogLevel::info, "Transcribing " + job.source.string());
        try {
            auto modified = fs::last_write_time(job.source);
            auto size = fs::file_size(job.source);
            report_progress("Reading audio streams", job.source.filename().string());
            AudioReader reader(job.source, options.audio_stream);
            auto job_options = options;
            job_options.audio_stream = reader.stream_index();
            log_message(LogLevel::info, "Audio stream: " + std::to_string(reader.stream_index()));
            auto fingerprint = job_fingerprint(job, job_options, backend);
            Journal journal(job, job_options, fingerprint);
            FileProgress progress(std::to_string(job_index) + "/" + std::to_string(jobs.size()),
                                  reader.duration());
            auto read = [&](size_t limit) { return reader.read(limit); };
            auto recognize = [&](const std::vector<float>& pcm, const std::string&) {
                return session.recognize(
                    pcm, [&] { progress.begin_window(journal.samples(), pcm.size()); },
                    [&](int value) { progress.update(value); });
            };
            run_chunks(journal, size_t(options.chunk_seconds) * sample_rate, read, recognize, cut,
                       [&](int64_t samples, size_t, bool committed) {
                           if (committed)
                               progress.commit(samples);
                       });
            if (size != fs::file_size(job.source) || modified != fs::last_write_time(job.source))
                throw std::runtime_error("Input changed during transcription");
            report_progress("Publishing", job.source.filename().string());
            publish_outputs(job, job_options, journal);
            log_message(LogLevel::info,
                        "Done: " + job.source.filename().string() + "; elapsed " +
                            std::to_string(std::chrono::duration<double>(
                                               std::chrono::steady_clock::now() - start)
                                               .count()) +
                            "s");
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
