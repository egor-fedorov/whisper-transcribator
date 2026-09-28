#pragma once
#include <string>
#include <vector>

namespace wt {
struct JobOptions {
    std::vector<std::string> inputs;
    std::string input_dir, output, output_dir;
    std::string format = "text", naming = "source", prefix = "result";
    bool skip_existing = false;
};
struct CheckpointOptions {
    bool resume = false, overwrite = false;
};
struct RenderOptions {
    std::string text_layout = "paragraphs";
    int paragraph_pause_ms = 2000;
};
struct ChunkOptions {
    int chunk_seconds = 120;
    int chunk_min_silence_ms = 200;
};
} // namespace wt
