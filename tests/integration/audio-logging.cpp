#include "audio/audio.hpp"
#include "support/report.hpp"
#include "support/test.hpp"
#include <iostream>
#include <sstream>
extern "C" {
#include <libavutil/log.h>
}

using namespace wt;
using namespace wt::test;
namespace {
void logging(const fs::path&) {
    std::ostringstream output;
    {
        StreamCapture capture(std::cerr, output.rdbuf());
        for (bool verbose : {false, true}) {
            configure_reporting(!verbose, verbose);
            configure_audio_logging(verbose);
            av_log(nullptr, AV_LOG_TRACE, "hidden-trace\n");
            av_log(nullptr, AV_LOG_DEBUG, "hidden-debug\n");
            av_log(nullptr, AV_LOG_WARNING, "visible-warning\n");
            av_log(nullptr, AV_LOG_ERROR, "visible-error\n");
            av_log(nullptr, AV_LOG_VERBOSE, "verbose-details\n");
            av_log(nullptr, AV_LOG_WARNING,
                   "Estimating duration from bitrate, this may be inaccurate\n");
            if (!verbose) {
                require(output.str().find("Estimating") == std::string::npos);
                require(output.str().find("verbose-details") == std::string::npos);
            }
        }
    }
    auto text = output.str();
    require(text.find("hidden-") == std::string::npos);
    require(text.find("verbose-details") != std::string::npos);
    require(text.find("Estimating") != std::string::npos);
    require(text.find("FFmpeg: visible-warning") != std::string::npos);
    require(text.find("FFmpeg: visible-error") != std::string::npos);
    configure_reporting(false, false);
    configure_audio_logging();
    require(format_seconds(22.33) == "22.3");
}
} // namespace
int main() { return run_tests({{"FFmpeg verbosity and benign diagnostics", logging}}); }
