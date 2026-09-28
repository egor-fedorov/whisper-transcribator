#pragma once
#include <string>

namespace wt {
struct InferenceOptions {
    std::string language = "ru";
    int cpu_threads = 0, beam_size = 5, vad_min_silence_ms = 2000;
    bool no_vad = false;
};
} // namespace wt
