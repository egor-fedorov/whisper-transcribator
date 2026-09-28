#include "inference/whisper.hpp"
#include "inference/runtime.hpp"
#include "models/types.hpp"
#include "support/cancel.hpp"
#include "support/report.hpp"
#include "support/strings.hpp"
#include "transcript/boundaries.hpp"
#include "whisper.h"
#include <algorithm>
#include <stdexcept>

namespace wt {
struct WhisperSession::Impl {
    InferenceOptions options;
    PreparedModel model, vad;
    DeviceSelection device;
    // whisper.cpp takes UTF-8 paths on every system and keeps the VAD path during inference.
    std::string model_path = model.path.u8string(), vad_path = vad.path.u8string();
    std::unique_ptr<whisper_context, decltype(&whisper_free)> context{nullptr, whisper_free};
    std::unique_ptr<whisper_vad_context, decltype(&whisper_vad_free)> splitter{nullptr,
                                                                               whisper_vad_free};
    Impl(const InferenceOptions& options, const PreparedModel& model, const PreparedModel& vad,
         const DeviceSelection& device)
        : options(options), model(model), vad(vad), device(device) {}
};
WhisperSession::WhisperSession(const InferenceOptions& options, const PreparedModel& model,
                               const PreparedModel& vad, const DeviceSelection& device)
    : impl(std::make_unique<Impl>(options, model, vad, device)) {}
WhisperSession::~WhisperSession() = default;
size_t WhisperSession::choose_cut(const std::vector<float>& pcm, int minimum_silence_ms) {
    auto& options = impl->options;
    auto& splitter = impl->splitter;
    if (options.no_vad)
        return pcm.size();
    if (!splitter) {
        auto params = whisper_vad_default_context_params();
        params.use_gpu = false;
        if (options.cpu_threads > 0)
            params.n_threads = options.cpu_threads;
        splitter.reset(whisper_vad_init_from_file_with_params(impl->vad_path.c_str(), params));
        if (!splitter)
            throw std::runtime_error("Cannot initialize VAD splitter");
    }
    auto params = whisper_vad_default_params();
    params.min_silence_duration_ms = minimum_silence_ms;
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
    return pause_cut(pcm.size(), speech, minimum_silence_ms);
}
Transcript WhisperSession::recognize(const std::vector<float>& pcm,
                                     const std::function<void()>& begin,
                                     const std::function<void(int)>& progress) {
    auto& options = impl->options;
    auto& context = impl->context;
    auto& model = impl->model;
    if (!context) {
        report_progress("Loading model", model.path.filename().string());
        auto params = whisper_context_default_params();
        params.use_gpu = impl->device.backend != "cpu";
        params.gpu_device = impl->device.gpu_index;
        params.flash_attn = true;
        context.reset(whisper_init_from_file_with_params(impl->model_path.c_str(), params));
        check_cancelled();
        if (!context)
            throw std::runtime_error("Cannot initialize GGML model on " + impl->device.backend +
                                     " (" + impl->device.description + "): " + model.path.string() +
                                     (params.use_gpu ? "; no CPU fallback was attempted. Check "
                                                       "GPU memory/driver or use --device cpu"
                                                     : ""));
    }
    auto inference = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
    if (options.cpu_threads > 0)
        inference.n_threads = options.cpu_threads;
    inference.beam_search.beam_size = options.beam_size;
    inference.language = options.language.c_str();
    inference.no_context = true;
    inference.print_progress = inference.print_realtime = inference.print_timestamps = false;
    inference.vad = !options.no_vad;
    inference.vad_model_path = impl->vad_path.c_str();
    inference.vad_params.min_silence_duration_ms = options.vad_min_silence_ms;
    inference.abort_callback = [](void*) { return stop_signal != 0; };
    inference.encoder_begin_callback = [](whisper_context*, whisper_state*, void*) {
        return stop_signal == 0;
    };
    begin();
    auto callback = progress;
    inference.progress_callback_user_data = &callback;
    inference.progress_callback = [](whisper_context*, whisper_state*, int value, void* data) {
        try {
            (*static_cast<std::function<void(int)>*>(data))(value);
        } catch (...) {
        }
    };
    auto status = whisper_full(context.get(), inference, pcm.data(), static_cast<int>(pcm.size()));
    check_cancelled();
    if (status != 0)
        throw std::runtime_error("Inference failed");
    Transcript result{"", pcm.size() / 16000.0, {}};
    for (int i = 0; i < whisper_full_n_segments(context.get()); ++i) {
        auto from =
            std::clamp(whisper_full_get_segment_t0(context.get(), i) / 100.0, 0.0, result.duration);
        auto to = std::clamp(whisper_full_get_segment_t1(context.get(), i) / 100.0, from,
                             result.duration);
        result.segments.push_back({from, to, whisper_full_get_segment_text(context.get(), i),
                                   whisper_full_get_segment_no_speech_prob(context.get(), i)});
    }
    bool has_text = std::any_of(result.segments.begin(), result.segments.end(),
                                [](const auto& segment) { return !trim(segment.text).empty(); });
    if (has_text) {
        auto language = whisper_lang_str(whisper_full_lang_id(context.get()));
        if (!language)
            throw std::runtime_error("Cannot determine transcript language");
        result.language = language;
    }
    return result;
}
} // namespace wt
