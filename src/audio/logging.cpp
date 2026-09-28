#include "audio/detail/logging.hpp"
#include "audio/runtime.hpp"
#include "support/report.hpp"
#include "support/strings.hpp"
#include <cstdio>
extern "C" {
#include <libavutil/log.h>
}

namespace wt {
namespace {
thread_local const void* decode_probe = nullptr;
}
audio_detail::DecodeProbeLogging::DecodeProbeLogging(const void* context) : previous(decode_probe) {
    decode_probe = context;
}
audio_detail::DecodeProbeLogging::~DecodeProbeLogging() { decode_probe = previous; }

void configure_audio_logging(bool verbose) {
    av_log_set_level(verbose ? AV_LOG_VERBOSE : AV_LOG_WARNING);
    av_log_set_callback([](void* context, int level, const char* format, va_list args) {
        try {
            if (level > av_log_get_level())
                return;
            char line[2048];
            vsnprintf(line, sizeof(line), format, args);
            auto message = trim(line);
            auto severity = level <= AV_LOG_ERROR     ? LogLevel::error
                            : level <= AV_LOG_WARNING ? LogLevel::warning
                                                      : LogLevel::debug;
            if (level == AV_LOG_WARNING &&
                message.rfind("Estimating duration from bitrate", 0) == 0)
                severity = LogLevel::debug;
            if (context && context == decode_probe && level == AV_LOG_WARNING &&
                message.rfind("start time for stream ", 0) == 0 &&
                message.find("is not set in estimate_timings_from_pts") != std::string::npos)
                severity = LogLevel::debug;
            if (!message.empty())
                log_message(severity, "FFmpeg: " + message);
        } catch (...) {
        }
    });
}
} // namespace wt
