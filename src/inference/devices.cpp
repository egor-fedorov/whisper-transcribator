#include "inference/runtime.hpp"
#include <stdexcept>

namespace wt {
DeviceSelection choose_device(const std::string& requested,
                              const std::vector<InferenceDevice>& devices) {
    DeviceSelection cpu;
    std::vector<DeviceSelection> gpus;
    for (const auto& device : devices) {
        if (device.kind == DeviceKind::cpu && cpu.backend.empty())
            cpu = {"cpu", device.name, device.description, -1};
        if (device.kind == DeviceKind::gpu) {
            // whisper.cpp counts all GPU/IGPU devices, including other backend families.
            gpus.push_back(
                {device.backend, device.name, device.description, static_cast<int>(gpus.size())});
        }
    }
    if (cpu.backend.empty())
        throw std::runtime_error("No compatible CPU backend; check the installed libggml-cpu "
                                 "plugins and their dependencies");
    if (requested == "cpu")
        return cpu;
    if (requested == "auto") {
        // Experimental Vulkan is opt-in; keep the established automatic selection policy.
        for (const char* backend : {"cuda", "metal"})
            for (const auto& gpu : gpus)
                if (gpu.backend == backend)
                    return gpu;
        return cpu;
    }
    for (const auto& gpu : gpus)
        if (gpu.backend == requested)
            return gpu;
    if (requested == "vulkan")
        throw std::runtime_error("Vulkan requested but unavailable; build with WT_VULKAN=ON "
                                 "and check the Vulkan loader and GPU driver with vulkaninfo");
    if (requested == "metal")
        throw std::runtime_error(
            "Metal requested but unavailable; use a macOS build on a Mac with a Metal GPU");
    if (requested == "cuda")
        throw std::runtime_error(
            "CUDA requested but unavailable; use a CUDA build and check the NVIDIA driver/runtime");
    throw std::invalid_argument("Unknown inference device: " + requested);
}
} // namespace wt
