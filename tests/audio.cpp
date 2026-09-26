#include "app.hpp"
#include <cmath>
#include <iostream>
#include <unistd.h>

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
} // namespace
int main() {
    char pattern[] = "/tmp/whisper-audio-XXXXXX";
    auto created = mkdtemp(pattern);
    if (!created)
        return 1;
    fs::path root = created;
    try {
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
