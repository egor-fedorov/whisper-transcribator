#include "audio/detail/logging.hpp"
#include "audio/runtime.hpp"
#include "support/report.hpp"
#include "support/test.hpp"
#include <iostream>
#include <memory>
#include <sstream>
extern "C" {
#include <libavformat/avformat.h>
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
void probe_logging(const fs::path&) {
    using Context = std::unique_ptr<AVFormatContext, decltype(&avformat_free_context)>;
    Context first(avformat_alloc_context(), avformat_free_context);
    Context second(avformat_alloc_context(), avformat_free_context);
    require(bool(first) && bool(second));
    configure_reporting(false, false);
    configure_audio_logging();
    std::ostringstream output;
    StreamCapture capture(std::cerr, output.rdbuf());
    auto warning = [&](AVFormatContext* context) {
        av_log(context, AV_LOG_WARNING,
               "start time for stream 0 is not set in estimate_timings_from_pts\n");
    };
    {
        audio_detail::DecodeProbeLogging outer(first.get());
        warning(first.get());
        require(output.str().empty());
        warning(second.get());
        require(output.str().find("start time") != std::string::npos);
        output.str("");
        try {
            audio_detail::DecodeProbeLogging inner(second.get());
            warning(second.get());
            require(output.str().empty());
            warning(first.get());
            require(output.str().find("start time") != std::string::npos);
            throw std::runtime_error("probe failed");
        } catch (const std::runtime_error&) {
        }
        output.str("");
        warning(first.get());
        require(output.str().empty(), "probe logging context was not restored");
        av_log(first.get(), AV_LOG_WARNING, "unrelated-warning\n");
        av_log(first.get(), AV_LOG_ERROR,
               "start time for stream 0 is not set in estimate_timings_from_pts\n");
        require(output.str().find("unrelated-warning") != std::string::npos);
        require(output.str().find("start time") != std::string::npos);
    }
    output.str("");
    warning(first.get());
    require(output.str().find("start time") != std::string::npos,
            "metadata re-probe must not suppress subsequent decode warnings");
}
} // namespace
int main() {
    return run_tests({{"FFmpeg verbosity and benign diagnostics", logging},
                      {"scoped probe logging and exception unwinding", probe_logging}});
}
