#include "audio/detail/error.hpp"
#include "support/cancel.hpp"
#include <stdexcept>
#include <string>
extern "C" {
#include <libavutil/error.h>
}

namespace wt::audio_detail {
void check(int code, const char* operation) {
    check_cancelled();
    if (code < 0) {
        char message[AV_ERROR_MAX_STRING_SIZE];
        av_strerror(code, message, sizeof(message));
        throw std::runtime_error(std::string(operation) + ": " + message);
    }
}
} // namespace wt::audio_detail
