#include "transcript/journal.hpp"
#include "transcript/outputs.hpp"

namespace wt {
void publish_outputs(const Job& job, const Options& options, Journal& journal) {
    TranscriptSource source{journal.samples(), journal.languages(),
                            [&](const SegmentConsumer& consume) { journal.visit(consume); }};
    journal.publish(job, options, [&](auto& out, const auto& format) {
        render_stream(out, format, job, options, source);
    });
}
} // namespace wt
