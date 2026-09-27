#include "inference/runtime.hpp"
#include "ggml-backend.h"
#include "support/error.hpp"
#include "support/fs.hpp"
#include "support/io.hpp"
#include "support/report.hpp"
#include "whisper.h"
#include <dlfcn.h>

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
        Dl_info library{};
        if (!dladdr(reinterpret_cast<void*>(ggml_backend_dev_count), &library) ||
            !library.dli_fname)
            throw std::runtime_error("Cannot locate installed ggml backend directory");
        auto directory = fs::canonical(library.dli_fname).parent_path();
        ggml_backend_load_all_from_path(directory.c_str());
        return true;
    }();
    (void)loaded;
    bool cuda = false, cpu = false;
    for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
        auto dev = ggml_backend_dev_get(i);
        if (ggml_backend_dev_type(dev) == GGML_BACKEND_DEVICE_TYPE_CPU)
            cpu = true;
        if (std::string(ggml_backend_reg_name(ggml_backend_dev_backend_reg(dev))) == "CUDA" &&
            ggml_backend_dev_type(dev) == GGML_BACKEND_DEVICE_TYPE_GPU)
            cuda = true;
    }
    if (!cpu)
        throw std::runtime_error("No compatible CPU backend; check the installed libggml-cpu "
                                 "plugins and their dependencies");
    if (requested == "cpu")
        return "cpu";
    if (cuda)
        return "cuda";
    if (requested == "cuda")
        throw std::runtime_error(
            "CUDA requested but unavailable; use a CUDA build and check the NVIDIA driver/runtime");
    log_message(LogLevel::info, "CUDA unavailable; using CPU");
    return "cpu";
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
        Dl_info library{};
        if (symbol && dladdr(reinterpret_cast<void*>(symbol), &library) && library.dli_fname)
            result["cpu_backend"] = fs::path(library.dli_fname).filename().string();
    }
    return result;
}
} // namespace wt
