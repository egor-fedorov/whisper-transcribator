#pragma once
#include "support/json.hpp"
#include <string>
#include <vector>

namespace wt {
void configure_inference_logging();
void validate_language(const std::string& language);
std::string inference_backend_version();
enum class DeviceKind { cpu, gpu, other };
struct InferenceDevice {
    DeviceKind kind;
    std::string backend, name, description;
};
struct DeviceSelection {
    std::string backend, name, description;
    int gpu_index = -1;
};
DeviceSelection choose_device(const std::string& requested,
                              const std::vector<InferenceDevice>& devices);
std::vector<InferenceDevice> inference_devices();
DeviceSelection select_device(const std::string& requested);
Json inference_diagnostics();
} // namespace wt
