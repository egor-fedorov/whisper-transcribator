#include "audio/reader.hpp"
#include "support/io.hpp"
#include "support/wav.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>

using namespace wt;
using namespace wt::test;
int main(int argc, char** argv) {
    if (argc != 4 || (std::string(argv[1]) != "--repeat" && std::string(argv[1]) != "--silence")) {
        std::cerr << "Usage: wt-audio-fixture --repeat|--silence SAMPLE OUTPUT\n";
        return 2;
    }
    try {
        AudioReader reader(argv[2]);
        auto pcm = reader.read(30 * 16000);
        if (pcm.empty() || !reader.read(1).empty())
            throw std::runtime_error("fixture must be under 30 seconds");
        if (std::string(argv[1]) == "--silence")
            std::fill(pcm.begin(), pcm.end(), 0.0f);
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

    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
