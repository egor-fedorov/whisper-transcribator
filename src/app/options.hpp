#pragma once
#include "audio/options.hpp"
#include "inference/options.hpp"
#include "models/options.hpp"
#include "transcript/options.hpp"
#include <string>

namespace wt {
// Only the application assembles settings across subsystem boundaries.
struct CliOptions {
    std::string command = "transcribe";
    JobOptions jobs;
    CheckpointOptions checkpoint;
    RenderOptions rendering;
    ChunkOptions chunking;
    AudioOptions audio;
    InferenceOptions inference;
    ModelCacheOptions cache;
    std::string model = "small", device = "auto", vad_model;
    bool continue_on_error = false, json = false, quiet = false, verbose = false;
};
} // namespace wt
