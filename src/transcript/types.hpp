#pragma once
#include "support/fs.hpp"
#include <map>
#include <string>
#include <vector>

namespace wt {
constexpr int sample_rate = 16000;
struct Job {
    fs::path source;
    std::map<std::string, fs::path> outputs;
};
struct Segment {
    double start, end;
    std::string text;
    double no_speech_probability;
    std::string language = "";
};
struct Transcript {
    std::string language;
    double duration;
    std::vector<Segment> segments;
};
} // namespace wt
