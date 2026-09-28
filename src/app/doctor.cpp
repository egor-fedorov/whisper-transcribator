#include "app/commands.hpp"
#include "app/options.hpp"
#include "audio/audio.hpp"
#include "inference/runtime.hpp"
#include "models/models.hpp"
#include "platform/cpu.hpp"
#include "version.hpp"

namespace wt {
Json doctor(const CliOptions& options) {
    configure_inference_logging();
    configure_audio_logging(options.verbose);
    int threads = options.inference.cpu_threads > 0 ? options.inference.cpu_threads
                                                    : platform::automatic_cpu_threads();
    Json result = {{"version", WT_VERSION},
                   {"source_revision", WT_SOURCE_REVISION},
                   {"source_dirty", Json::parse(WT_SOURCE_DIRTY_JSON)},
                   {"ffmpeg", audio_diagnostics()},
                   {"backend", "whisper.cpp"},
                   {"backend_version", inference_backend_version()},
                   {"backend_revision", WT_WHISPER_REVISION},
                   {"model_cache", model_root(options.cache).string()},
                   {"cpu_threads", threads},
                   {"errors", Json::array()}};
    try {
        result.update(inference_diagnostics());
        auto selected = select_device(options.device);
        result["device"] = selected.backend;
        result["selected_device"] = {{"name", selected.name},
                                     {"description", selected.description},
                                     {"gpu_index", selected.gpu_index}};
    } catch (const std::exception& error) {
        result["errors"].push_back(error.what());
    }
    return result;
}
} // namespace wt
