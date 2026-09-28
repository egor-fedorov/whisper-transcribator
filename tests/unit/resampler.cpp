#include "audio/resampler.hpp"
#include "support/test.hpp"
#include <cmath>
#include <memory>
extern "C" {
#include <libavutil/frame.h>
}
using namespace wt;
using namespace wt::test;
namespace {
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
void frame_changes(const fs::path&) {
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
        FrameResampler reset;
        auto first = reset.convert(*frame);
        auto delay = reset.delay();
        reset.reset();
        require(reset.delay() == 0 && reset.drain().empty());
        compare(reset.convert(*frame), first);
        require(reset.delay() == delay, "reset must restore a fresh resampler");
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
int main() {
    return run_tests({{"resampler parameter changes and invalid frames", frame_changes}});
}
