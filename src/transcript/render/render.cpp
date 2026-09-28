#include "transcript/render/render.hpp"
#include "transcript/render/detail/formatters.hpp"
#include <stdexcept>

namespace wt {
void render_stream(std::ostream& stream, const std::string& format, const Job& job,
                   const RenderOptions& options, const TranscriptSource& source) {
    bool any = false;
    if (format == "text")
        any = render_detail::render_text(stream, options, source);
    else if (format == "json")
        any = render_detail::render_json(stream, job, options, source);
    else if (format == "srt")
        any = render_detail::render_srt(stream, source);
    else if (format == "vtt")
        any = render_detail::render_vtt(stream, source);
    if (!any)
        throw std::runtime_error("No transcript produced for: " + job.source.string());
}
} // namespace wt
