#include "inference/runtime.hpp"
#include "support/test.hpp"

using namespace wt;
using namespace wt::test;
namespace {
const InferenceDevice cpu{DeviceKind::cpu, "cpu", "CPU", "test CPU"};
const InferenceDevice vk{DeviceKind::gpu, "vulkan", "Vulkan0", "test Vulkan GPU"};
const InferenceDevice cuda{DeviceKind::gpu, "cuda", "CUDA0", "test CUDA GPU"};
const InferenceDevice metal{DeviceKind::gpu, "metal", "MTL0", "test Metal GPU"};
void explicit_selection(const fs::path&) {
    auto selected = choose_device("cpu", {vk, cpu, cuda});
    require(selected.backend == "cpu" && selected.gpu_index == -1);
    require(choose_device("vulkan", {cpu, vk}).gpu_index == 0);
    require(choose_device("cuda", {vk, cpu, cuda}).gpu_index == 1);
    require(choose_device("vulkan", {cuda, cpu, vk}).gpu_index == 1);
    auto second = vk;
    second.name = "Vulkan1";
    selected = choose_device("vulkan", {cuda, cpu, vk, second});
    require(selected.gpu_index == 1 && selected.name == "Vulkan0");
    require(selected.description == vk.description);
    InferenceDevice other_gpu{DeviceKind::gpu, "future", "GPU", "unknown GPU"};
    InferenceDevice accel{DeviceKind::other, "ACCEL", "ACCEL", "accelerator"};
    require(choose_device("vulkan", {other_gpu, cpu, accel, vk}).gpu_index == 1);
}
void automatic_selection(const fs::path&) {
    require(choose_device("auto", {cpu}).backend == "cpu");
    require(choose_device("auto", {cpu, vk}).backend == "cpu");
    require(choose_device("auto", {cpu, vk, metal}).backend == "metal");
    auto selected = choose_device("auto", {vk, metal, cpu, cuda});
    require(selected.backend == "cuda" && selected.gpu_index == 2);
}
void unavailable(const fs::path&) {
    rejects([] { choose_device("vulkan", {cpu}); }, "WT_VULKAN=ON");
    rejects([] { choose_device("cuda", {cpu, vk}); }, "CUDA requested but unavailable");
    rejects([] { choose_device("metal", {cpu}); }, "Metal requested but unavailable");
    rejects([] { choose_device("vulkan", {vk}); }, "No compatible CPU backend");
    rejects([] { choose_device("auto", {}); }, "No compatible CPU backend");
    rejects<std::invalid_argument>([] { choose_device("unknown", {cpu}); }, "Unknown inference");
}
} // namespace
int main() {
    return run_tests({{"explicit GPU ordinal and first matching device", explicit_selection},
                      {"automatic selection keeps Vulkan opt-in", automatic_selection},
                      {"unavailable backend does not fall back", unavailable}});
}
