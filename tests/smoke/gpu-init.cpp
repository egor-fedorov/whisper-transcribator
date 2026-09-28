#include "inference/runtime.hpp"
#include "whisper.h"
#include <iostream>
#include <limits>
#include <memory>

// Model-dependent and driver-independent: run in smoke, never in model-free CTest/package.
int main(int argc, char** argv) {
    if (argc != 2)
        return 2;
    wt::configure_inference_logging();
    wt::select_device("cpu");
    auto params = whisper_context_default_params();
    params.use_gpu = true;
    params.gpu_device = std::numeric_limits<int>::max();
    std::unique_ptr<whisper_context, decltype(&whisper_free)> context(
        whisper_init_from_file_with_params(argv[1], params), whisper_free);
    if (context) {
        std::cerr << "Unavailable GPU silently fell back to CPU\n";
        return 1;
    }
    // Prove that the failure above was not simply an unreadable or corrupt model.
    params.use_gpu = false;
    context.reset(whisper_init_from_file_with_params(argv[1], params));
    if (!context)
        return 1;
    std::cout << "GPU initialization failure rejects CPU fallback\n";
}
