#include "app.hpp"
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

struct Decoder {
    AVFormatContext* format = nullptr;
    AVDictionary* options = nullptr;
    AVCodecContext* codec = nullptr;
    SwrContext* resampler = nullptr;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    ~Decoder() {
        av_dict_free(&options);
        av_frame_free(&frame);
        av_packet_free(&packet);
        swr_free(&resampler);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }
};

static void check(int code, const char* operation) {
    wt::check_cancelled();
    if (code < 0) {
        char message[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(code, message, sizeof(message));
        throw std::runtime_error(std::string(operation) + ": " + message);
    }
}

namespace wt {
std::vector<float> decode_audio(const fs::path& path) {
    check_cancelled();
    Decoder d;
    if (!d.frame || !d.packet)
        throw std::bad_alloc();
    d.format = avformat_alloc_context();
    if (!d.format)
        throw std::bad_alloc();
    d.format->interrupt_callback = {[](void*) { return stop_signal ? 1 : 0; }, nullptr};
    check(av_dict_set(&d.options, "protocol_whitelist", "file", 0), "restrict media protocols");
    check(avformat_open_input(&d.format, path.c_str(), nullptr, &d.options), "open media");
    check(avformat_find_stream_info(d.format, nullptr), "read streams");
    int stream = -1;
    for (unsigned int i = 0; i < d.format->nb_streams; ++i) {
        if (d.format->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
            stream = static_cast<int>(i);
            break;
        }
    }
    if (stream < 0)
        throw std::runtime_error("No audio stream in media file: " + path.string());
    const auto* parameters = d.format->streams[stream]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(parameters->codec_id);
    if (!codec)
        throw std::runtime_error("unsupported audio codec");
    d.codec = avcodec_alloc_context3(codec);
    if (!d.codec)
        throw std::bad_alloc();
    check(avcodec_parameters_to_context(d.codec, parameters), "codec parameters");
    check(avcodec_open2(d.codec, codec, nullptr), "open codec");
    AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
    check(swr_alloc_set_opts2(&d.resampler, &mono, AV_SAMPLE_FMT_FLT, 16000, &d.codec->ch_layout,
                              d.codec->sample_fmt, d.codec->sample_rate, 0, nullptr),
          "create resampler");
    check(swr_init(d.resampler), "initialize resampler");
    std::vector<float> pcm;
    auto convert = [&](const uint8_t** data, int count) {
        int capacity = swr_get_out_samples(d.resampler, count);
        check(capacity, "resample capacity");
        std::vector<float> buffer(static_cast<size_t>(capacity));
        uint8_t* output = reinterpret_cast<uint8_t*>(buffer.data());
        int size = swr_convert(d.resampler, &output, capacity, data, count);
        check(size, "resample");
        pcm.insert(pcm.end(), buffer.begin(), buffer.begin() + size);
        return size;
    };
    auto receive = [&]() {
        int code;
        while ((code = avcodec_receive_frame(d.codec, d.frame)) >= 0) {
            check_cancelled();
            convert(const_cast<const uint8_t**>(d.frame->extended_data), d.frame->nb_samples);
            av_frame_unref(d.frame);
        }
        if (code != AVERROR(EAGAIN) && code != AVERROR_EOF)
            check(code, "decode frame");
    };
    int code;
    while ((code = av_read_frame(d.format, d.packet)) >= 0) {
        check_cancelled();
        if (d.packet->stream_index == stream) {
            check(avcodec_send_packet(d.codec, d.packet), "decode packet");
            receive();
        }
        av_packet_unref(d.packet);
    }
    if (code != AVERROR_EOF)
        check(code, "read packet");
    check(avcodec_send_packet(d.codec, nullptr), "flush decoder");
    receive();
    while (convert(nullptr, 0) > 0) {
    }
    if (pcm.empty())
        throw std::runtime_error("empty audio stream");
    return pcm;
}

} // namespace wt
