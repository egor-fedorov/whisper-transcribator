#include "inference/runtime.hpp"
#include "ggml-backend.h"
#include "platform/system.hpp"
#include "support/error.hpp"
#include "support/fs.hpp"
#include "support/io.hpp"
#include "support/report.hpp"
#include "whisper.h"

namespace wt {
void configure_inference_logging() {
    whisper_log_set(
        [](ggml_log_level level, const char* text, void*) {
            try {
                static thread_local LogLevel previous = LogLevel::debug;
                if (level != GGML_LOG_LEVEL_CONT)
                    previous = level == GGML_LOG_LEVEL_ERROR  ? LogLevel::error
                               : level == GGML_LOG_LEVEL_WARN ? LogLevel::warning
                                                              : LogLevel::debug;
                if (!trim(text).empty())
                    log_message(previous, trim(text));
            } catch (...) {
            }
        },
        nullptr);
}
std::vector<InferenceDevice> inference_devices() {
    static const bool loaded = [] {
        auto library =
            platform::library_path(reinterpret_cast<const void*>(ggml_backend_dev_count));
        if (library.empty())
            throw std::runtime_error("Cannot locate installed ggml backend directory");
        auto directory = fs::canonical(library).parent_path();
        ggml_backend_load_all_from_path(directory.u8string().c_str());
        return true;
    }();
    (void)loaded;
    std::vector<InferenceDevice> devices;
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        auto dev = ggml_backend_dev_get(i);
        auto type = ggml_backend_dev_type(dev);
        std::string backend = ggml_backend_reg_name(ggml_backend_dev_backend_reg(dev));
        if (backend == "CUDA")
            backend = "cuda";
        else if (backend == "MTL")
            backend = "metal";
        else if (backend == "Vulkan")
            backend = "vulkan";
        else if (backend == "CPU")
            backend = "cpu";
        auto kind = type == GGML_BACKEND_DEVICE_TYPE_CPU ? DeviceKind::cpu
                    : type == GGML_BACKEND_DEVICE_TYPE_GPU || type == GGML_BACKEND_DEVICE_TYPE_IGPU
                        ? DeviceKind::gpu
                        : DeviceKind::other;
        devices.push_back(
            {kind, backend, ggml_backend_dev_name(dev), ggml_backend_dev_description(dev)});
    }
    return devices;
}
DeviceSelection select_device(const std::string& requested) {
    auto selected = choose_device(requested, inference_devices());
    if (requested == "auto" && selected.backend == "cpu")
        log_message(LogLevel::info, "No CUDA or Metal GPU available; using CPU");
    return selected;
}
void validate_language(const std::string& language) {
    if (language != "auto" && whisper_lang_id(language.c_str()) < 0)
        throw UsageError("Unknown language: " + language);
}
std::string inference_backend_version() { return whisper_version(); }
Json inference_diagnostics() {
    Json devices = Json::array();
    for (const auto& device : inference_devices())
        devices.push_back({{"backend", device.backend},
                           {"name", device.name},
                           {"description", device.description}});
    Json result = {{"system_info", whisper_print_system_info()},
                   {"cpu_backend", nullptr},
                   {"available_devices", devices}};
    for (size_t i = 0; i < ggml_backend_reg_count(); ++i) {
        auto reg = ggml_backend_reg_get(i);
        if (std::string(ggml_backend_reg_name(reg)) != "CPU")
            continue;
        auto symbol = ggml_backend_reg_get_proc_address(reg, "ggml_backend_get_features");
        auto library = symbol ? platform::library_path(symbol) : fs::path{};
        if (!library.empty())
            result["cpu_backend"] = library.filename().string();
    }
    return result;
}
} // namespace wt
