#include "transcript/publication.hpp"
#include "transcript/checkpoint/journal.hpp"
#include "transcript/render/render.hpp"

namespace wt {
void publish_outputs(const Job& job, const RenderOptions& options, Journal& journal,
                     bool overwrite) {
    TranscriptSource source{journal.samples(), journal.languages(),
                            [&](const SegmentConsumer& consume) { journal.visit(consume); },
                            journal.output_metadata()};
    journal.publish(job, overwrite, [&](auto& out, const auto& format) {
        render_stream(out, format, job, options, source);
    });
}
} // namespace wt
