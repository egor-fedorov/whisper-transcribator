#include "app.hpp"
#include "resampler.hpp"
#include <algorithm>
#include <memory>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

namespace wt {
namespace {
void check(int code, const char* operation) {
    check_cancelled();
    if (code < 0) {
        char message[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(code, message, sizeof(message));
        throw std::runtime_error(std::string(operation) + ": " + message);
    }
}
} // namespace
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
        check(capacity, "resample capacity");
        std::vector<float> result(static_cast<size_t>(std::max(1, capacity)));
        auto* output = reinterpret_cast<uint8_t*>(result.data());
        int size = swr_convert(context, &output, capacity, data, count);
        check(size, "resample");
        result.resize(static_cast<size_t>(size));
        return result;
    }
};
FrameResampler::FrameResampler() : impl(std::make_unique<Impl>()) {}
FrameResampler::~FrameResampler() = default;
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
        check(av_channel_layout_copy(&d.layout, &frame.ch_layout), "copy channel layout");
        d.rate = frame.sample_rate;
        d.format = frame.format;
        AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
        check(swr_alloc_set_opts2(&d.context, &mono, AV_SAMPLE_FMT_FLT, 16000, &d.layout,
                                  static_cast<AVSampleFormat>(d.format), d.rate, 0, nullptr),
              "create resampler");
        check(swr_init(d.context), "initialize resampler");
    }
    auto converted = d.convert(const_cast<const uint8_t**>(frame.extended_data), frame.nb_samples);
    result.insert(result.end(), converted.begin(), converted.end());
    return result;
}
struct AudioReader::Impl {
    AVFormatContext* format = nullptr;
    AVDictionary* options = nullptr;
    AVCodecContext* codec = nullptr;
    FrameResampler resampler;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    int stream = -1;
    bool flushing = false, decoded = false, finished = false;
    std::vector<float> pending;
    size_t offset = 0;
    ~Impl() {
        av_dict_free(&options);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }
    void next() {
        pending.clear();
        offset = 0;
        while (!finished && pending.empty()) {
            check_cancelled();
            if (decoded) {
                pending = resampler.drain();
                finished = pending.empty();
                continue;
            }
            int code = avcodec_receive_frame(codec, frame);
            if (code >= 0) {
                pending = resampler.convert(*frame);
                av_frame_unref(frame);
                continue;
            }
            if (code == AVERROR_EOF) {
                decoded = true;
                continue;
            }
            if (code != AVERROR(EAGAIN))
                check(code, "decode frame");
            if (flushing)
                throw std::runtime_error("Decoder requested input after EOF");
            do {
                av_packet_unref(packet);
                code = av_read_frame(format, packet);
                check_cancelled();
            } while (code >= 0 && packet->stream_index != stream);
            if (code == AVERROR_EOF) {
                flushing = true;
                check(avcodec_send_packet(codec, nullptr), "flush decoder");
            } else {
                check(code, "read packet");
                check(avcodec_send_packet(codec, packet), "decode packet");
                av_packet_unref(packet);
            }
        }
    }
};
AudioReader::AudioReader(const fs::path& path) : impl(std::make_unique<Impl>()) {
    check_cancelled();
    auto& d = *impl;
    if (!d.frame || !d.packet)
        throw std::bad_alloc();
    d.format = avformat_alloc_context();
    if (!d.format)
        throw std::bad_alloc();
    d.format->interrupt_callback = {[](void*) { return stop_signal ? 1 : 0; }, nullptr};
    check(av_dict_set(&d.options, "protocol_whitelist", "file", 0), "restrict media protocols");
    check(avformat_open_input(&d.format, path.c_str(), nullptr, &d.options), "open media");
    check(avformat_find_stream_info(d.format, nullptr), "read streams");
    for (unsigned int i = 0; i < d.format->nb_streams; ++i)
        if (d.format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            d.stream = static_cast<int>(i);
            break;
        }
    if (d.stream < 0)
        throw std::runtime_error("No audio stream in media file: " + path.string());
    const auto* parameters = d.format->streams[d.stream]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(parameters->codec_id);
    if (!codec)
        throw std::runtime_error("unsupported audio codec");
    d.codec = avcodec_alloc_context3(codec);
    if (!d.codec)
        throw std::bad_alloc();
    check(avcodec_parameters_to_context(d.codec, parameters), "codec parameters");
    check(avcodec_open2(d.codec, codec, nullptr), "open codec");
}
AudioReader::~AudioReader() = default;
std::vector<float> AudioReader::read(size_t limit) {
    if (!limit || limit > 600 * 16000)
        throw std::runtime_error("Invalid audio read size");
    check_cancelled();
    std::vector<float> output;
    output.reserve(limit);
    auto& d = *impl;
    while (output.size() < limit) {
        if (d.offset == d.pending.size())
            d.next();
        if (d.pending.empty())
            break;
        auto count = std::min(limit - output.size(), d.pending.size() - d.offset);
        output.insert(output.end(), d.pending.begin() + d.offset,
                      d.pending.begin() + d.offset + count);
        d.offset += count;
    }
    return output;
}
std::string audio_backend_version() {
    return std::to_string(avformat_version()) + "/" + std::to_string(avcodec_version()) + "/" +
           std::to_string(swresample_version()) + "/" + std::to_string(avutil_version());
}
} // namespace wt
