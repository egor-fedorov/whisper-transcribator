#pragma once
#include <string>
#include <vector>

namespace wt {
struct Options {
    std::string command = "transcribe";
    std::vector<std::string> inputs;
    std::string input_dir, output, output_dir;
    std::string format = "text", naming = "source", prefix = "result";
    std::string text_layout = "paragraphs";
    int paragraph_pause_ms = 2000;
    std::string model = "small", download_root, language = "ru", device = "auto", vad_model;
    int cpu_threads = 0, beam_size = 5, vad_min_silence_ms = 2000;
    int chunk_seconds = 120;
    int chunk_min_silence_ms = 200;
    int audio_stream = -1;
    std::string timestamp_gaps = "auto";
    bool resume = false;
    bool overwrite = false, skip_existing = false, continue_on_error = false;
    bool local_files_only = false, no_vad = false, json = false;
    bool quiet = false, verbose = false;
};
} // namespace wt
