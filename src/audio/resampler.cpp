#include "audio/resampler.hpp"
#include "audio/detail/error.hpp"
#include "support/cancel.hpp"
#include <algorithm>
#include <stdexcept>
extern "C" {
#include <libavutil/frame.h>
#include <libswresample/swresample.h>
}

namespace wt {
struct FrameResampler::Impl {
    SwrContext* context = nullptr;
    AVChannelLayout layout{};
    int rate = 0, format = AV_SAMPLE_FMT_NONE;
    ~Impl() {
        swr_free(&context);
        av_channel_layout_uninit(&layout);
    }
    std::vector<float> convert(const uint8_t** data, int count) {
        if (!context)
            return {};
        int capacity = swr_get_out_samples(context, count);
        audio_detail::check(capacity, "resample capacity");
        std::vector<float> result(static_cast<size_t>(std::max(1, capacity)));
        auto* output = reinterpret_cast<uint8_t*>(result.data());
        int size = swr_convert(context, &output, capacity, data, count);
        audio_detail::check(size, "resample");
        result.resize(static_cast<size_t>(size));
        return result;
    }
};
FrameResampler::FrameResampler() : impl(std::make_unique<Impl>()) {}
FrameResampler::~FrameResampler() = default;
int64_t FrameResampler::delay() const {
    return impl->context ? swr_get_delay(impl->context, 16000) : 0;
}
void FrameResampler::reset() { impl = std::make_unique<Impl>(); }
std::vector<float> FrameResampler::drain() { return impl->convert(nullptr, 0); }
std::vector<float> FrameResampler::convert(const AVFrame& frame) {
    check_cancelled();
    if (frame.sample_rate <= 0 || !av_channel_layout_check(&frame.ch_layout) ||
        !av_get_bytes_per_sample(static_cast<AVSampleFormat>(frame.format)) || frame.nb_samples < 0)
        throw std::runtime_error("Invalid decoded audio parameters");
    auto& d = *impl;
    std::vector<float> result;
    if (!d.context || frame.sample_rate != d.rate || frame.format != d.format ||
        av_channel_layout_compare(&frame.ch_layout, &d.layout) != 0) {
        // Preserve delayed samples before reconfiguring for the retained frame.
        while (true) {
            auto tail = drain();
            if (tail.empty())
                break;
            result.insert(result.end(), tail.begin(), tail.end());
        }
        swr_free(&d.context);
        av_channel_layout_uninit(&d.layout);
        audio_detail::check(av_channel_layout_copy(&d.layout, &frame.ch_layout),
                            "copy channel layout");
        d.rate = frame.sample_rate;
        d.format = frame.format;
        AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
        audio_detail::check(swr_alloc_set_opts2(&d.context, &mono, AV_SAMPLE_FMT_FLT, 16000,
                                                &d.layout, static_cast<AVSampleFormat>(d.format),
                                                d.rate, 0, nullptr),
                            "create resampler");
        audio_detail::check(swr_init(d.context), "initialize resampler");
    }
    auto converted = d.convert(const_cast<const uint8_t**>(frame.extended_data), frame.nb_samples);
    result.insert(result.end(), converted.begin(), converted.end());
    return result;
}
} // namespace wt
