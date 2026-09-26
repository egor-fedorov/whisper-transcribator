#include "app.hpp"
#include "ggml-backend.h"
#include "version.hpp"
#include "whisper.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <limits>
#include <memory>

namespace wt {
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
    std::cerr << "CUDA unavailable; using CPU\n";
    return "cpu";
}
Json doctor(const Options& options) {
    Json result = {{"version", WT_VERSION},
                   {"backend", "whisper.cpp"},
                   {"backend_version", whisper_version()},
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
    if (options.language != "auto" && whisper_lang_id(options.language.c_str()) < 0)
        throw UsageError("Unknown language: " + options.language);
    auto jobs = prepare_jobs(options);
    if (jobs.empty()) {
        std::cerr << "No files to transcribe\n";
        return 0;
    }
    options.device = select_device(options.device);
    auto model = prepare_model(options.model, options);
    fs::path vad;
    if (!options.no_vad)
        vad =
            prepare_model(options.vad_model.empty() ? "silero-v6.2.0" : options.vad_model, options);
    check_cancelled();
    std::cerr << "Model: " << model << "; device: " << options.device << '\n';
    auto params = whisper_context_default_params();
    params.use_gpu = options.device == "cuda";
    params.flash_attn = true;
    std::unique_ptr<whisper_context, decltype(&whisper_free)> context(
        whisper_init_from_file_with_params(model.c_str(), params), whisper_free);
    check_cancelled();
    if (!context)
        throw std::runtime_error("Cannot initialize GGML model: " + model.string());
    bool failed = false;
    for (const auto& job : jobs) {
        check_cancelled();
        auto start = std::chrono::steady_clock::now();
        std::cerr << "Transcribing " << job.source << '\n';
        try {
            auto pcm = decode_audio(job.source);
            if (pcm.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
                throw std::runtime_error("Audio too long for whisper_full");
            auto inference = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
            if (options.cpu_threads > 0)
                inference.n_threads = options.cpu_threads;
            inference.beam_search.beam_size = options.beam_size;
            inference.language = options.language.c_str();
            inference.print_progress = inference.print_realtime = inference.print_timestamps =
                false;
            inference.vad = !options.no_vad;
            inference.vad_model_path = vad.c_str();
            inference.vad_params.min_silence_duration_ms = options.vad_min_silence_ms;
            inference.abort_callback = [](void*) { return stop_signal != 0; };
            inference.encoder_begin_callback = [](whisper_context*, whisper_state*, void*) {
                return stop_signal == 0;
            };
            int progress = -1;
            inference.progress_callback_user_data = &progress;
            inference.progress_callback = [](whisper_context*, whisper_state*, int value,
                                             void* data) {
                auto& last = *static_cast<int*>(data);
                if (value / 10 != last) {
                    last = value / 10;
                    std::cerr << "Progress: " << value << "%\n";
                }
            };
            auto status =
                whisper_full(context.get(), inference, pcm.data(), static_cast<int>(pcm.size()));
            check_cancelled();
            if (status != 0)
                throw std::runtime_error("Inference failed");
            Transcript result{
                whisper_lang_str(whisper_full_lang_id(context.get())), pcm.size() / 16000.0, {}};
            for (int i = 0; i < whisper_full_n_segments(context.get()); ++i) {
                auto from = std::clamp(whisper_full_get_segment_t0(context.get(), i) / 100.0, 0.0,
                                       result.duration);
                auto to = std::clamp(whisper_full_get_segment_t1(context.get(), i) / 100.0, from,
                                     result.duration);
                result.segments.push_back(
                    {from, to, whisper_full_get_segment_text(context.get(), i),
                     whisper_full_get_segment_no_speech_prob(context.get(), i)});
            }
            write_outputs(job, options, result);
            std::cerr
                << "Done: " << job.source.filename() << "; elapsed "
                << std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count()
                << "s\n";
        } catch (const std::exception& error) {
            std::cerr << "Failed: " << job.source << ": " << error.what() << '\n';
            if (!options.continue_on_error)
                return 1;
            failed = true;
        }
    }
    return failed ? 1 : 0;
}
} // namespace wt
