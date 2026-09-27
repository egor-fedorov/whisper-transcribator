#include "audio/audio.hpp"
#include "support/cancel.hpp"
#include "support/error.hpp"
#include "support/io.hpp"
#include "support/report.hpp"
#include "support/test.hpp"
#include "support/wav.hpp"
#include <algorithm>
#include <cmath>
#include <sys/resource.h>

using namespace wt;
using namespace wt::test;
namespace {
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
void reader(const fs::path& root) {
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
}
} // namespace
int main(int argc, char** argv) {
    if (argc == 4 && std::string(argv[1]) == "--timeline") {
        auto actual = decode_audio(argv[2]);
        auto expected = decode_audio(argv[3]);
        AudioReader estimate(argv[2]);
        require(std::abs(estimate.duration() - expected.size() / 16000.0) < 0.1,
                "duration must include delayed audio and gaps");
        require(std::abs(int64_t(actual.size()) - int64_t(expected.size())) <= 32,
                "timeline sample count: " + std::to_string(actual.size()) + " vs " +
                    std::to_string(expected.size()));
        double error = 0, energy = 0;
        for (size_t i = 0; i < std::min(actual.size(), expected.size()); ++i) {
            require(std::isfinite(actual[i]));
            error += std::pow(actual[i] - expected[i], 2);
            energy += std::pow(expected[i], 2);
        }
        require(energy > 0 && error / energy < 0.005,
                "timeline PCM mismatch: " + std::to_string(error / energy));
        return 0;
    }
    if (argc == 3 && std::string(argv[1]) == "--gap") {
        AudioReader reader(argv[2]);
        rusage before{}, after{};
        require(getrusage(RUSAGE_SELF, &before) == 0);
        auto prefix = reader.read(2 * 16000);
        require(prefix.size() == 2 * 16000);
        for (int i = 0; i < 10; ++i) {
            auto gap = reader.read(65536);
            require(gap.size() == 65536);
            require(std::all_of(gap.begin(), gap.end(), [](float x) { return x == 0; }));
        }
        require(getrusage(RUSAGE_SELF, &after) == 0);
        require(after.ru_maxrss <= before.ru_maxrss + 32 * 1024,
                "timestamp gap allocated more than 32 MiB");
        return 0;
    }
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
        require(actual.size() == expected.size(),
                "MPEG-TS sample count: " + std::to_string(actual.size()) + " vs " +
                    std::to_string(expected.size()));
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
    return run_tests({{"bounded decoding, invalid media and cancellation", reader}});
}
