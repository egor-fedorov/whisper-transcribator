#include "audio/runtime.hpp"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

namespace wt {
std::string audio_backend_version() {
    return std::to_string(avformat_version()) + "/" + std::to_string(avcodec_version()) + "/" +
           std::to_string(swresample_version()) + "/" + std::to_string(avutil_version());
}
Json audio_diagnostics() {
    auto version = [](unsigned value) {
        return std::to_string(AV_VERSION_MAJOR(value)) + "." +
               std::to_string(AV_VERSION_MINOR(value)) + "." +
               std::to_string(AV_VERSION_MICRO(value));
    };
    return {{"version", av_version_info()},
            {"avformat", version(avformat_version())},
            {"avcodec", version(avcodec_version())},
            {"avutil", version(avutil_version())},
            {"swresample", version(swresample_version())}};
}
} // namespace wt
