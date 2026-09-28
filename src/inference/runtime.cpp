#include "inference/runtime.hpp"
#include "ggml-backend.h"
#include "platform/system.hpp"
#include "support/error.hpp"
#include "support/fs.hpp"
#include "support/io.hpp"
#include "support/report.hpp"
#include "whisper.h"
#include <set>

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
std::string select_device(const std::string& requested) {
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
    bool cpu = false;
    std::set<std::string> gpus;
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        auto dev = ggml_backend_dev_get(i);
        auto type = ggml_backend_dev_type(dev);
        std::string backend = ggml_backend_reg_name(ggml_backend_dev_backend_reg(dev));
        if (type == GGML_BACKEND_DEVICE_TYPE_CPU)
            cpu = true;
        if (type == GGML_BACKEND_DEVICE_TYPE_GPU || type == GGML_BACKEND_DEVICE_TYPE_IGPU) {
            if (backend == "CUDA")
                gpus.insert("cuda");
            if (backend == "MTL")
                gpus.insert("metal");
        }
    }
    if (!cpu)
        throw std::runtime_error("No compatible CPU backend; check the installed libggml-cpu "
                                 "plugins and their dependencies");
    if (requested == "cpu")
        return "cpu";
    if (requested == "auto") {
        // whisper.cpp uses the first GPU; a build has either CUDA or Metal, not both.
        for (const char* gpu : {"cuda", "metal"})
            if (gpus.count(gpu))
                return gpu;
        log_message(LogLevel::info, "No CUDA or Metal GPU available; using CPU");
        return "cpu";
    }
    if (gpus.count(requested))
        return requested;
    if (requested == "metal")
        throw std::runtime_error(
            "Metal requested but unavailable; use a macOS build on a Mac with a Metal GPU");
    throw std::runtime_error(
        "CUDA requested but unavailable; use a CUDA build and check the NVIDIA driver/runtime");
}
void validate_language(const std::string& language) {
    if (language != "auto" && whisper_lang_id(language.c_str()) < 0)
        throw UsageError("Unknown language: " + language);
}
std::string inference_backend_version() { return whisper_version(); }
Json inference_diagnostics() {
    Json result = {{"system_info", whisper_print_system_info()}, {"cpu_backend", nullptr}};
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
