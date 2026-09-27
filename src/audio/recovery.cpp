#include "audio/recovery.hpp"
#include <cmath>
#include <stdexcept>
#include <string>
extern "C" {
#include <libavutil/error.h>
}

namespace wt {
namespace {
constexpr const char* restart_hint =
    " Repeating --resume with the same settings will fail again. Changed decode settings "
    "require a fresh output or --overwrite without --resume; alternatively repair the input "
    "with ffmpeg first.";
}
DecodeRecovery::DecodeRecovery(DecodeErrorPolicy value) : policy(value) {
    if (policy.limit_seconds < 0)
        throw std::invalid_argument("Negative audio decode error limit");
}
void DecodeRecovery::packet(double seconds) {
    stalled = 0;
    // Count represented input audio once per packet, not retries or timestamp gaps.
    if (std::isfinite(seconds) && seconds > 0)
        without_frame += seconds;
}
void DecodeRecovery::frame() {
    without_frame = 0;
    stalled = 0;
    damaged = false;
}
void DecodeRecovery::awaiting_input() const {
    if (damaged && policy.limit_seconds && without_frame >= policy.limit_seconds)
        throw std::runtime_error(
            "No successful audio frame after decoder errors for " +
            std::to_string(policy.limit_seconds) +
            " seconds of input audio. Increase --decode-error-limit-seconds or set it to 0 "
            "to disable the time limit." +
            restart_hint);
}
bool DecodeRecovery::recover(int code) {
    if (code >= 0 || code == AVERROR(EAGAIN) || code == AVERROR_EOF || code == AVERROR(ENOMEM) ||
        code == AVERROR_EXIT || code == AVERROR(EINTR))
        return false;
    ++total;
    if (policy.strict) {
        char message[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(code, message, sizeof(message));
        throw std::runtime_error(std::string("Audio decoder error: ") + message +
                                 ". Use --decode-errors tolerant to skip damaged audio." +
                                 restart_hint);
    }
    damaged = true;
    if (++stalled >= 1024)
        throw std::runtime_error(
            std::string("Audio decoder made no progress after 1024 errors without a new "
                        "packet or successful frame; repair the input with ffmpeg.") +
            restart_hint);
    awaiting_input();
    return true;
}
} // namespace wt
