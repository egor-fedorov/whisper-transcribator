#include "app/commands.hpp"
#include "audio/audio.hpp"
#include "inference/runtime.hpp"
#include "models/models.hpp"
#include "support/cpu.hpp"
#include "support/options.hpp"
#include "version.hpp"

namespace wt {
Json doctor(const Options& options) {
    configure_inference_logging();
    configure_audio_logging(options.verbose);
    Json result = {
        {"version", WT_VERSION},
        {"source_revision", WT_SOURCE_REVISION},
        {"source_dirty", Json::parse(WT_SOURCE_DIRTY_JSON)},
        {"ffmpeg", audio_diagnostics()},
        {"backend", "whisper.cpp"},
        {"backend_version", inference_backend_version()},
        {"backend_revision", WT_WHISPER_REVISION},
        {"model_cache", model_root(options).string()},
        {"cpu_threads", options.cpu_threads > 0 ? options.cpu_threads : automatic_cpu_threads()},
        {"errors", Json::array()}};
    try {
        result["device"] = select_device(options.device);
        result.update(inference_diagnostics());
    } catch (const std::exception& error) {
        result["errors"].push_back(error.what());
    }
    return result;
}
} // namespace wt
