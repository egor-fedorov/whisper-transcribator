#include "audio/reader.hpp"
#include "audio/detail/error.hpp"
#include "audio/detail/logging.hpp"
#include "audio/resampler.hpp"
#include "support/cancel.hpp"
#include "support/error.hpp"
#include "support/report.hpp"
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

namespace wt {
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
    // Input EOF starts flushing; decoder EOF still leaves the resampler tail to drain.
    bool flushing = false, decoded = false, finished = false;
    std::vector<float> pending;
    size_t offset = 0;
    int64_t silence = 0, origin = AV_NOPTS_VALUE, retained_start = 0;
    double estimated_duration = 0, last_frame_duration = 0;
    TimestampGaps gaps = TimestampGaps::automatic;
    bool retained = false, warned_reset = false, warned_gap = false, adjusted = false;
    bool recover(int code, const char* operation) {
        check_cancelled();
        if (!recovery.recover(code))
            return false;
        if (recovery.errors() == 1)
            log_message(LogLevel::warning, "Skipping invalid audio data rejected by the decoder; "
                                           "the transcript may be incomplete");
        char message[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(code, message, sizeof(message));
        log_message(LogLevel::debug, std::string("Recoverable audio decoder error: ") + operation +
                                         ": " + message + "; total " +
                                         std::to_string(recovery.errors()));
        return true;
    }
    ~Impl() {
        av_dict_free(&options);
        av_frame_free(&frame);
        av_packet_free(&packet);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }
    void open_input(const fs::path& path, bool raw_timestamps) {
        format = avformat_alloc_context();
        if (!format)
            throw std::bad_alloc();
        if (raw_timestamps) {
            format->flags |= AVFMT_FLAG_NOFILLIN;
            format->skip_estimate_duration_from_pts = 1;
        }
        format->interrupt_callback = {[](void*) { return stop_signal ? 1 : 0; }, nullptr};
        audio_detail::check(av_dict_set(&options, "protocol_whitelist", "file", 0),
                            "restrict media protocols");
        // FFmpeg takes UTF-8 file names on every system.
        audio_detail::check(
            avformat_open_input(&format, path.u8string().c_str(), nullptr, &options), "open media");
    }
    void probe_stream(const fs::path& path, int requested_stream) {
        audio_detail::check(avformat_find_stream_info(format, nullptr), "read streams");
        if (format->start_time != AV_NOPTS_VALUE)
            origin = av_rescale_q(format->start_time, AV_TIME_BASE_Q, AVRational{1, 16000});
        else
            for (unsigned i = 0; i < format->nb_streams; ++i) {
                auto* candidate = format->streams[i];
                if (candidate->start_time != AV_NOPTS_VALUE) {
                    auto start = av_rescale_q(candidate->start_time, candidate->time_base,
                                              AVRational{1, 16000});
                    origin = origin == AV_NOPTS_VALUE ? start : std::min(origin, start);
                }
            }
        if (requested_stream >= 0) {
            if (static_cast<unsigned>(requested_stream) >= format->nb_streams ||
                format->streams[requested_stream]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
                throw UsageError("--audio-stream does not select an audio stream: " +
                                 std::to_string(requested_stream));
            stream = requested_stream;
        } else
            stream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (stream < 0)
            throw std::runtime_error("No audio stream in media file: " + path.string());
        auto* selected = format->streams[stream];
        if (selected->duration != AV_NOPTS_VALUE && selected->duration > 0) {
            auto start = selected->start_time != AV_NOPTS_VALUE && origin != AV_NOPTS_VALUE
                             ? selected->start_time * av_q2d(selected->time_base) - origin / 16000.0
                             : 0;
            estimated_duration =
                std::max(0.0, start + selected->duration * av_q2d(selected->time_base));
        } else if (format->duration != AV_NOPTS_VALUE && format->duration > 0)
            estimated_duration = format->duration / double(AV_TIME_BASE);
    }
    void reopen_transport(const fs::path& path) {
        if (std::string(format->iformat->name) == "mpegts") {
            // Metadata probing needs normal timestamp filling. Decode in a fresh context
            // so queued probe packets cannot carry stale sample-rate extrapolations.
            auto id = format->streams[stream]->id;
            avformat_close_input(&format);
            av_dict_free(&options);
            open_input(path, true);
            {
                audio_detail::DecodeProbeLogging logging(format);
                audio_detail::check(avformat_find_stream_info(format, nullptr), "read streams");
            }
            if (static_cast<unsigned>(stream) >= format->nb_streams ||
                format->streams[stream]->id != id ||
                format->streams[stream]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO)
                throw std::runtime_error("Audio stream changed while probing");
        }
    }
    void open_decoder() {
        const auto* parameters = format->streams[stream]->codecpar;
        const AVCodec* decoder = avcodec_find_decoder(parameters->codec_id);
        if (!decoder)
            throw std::runtime_error("unsupported audio codec");
        codec = avcodec_alloc_context3(decoder);
        if (!codec)
            throw std::bad_alloc();
        audio_detail::check(avcodec_parameters_to_context(codec, parameters), "codec parameters");
        codec->pkt_timebase = format->streams[stream]->time_base;
        audio_detail::check(avcodec_open2(codec, decoder, nullptr), "open codec");
    }
    void place(int64_t start, std::vector<float> pcm) {
        // Keep the resampler's signed position, but never emit pre-origin samples.
        auto end = std::max<int64_t>(0, timeline.end());
        silence = std::max<int64_t>(0, start - end);
        offset = start < end ? static_cast<size_t>(std::min<int64_t>(end - start, pcm.size())) : 0;
        timeline.advance(start, pcm.size());
        pending = std::move(pcm);
    }
    std::optional<int64_t> frame_timestamp() {
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
        return sample_pts;
    }
    void report_location(const AudioPlacement& location, int64_t delay) {
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
                                               format_seconds(gap / 16000.0) + "s as silence");
            warned_gap = true;
        }
        if (location.discontinuity)
            log_message(LogLevel::debug,
                        "Audio timestamp boundary: " + std::to_string(timeline.end() + delay) +
                            " -> " + std::to_string(location.start) + "; rate " +
                            std::to_string(frame->sample_rate));
    }
    void retain_after_boundary(int64_t start) {
        // Drain the old section first; the decoded frame stays alive until the next step.
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
        retained_start = start;
    }
    void process_frame() {
        recovery.frame();
        if (frame->sample_rate > 0 && frame->nb_samples > 0)
            last_frame_duration = double(frame->nb_samples) / frame->sample_rate;
        auto sample_pts = frame_timestamp();
        auto delay = resampler.delay();
        auto tolerance =
            std::max<int64_t>(1, av_rescale_q_rnd(1, format->streams[stream]->time_base,
                                                  AVRational{1, 16000}, AV_ROUND_UP));
        auto location = timeline.locate(sample_pts, delay, tolerance,
                                        format->iformat->flags & AVFMT_TS_DISCONT, gaps);
        if (location.reset)
            adjusted = true;
        report_location(location, delay);
        if (location.discontinuity) {
            retain_after_boundary(location.start);
            return;
        }
        place(location.start - delay, resampler.convert(*frame));
        av_frame_unref(frame);
    }
    double packet_duration() const {
        double duration = last_frame_duration;
        if (packet->duration > 0)
            duration = packet->duration * av_q2d(format->streams[stream]->time_base);
        else if (codec->sample_rate > 0) {
            auto samples = av_get_audio_frame_duration(codec, packet->size);
            if (samples > 0)
                duration = double(samples) / codec->sample_rate;
        }
        return duration;
    }
    void send_input() {
        int code;
        recovery.awaiting_input();
        if (flushing)
            throw std::runtime_error("Decoder requested input after EOF");
        do {
            av_packet_unref(packet);
            code = av_read_frame(format, packet);
            check_cancelled();
        } while (code >= 0 && packet->stream_index != stream);
        if (code == AVERROR_EOF) {
            flushing = true;
            code = avcodec_send_packet(codec, nullptr);
            if (!recover(code, "flush decoder"))
                audio_detail::check(code, "flush decoder");
        } else {
            audio_detail::check(code, "read packet");
            recovery.packet(packet_duration());
            code = avcodec_send_packet(codec, packet);
            av_packet_unref(packet);
            if (!recover(code, "send packet"))
                audio_detail::check(code, "decode packet");
        }
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
            } else if (decoded) {
                place(timeline.end(), resampler.drain());
                finished = pending.empty();
            } else {
                int code = avcodec_receive_frame(codec, frame);
                if (code >= 0)
                    process_frame();
                else if (code == AVERROR_EOF) {
                    if (recovery.errors())
                        log_message(LogLevel::info, "Audio decoding completed with " +
                                                        std::to_string(recovery.errors()) +
                                                        " recoverable decoder errors");
                    decoded = true;
                } else if (!recover(code, "receive frame")) {
                    if (code != AVERROR(EAGAIN))
                        audio_detail::check(code, "decode frame");
                    send_input();
                }
            }
        }
    }
};
AudioReader::AudioReader(const fs::path& path, int stream, TimestampGaps gaps,
                         DecodeErrorPolicy errors)
    : impl(std::make_unique<Impl>()) {
    check_cancelled();
    auto& d = *impl;
    d.recovery = DecodeRecovery(errors);
    if (!d.frame || !d.packet)
        throw std::bad_alloc();
    d.gaps = gaps;
    d.open_input(path, false);
    d.probe_stream(path, stream);
    d.reopen_transport(path);
    d.open_decoder();
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
} // namespace wt
