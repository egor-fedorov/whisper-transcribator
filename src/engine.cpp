#include "ggml-backend.h"
#include "pipeline.hpp"
#include "version.hpp"
#include "whisper.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
extern "C" {
#include <libavutil/log.h>
}

namespace wt {
namespace {
void backend_logs() {
    whisper_log_set(
        [](ggml_log_level level, const char* text, void*) {
            try {
                static thread_local LogLevel previous = LogLevel::debug;
                if (level != GGML_LOG_LEVEL_CONT)
                    previous = level == GGML_LOG_LEVEL_ERROR  ? LogLevel::error
                               : level == GGML_LOG_LEVEL_WARN ? LogLevel::warning
                                                              : LogLevel::debug;
                if (!trim(text).empty())
                    log_message(previous, trim(text));
            } catch (...) {
            }
        },
        nullptr);
    av_log_set_callback([](void* ptr, int level, const char* format, va_list args) {
        try {
            char line[2048];
            int prefix = 1;
            av_log_format_line2(ptr, level, format, args, line, sizeof(line), &prefix);
            if (!trim(line).empty())
                log_message(level <= AV_LOG_ERROR     ? LogLevel::error
                            : level <= AV_LOG_WARNING ? LogLevel::warning
                                                      : LogLevel::debug,
                            trim(line));
        } catch (...) {
        }
    });
}
} // namespace
std::string select_device(const std::string& requested) {
    if (requested == "cpu")
        return "cpu";
    ggml_backend_load_all();
    bool cuda = false;
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        auto dev = ggml_backend_dev_get(i);
        if (std::string(ggml_backend_reg_name(ggml_backend_dev_backend_reg(dev))) == "CUDA" &&
            ggml_backend_dev_type(dev) == GGML_BACKEND_DEVICE_TYPE_GPU)
            cuda = true;
    }
    if (cuda)
        return "cuda";
    if (requested == "cuda")
        throw std::runtime_error(
            "CUDA requested but unavailable; use a CUDA build and check the NVIDIA driver/runtime");
    log_message(LogLevel::info, "CUDA unavailable; using CPU");
    return "cpu";
}
Json doctor(const Options& options) {
    backend_logs();
    Json result = {{"version", WT_VERSION},
                   {"backend", "whisper.cpp"},
                   {"backend_version", whisper_version()},
                   {"backend_revision", WT_WHISPER_REVISION},
                   {"model_cache", model_root(options).string()},
                   {"errors", Json::array()}};
    try {
        result["device"] = select_device(options.device);
    } catch (const std::exception& error) {
        result["errors"].push_back(error.what());
    }
    return result;
}
int transcribe(Options options) {
    backend_logs();
    if (options.language != "auto" && whisper_lang_id(options.language.c_str()) < 0)
        throw UsageError("Unknown language: " + options.language);
    auto jobs = prepare_jobs(options);
    if (jobs.empty()) {
        log_message(LogLevel::info, "No files to transcribe");
        return 0;
    }
    options.device = select_device(options.device);
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
    std::unique_ptr<whisper_context, decltype(&whisper_free)> context(nullptr, whisper_free);
    std::unique_ptr<whisper_vad_context, decltype(&whisper_vad_free)> splitter(nullptr,
                                                                               whisper_vad_free);
    auto cut = [&](const std::vector<float>& pcm) -> size_t {
        if (options.no_vad)
            return pcm.size();
        if (!splitter) {
            auto params = whisper_vad_default_context_params();
            params.use_gpu = false;
            if (options.cpu_threads > 0)
                params.n_threads = options.cpu_threads;
            splitter.reset(whisper_vad_init_from_file_with_params(vad.path.c_str(), params));
            if (!splitter)
                throw std::runtime_error("Cannot initialize VAD splitter");
        }
        auto params = whisper_vad_default_params();
        params.min_silence_duration_ms = options.chunk_min_silence_ms;
        params.speech_pad_ms = 0;
        std::unique_ptr<whisper_vad_segments, decltype(&whisper_vad_free_segments)> segments(
            whisper_vad_segments_from_samples(splitter.get(), params, pcm.data(),
                                              static_cast<int>(pcm.size())),
            whisper_vad_free_segments);
        check_cancelled();
        if (!segments)
            throw std::runtime_error("VAD splitting failed");
        std::vector<std::pair<int64_t, int64_t>> speech;
        for (int i = 0; i < whisper_vad_segments_n_segments(segments.get()); ++i) {
            // The VAD API returns centiseconds, not seconds.
            auto from = int64_t(whisper_vad_segments_get_segment_t0(segments.get(), i) * 160);
            auto to = int64_t(whisper_vad_segments_get_segment_t1(segments.get(), i) * 160);
            from = std::clamp<int64_t>(from, 0, pcm.size());
            speech.emplace_back(from, std::clamp<int64_t>(to, from, pcm.size()));
        }
        return pause_cut(pcm.size(), speech, options.chunk_min_silence_ms);
    };
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
                if (!context) {
                    report_progress("Loading model", model.path.filename().string());
                    auto params = whisper_context_default_params();
                    params.use_gpu = options.device == "cuda";
                    params.flash_attn = true;
                    context.reset(whisper_init_from_file_with_params(model.path.c_str(), params));
                    check_cancelled();
                    if (!context)
                        throw std::runtime_error("Cannot initialize GGML model: " +
                                                 model.path.string());
                }
                auto inference = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
                if (options.cpu_threads > 0)
                    inference.n_threads = options.cpu_threads;
                inference.beam_search.beam_size = options.beam_size;
                inference.language = options.language.c_str();
                inference.no_context = true;
                inference.print_progress = inference.print_realtime = inference.print_timestamps =
                    false;
                inference.vad = !options.no_vad;
                inference.vad_model_path = vad.path.c_str();
                inference.vad_params.min_silence_duration_ms = options.vad_min_silence_ms;
                inference.abort_callback = [](void*) { return stop_signal != 0; };
                inference.encoder_begin_callback = [](whisper_context*, whisper_state*, void*) {
                    return stop_signal == 0;
                };
                progress.begin_window(journal.samples(), pcm.size());
                inference.progress_callback_user_data = &progress;
                inference.progress_callback = [](whisper_context*, whisper_state*, int value,
                                                 void* data) {
                    try {
                        static_cast<FileProgress*>(data)->update(value);
                    } catch (...) {
                    }
                };
                auto status = whisper_full(context.get(), inference, pcm.data(),
                                           static_cast<int>(pcm.size()));
                check_cancelled();
                if (status != 0)
                    throw std::runtime_error("Inference failed");
                Transcript result{"", pcm.size() / 16000.0, {}};
                for (int i = 0; i < whisper_full_n_segments(context.get()); ++i) {
                    auto from = std::clamp(whisper_full_get_segment_t0(context.get(), i) / 100.0,
                                           0.0, result.duration);
                    auto to = std::clamp(whisper_full_get_segment_t1(context.get(), i) / 100.0,
                                         from, result.duration);
                    result.segments.push_back(
                        {from, to, whisper_full_get_segment_text(context.get(), i),
                         whisper_full_get_segment_no_speech_prob(context.get(), i)});
                }
                bool has_text =
                    std::any_of(result.segments.begin(), result.segments.end(),
                                [](const auto& segment) { return !trim(segment.text).empty(); });
                if (has_text) {
                    auto language = whisper_lang_str(whisper_full_lang_id(context.get()));
                    if (!language)
                        throw std::runtime_error("Cannot determine transcript language");
                    result.language = language;
                }
                return result;
            };
            run_chunks(journal, size_t(options.chunk_seconds) * sample_rate, read, recognize, cut,
                       [&](int64_t samples, size_t, bool committed) {
                           if (committed)
                               progress.commit(samples);
                       });
            if (size != fs::file_size(job.source) || modified != fs::last_write_time(job.source))
                throw std::runtime_error("Input changed during transcription");
            report_progress("Publishing", job.source.filename().string());
            journal.publish(job, job_options);
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
