#include "audio/audio.hpp"
#include "audio/recovery.hpp"
#include "audio/resampler.hpp"
#include "audio/timeline.hpp"
#include "support/cancel.hpp"
#include "support/error.hpp"
#include "support/io.hpp"
#include "support/report.hpp"
#include <algorithm>
#include <memory>
#include <optional>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/log.h>
#include <libswresample/swresample.h>
}

namespace wt {
namespace {
thread_local const void* decode_probe = nullptr;

struct DecodeProbeLogging {
    const void* previous = decode_probe;
    explicit DecodeProbeLogging(const void* context) { decode_probe = context; }
    ~DecodeProbeLogging() { decode_probe = previous; }
};

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
    AudioTimeline timeline;
    DecodeRecovery recovery;
    AVPacket* packet = av_packet_alloc();
    AVFrame* frame = av_frame_alloc();
    int stream = -1;
    bool flushing = false, decoded = false, finished = false;
    std::vector<float> pending;
    size_t offset = 0;
    int64_t silence = 0, origin = AV_NOPTS_VALUE, retained_start = 0;
    double estimated_duration = 0;
    TimestampGaps gaps = TimestampGaps::automatic;
    bool retained = false, warned_reset = false, warned_gap = false, adjusted = false;
    bool recover(int code, const char* operation) {
        check_cancelled();
        if (!recovery.recover(code))
            return false;
        if (recovery.errors() == 1)
            log_message(LogLevel::warning, "Skipping invalid audio data rejected by the decoder; "
                                           "the transcript may be incomplete");
        log_message(LogLevel::debug, std::string("Recoverable audio decoder error: ") + operation +
                                         "; total " + std::to_string(recovery.errors()));
        return true;
    }
    ~Impl() {
        av_dict_free(&options);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }
    void place(int64_t start, std::vector<float> pcm) {
        // Keep the resampler's signed position, but never emit pre-origin samples.
        auto end = std::max<int64_t>(0, timeline.end());
        silence = std::max<int64_t>(0, start - end);
        offset = start < end ? static_cast<size_t>(std::min<int64_t>(end - start, pcm.size())) : 0;
        timeline.advance(start, pcm.size());
        pending = std::move(pcm);
    }
    void next() {
        pending.clear();
        offset = 0;
        while (!finished && !silence && offset == pending.size()) {
            check_cancelled();
            if (retained) {
                place(retained_start, resampler.convert(*frame));
                av_frame_unref(frame);
                retained = false;
                continue;
            }
            if (decoded) {
                place(timeline.end(), resampler.drain());
                finished = pending.empty();
                continue;
            }
            int code = avcodec_receive_frame(codec, frame);
            if (code >= 0) {
                recovery.frame();
                auto* selected = format->streams[stream];
                auto pts = frame->best_effort_timestamp;
                if (pts == AV_NOPTS_VALUE)
                    pts = frame->pts;
                std::optional<int64_t> sample_pts;
                if (pts != AV_NOPTS_VALUE) {
                    auto time = av_rescale_q(pts, selected->time_base, AVRational{1, 16000});
                    constexpr int64_t bound = INT64_MAX / 4;
                    if (time < -bound || time > bound)
                        throw std::runtime_error("Audio timestamp out of range");
                    if (origin == AV_NOPTS_VALUE)
                        origin = time - (timeline.end() + resampler.delay());
                    if (origin < -bound || origin > bound)
                        throw std::runtime_error("Audio timestamp out of range");
                    sample_pts = time - origin;
                }
                auto delay = resampler.delay();
                auto tolerance = std::max<int64_t>(
                    1, av_rescale_q_rnd(1, selected->time_base, AVRational{1, 16000}, AV_ROUND_UP));
                auto location = timeline.locate(sample_pts, delay, tolerance,
                                                format->iformat->flags & AVFMT_TS_DISCONT, gaps);
                if (location.reset)
                    adjusted = true;
                if (location.reset && !warned_reset) {
                    log_message(LogLevel::warning,
                                "Audio timestamp discontinuity corrected; continuing on a "
                                "contiguous timeline (use --timestamp-gaps preserve to retain "
                                "forward gaps)");
                    warned_reset = true;
                }
                auto gap = location.start - (timeline.end() + delay);
                if (gap > 10 * 16000 && !warned_gap) {
                    log_message(LogLevel::warning, "Preserving audio timestamp gap of " +
                                                       format_seconds(gap / 16000.0) +
                                                       "s as silence");
                    warned_gap = true;
                }
                if (location.discontinuity) {
                    log_message(
                        LogLevel::debug,
                        "Audio timestamp boundary: " + std::to_string(timeline.end() + delay) +
                            " -> " + std::to_string(location.start) + "; rate " +
                            std::to_string(frame->sample_rate));
                    std::vector<float> tail;
                    for (;;) {
                        auto part = resampler.drain();
                        if (part.empty())
                            break;
                        tail.insert(tail.end(), part.begin(), part.end());
                    }
                    place(timeline.end(), std::move(tail));
                    resampler.reset();
                    retained = true;
                    retained_start = location.start;
                    continue;
                }
                place(location.start - delay, resampler.convert(*frame));
                av_frame_unref(frame);
                continue;
            }
            if (code == AVERROR_EOF) {
                if (recovery.errors())
                    log_message(LogLevel::info, "Audio decoding completed with " +
                                                    std::to_string(recovery.errors()) +
                                                    " recoverable decoder errors");
                decoded = true;
                continue;
            }
            if (recover(code, "receive frame"))
                continue;
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
                recovery.packet();
                code = avcodec_send_packet(codec, packet);
                av_packet_unref(packet);
                if (!recover(code, "send packet"))
                    check(code, "decode packet");
            }
        }
    }
};
AudioReader::AudioReader(const fs::path& path, int stream, TimestampGaps gaps)
    : impl(std::make_unique<Impl>()) {
    check_cancelled();
    auto& d = *impl;
    if (!d.frame || !d.packet)
        throw std::bad_alloc();
    d.gaps = gaps;
    auto open = [&](bool raw_timestamps) {
        d.format = avformat_alloc_context();
        if (!d.format)
            throw std::bad_alloc();
        if (raw_timestamps) {
            d.format->flags |= AVFMT_FLAG_NOFILLIN;
            d.format->skip_estimate_duration_from_pts = 1;
        }
        d.format->interrupt_callback = {[](void*) { return stop_signal ? 1 : 0; }, nullptr};
        check(av_dict_set(&d.options, "protocol_whitelist", "file", 0), "restrict media protocols");
        check(avformat_open_input(&d.format, path.c_str(), nullptr, &d.options), "open media");
    };
    open(false);
    check(avformat_find_stream_info(d.format, nullptr), "read streams");
    if (d.format->start_time != AV_NOPTS_VALUE)
        d.origin = av_rescale_q(d.format->start_time, AV_TIME_BASE_Q, AVRational{1, 16000});
    else
        for (unsigned i = 0; i < d.format->nb_streams; ++i) {
            auto* candidate = d.format->streams[i];
            if (candidate->start_time != AV_NOPTS_VALUE) {
                auto start =
                    av_rescale_q(candidate->start_time, candidate->time_base, AVRational{1, 16000});
                d.origin = d.origin == AV_NOPTS_VALUE ? start : std::min(d.origin, start);
            }
        }
    if (stream >= 0) {
        if (static_cast<unsigned>(stream) >= d.format->nb_streams ||
            d.format->streams[stream]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
            throw UsageError("--audio-stream does not select an audio stream: " +
                             std::to_string(stream));
        d.stream = stream;
    } else
        d.stream = av_find_best_stream(d.format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (d.stream < 0)
        throw std::runtime_error("No audio stream in media file: " + path.string());
    auto* selected = d.format->streams[d.stream];
    if (selected->duration != AV_NOPTS_VALUE && selected->duration > 0) {
        auto start = selected->start_time != AV_NOPTS_VALUE && d.origin != AV_NOPTS_VALUE
                         ? selected->start_time * av_q2d(selected->time_base) - d.origin / 16000.0
                         : 0;
        d.estimated_duration =
            std::max(0.0, start + selected->duration * av_q2d(selected->time_base));
    } else if (d.format->duration != AV_NOPTS_VALUE && d.format->duration > 0)
        d.estimated_duration = d.format->duration / double(AV_TIME_BASE);
    if (std::string(d.format->iformat->name) == "mpegts") {
        // Metadata probing needs normal timestamp filling. Decode in a fresh context
        // so queued probe packets cannot carry stale sample-rate extrapolations.
        auto id = selected->id;
        avformat_close_input(&d.format);
        av_dict_free(&d.options);
        open(true);
        {
            DecodeProbeLogging logging(d.format);
            check(avformat_find_stream_info(d.format, nullptr), "read streams");
        }
        if (static_cast<unsigned>(d.stream) >= d.format->nb_streams ||
            d.format->streams[d.stream]->id != id ||
            d.format->streams[d.stream]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
            throw std::runtime_error("Audio stream changed while probing");
    }
    const auto* parameters = d.format->streams[d.stream]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(parameters->codec_id);
    if (!codec)
        throw std::runtime_error("unsupported audio codec");
    d.codec = avcodec_alloc_context3(codec);
    if (!d.codec)
        throw std::bad_alloc();
    check(avcodec_parameters_to_context(d.codec, parameters), "codec parameters");
    d.codec->pkt_timebase = d.format->streams[d.stream]->time_base;
    check(avcodec_open2(d.codec, codec, nullptr), "open codec");
}
AudioReader::~AudioReader() = default;
int AudioReader::stream_index() const { return impl->stream; }
double AudioReader::duration() const { return impl->adjusted ? 0 : impl->estimated_duration; }
std::vector<float> AudioReader::read(size_t limit) {
    if (!limit || limit > 600 * 16000)
        throw std::runtime_error("Invalid audio read size");
    check_cancelled();
    std::vector<float> output;
    output.reserve(limit);
    auto& d = *impl;
    while (output.size() < limit) {
        if (!d.silence && d.offset == d.pending.size())
            d.next();
        if (d.silence) {
            auto count = static_cast<size_t>(std::min<int64_t>(limit - output.size(), d.silence));
            output.insert(output.end(), count, 0.0f);
            d.silence -= count;
            continue;
        }
        if (d.offset == d.pending.size())
            break;
        auto count = std::min(limit - output.size(), d.pending.size() - d.offset);
        output.insert(output.end(), d.pending.begin() + d.offset,
                      d.pending.begin() + d.offset + count);
        d.offset += count;
    }
    return output;
}
void configure_audio_logging(bool verbose) {
    av_log_set_level(verbose ? AV_LOG_VERBOSE : AV_LOG_WARNING);
    av_log_set_callback([](void* context, int level, const char* format, va_list args) {
        try {
            if (level > av_log_get_level())
                return;
            char line[2048];
            vsnprintf(line, sizeof(line), format, args);
            auto message = trim(line);
            auto severity = level <= AV_LOG_ERROR     ? LogLevel::error
                            : level <= AV_LOG_WARNING ? LogLevel::warning
                                                      : LogLevel::debug;
            if (level == AV_LOG_WARNING &&
                message.rfind("Estimating duration from bitrate", 0) == 0)
                severity = LogLevel::debug;
            if (context && context == decode_probe && level == AV_LOG_WARNING &&
                message.rfind("start time for stream ", 0) == 0 &&
                message.find("is not set in estimate_timings_from_pts") != std::string::npos)
                severity = LogLevel::debug;
            if (!message.empty())
                log_message(severity, "FFmpeg: " + message);
        } catch (...) {
        }
    });
}
std::string audio_backend_version() {
    return std::to_string(avformat_version()) + "/" + std::to_string(avcodec_version()) + "/" +
           std::to_string(swresample_version()) + "/" + std::to_string(avutil_version());
}
Json audio_diagnostics() {
    auto version = [](unsigned value) {
        return std::to_string(AV_VERSION_MAJOR(value)) + "." +
               std::to_string(AV_VERSION_MINOR(value)) + "." +
               std::to_string(AV_VERSION_MICRO(value));
    };
    return {{"version", av_version_info()},
            {"avformat", version(avformat_version())},
            {"avcodec", version(avcodec_version())},
            {"avutil", version(avutil_version())},
            {"swresample", version(swresample_version())}};
}
} // namespace wt
