#include "app.hpp"
#include "resampler.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <unistd.h>
extern "C" {
#include <libavutil/frame.h>
}

using namespace wt;
namespace {
void little(std::string& bytes, unsigned value, int size) {
    for (int i = 0; i < size; ++i)
        bytes += static_cast<char>((value >> (8 * i)) & 255);
}
std::string wav() {
    std::string bytes = "RIFF";
    little(bytes, 36 + 48000 * 4, 4);
    bytes += "WAVEfmt ";
    little(bytes, 16, 4);
    little(bytes, 1, 2);
    little(bytes, 2, 2);
    little(bytes, 48000, 4);
    little(bytes, 48000 * 4, 4);
    little(bytes, 4, 2);
    little(bytes, 16, 2);
    bytes += "data";
    little(bytes, 48000 * 4, 4);
    for (unsigned i = 0; i < 48000; ++i) {
        little(bytes, 1000, 2);
        little(bytes, 1000, 2);
    }
    return bytes;
}
void require(bool value, const std::string& message = "audio assertion failed") {
    if (!value)
        throw std::runtime_error(message);
}
std::vector<float> decode_audio(const fs::path& path) {
    AudioReader reader(path);
    std::vector<float> result;
    while (true) {
        auto block = reader.read(113);
        require(block.size() <= 113);
        if (block.empty())
            return result;
        result.insert(result.end(), block.begin(), block.end());
    }
}
void append(std::vector<float>& out, const std::vector<float>& part) {
    out.insert(out.end(), part.begin(), part.end());
}
void drain(FrameResampler& resampler, std::vector<float>& out) {
    while (true) {
        auto part = resampler.drain();
        if (part.empty())
            break;
        append(out, part);
    }
}
void compare(const std::vector<float>& actual, const std::vector<float>& expected) {
    require(actual.size() == expected.size(), "changed-parameter sample count");
    for (size_t i = 0; i < actual.size(); ++i)
        require(std::isfinite(actual[i]) && std::abs(actual[i] - expected[i]) < 0.00001,
                "changed-parameter PCM mismatch");
}
void frame_changes() {
    FrameResampler changing;
    require(changing.drain().empty());
    std::vector<float> actual, expected;
    const int rates[] = {48000, 16000, 44100, 48000, 48000};
    const int channels[] = {6, 1, 2, 6, 6};
    const AVSampleFormat formats[] = {AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_FLTP, AV_SAMPLE_FMT_S16,
                                      AV_SAMPLE_FMT_FLT, AV_SAMPLE_FMT_FLTP};
    for (int i = 0; i < 5; ++i) {
        auto release = [](AVFrame* frame) { av_frame_free(&frame); };
        std::unique_ptr<AVFrame, decltype(release)> frame(av_frame_alloc(), release);
        require(bool(frame));
        frame->sample_rate = rates[i];
        frame->format = formats[i];
        frame->nb_samples = rates[i] / 10 + 7;
        av_channel_layout_default(&frame->ch_layout, channels[i]);
        require(av_frame_get_buffer(frame.get(), 0) >= 0);
        int planes = av_sample_fmt_is_planar(formats[i]) ? channels[i] : 1;
        int count = frame->nb_samples * channels[i] / planes;
        for (int plane = 0; plane < planes; ++plane)
            for (int j = 0; j < count; ++j) {
                if (formats[i] == AV_SAMPLE_FMT_S16)
                    reinterpret_cast<int16_t*>(frame->extended_data[plane])[j] = 4096;
                else
                    reinterpret_cast<float*>(frame->extended_data[plane])[j] = 0.125f;
            }
        FrameResampler reference;
        append(expected, reference.convert(*frame));
        drain(reference, expected);
        append(actual, changing.convert(*frame));
    }
    drain(changing, actual);
    compare(actual, expected);
    require(actual.size() > 8000 && actual.size() < 8100);
    AVFrame invalid{};
    bool failed = false;
    try {
        changing.convert(invalid);
    } catch (const std::runtime_error&) {
        failed = true;
    }
    require(failed);
}
} // namespace
int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--streams") {
        AudioReader selected(argv[2]);
        require(selected.stream_index() == 1 && selected.duration() > 0);
        require(selected.read(3 * 16000).size() == 2 * 16000);
        AudioReader first(argv[2], 0);
        require(first.read(3 * 16000).size() == 16000);
        bool rejected = false;
        try {
            AudioReader invalid(argv[2], 99);
        } catch (const UsageError&) {
            rejected = true;
        }
        require(rejected);
        return 0;
    }
    if (argc == 5 && std::string(argv[1]) == "--compare") {
        auto expected = decode_audio(argv[3]);
        auto boundary = expected.size();
        append(expected, decode_audio(argv[4]));
        auto actual = decode_audio(argv[2]);
        require(actual.size() == expected.size(), "MPEG-TS sample count");
        double error = 0, energy = 0;
        for (size_t i = 0; i < actual.size(); ++i) {
            require(std::isfinite(actual[i]), "non-finite MPEG-TS PCM");
            // AAC overlap and noise state need not match two fresh decoders at the splice.
            if (i + 4096 >= boundary && i < boundary + 4096)
                continue;
            error += std::pow(actual[i] - expected[i], 2);
            energy += std::pow(expected[i], 2);
        }
        require(energy > 0 && error / energy < 0.01,
                "MPEG-TS relative PCM error: " + std::to_string(error / energy));
        return 0;
    }
    if (argc == 4 && std::string(argv[1]) == "--repeat") {
        AudioReader reader(argv[2]);
        auto pcm = reader.read(30 * 16000);
        require(!pcm.empty() && reader.read(1).empty(), "fixture must be under 30 seconds");
        unsigned count = static_cast<unsigned>(pcm.size()) * 3 + 8 * 16000;
        std::string bytes = "RIFF";
        little(bytes, 36 + count * 2, 4);
        bytes += "WAVEfmt ";
        little(bytes, 16, 4);
        little(bytes, 1, 2);
        little(bytes, 1, 2);
        little(bytes, 16000, 4);
        little(bytes, 32000, 4);
        little(bytes, 2, 2);
        little(bytes, 16, 2);
        bytes += "data";
        little(bytes, count * 2, 4);
        for (int i = 0; i < 3; ++i) {
            if (i == 2)
                bytes.append(8 * 16000 * 2, '\0');
            for (float sample : pcm)
                little(
                    bytes,
                    static_cast<unsigned>(std::clamp(std::lround(sample * 32768), -32768L, 32767L)),
                    2);
        }
        atomic_write(argv[3], bytes);
        return 0;
    }
    char pattern[] = "/tmp/whisper-audio-XXXXXX";
    auto created = mkdtemp(pattern);
    if (!created)
        return 1;
    fs::path root = created;
    try {
        frame_changes();
        auto path = root / "stereo.wav";
        atomic_write(path, wav());
        auto pcm = decode_audio(path);
        require(pcm.size() == 16000, "sample count: " + std::to_string(pcm.size()));
        // FFmpeg's float stereo-to-mono matrix uses sqrt(1/2) per channel.
        require(std::abs(pcm[8000] - std::sqrt(2.0) * 1000.0 / 32768) < 0.0001,
                "sample value: " + std::to_string(pcm[8000]));
        atomic_write(root / "invalid.mp4", "not a media file");
        bool failed = false;
        try {
            decode_audio(root / "invalid.mp4");
        } catch (const std::runtime_error&) {
            failed = true;
        }
        require(failed);
        failed = false;
        try {
            decode_audio(root / "missing");
        } catch (const std::runtime_error&) {
            failed = true;
        }
        require(failed);
        stop_signal = SIGTERM;
        bool cancelled = false;
        try {
            decode_audio(path);
        } catch (const Cancelled&) {
            cancelled = true;
        }
        stop_signal = 0;
        require(cancelled);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        fs::remove_all(root);
        return 1;
    }
    fs::remove_all(root);
    std::cout << "Audio resampling, invalid/missing media and cancellation passed\n";
}
