#include "support/options.hpp"
#include "support/test.hpp"
#include "transcript/metadata.hpp"
#include "version.hpp"

using namespace wt;
using namespace wt::test;
namespace {
void defaults(const fs::path&) {
    Options o;
    require(o.command == "transcribe" && o.inputs.empty() && o.input_dir.empty() &&
            o.output.empty() && o.output_dir.empty());
    require(o.format == "text" && o.naming == "source" && o.prefix == "result");
    require(o.model == "small" && o.language == "ru" && o.vad_model.empty() &&
            o.download_root.empty());
    require(!o.resume && !o.overwrite && !o.skip_existing && !o.continue_on_error &&
            !o.local_files_only && !o.json && !o.quiet && !o.verbose);
    const Json expected = {{"backend", "whisper.cpp"},
                           {"version", WT_VERSION},
                           {"source_revision", WT_SOURCE_REVISION},
                           {"source_dirty", Json::parse(WT_SOURCE_DIRTY_JSON)},
                           {"backend_revision", WT_WHISPER_REVISION},
                           {"device", "auto"},
                           {"beam_size", 5},
                           {"cpu_threads", 0},
                           {"audio_stream", -1},
                           {"text_layout", "paragraphs"},
                           {"paragraph_pause_ms", 2000},
                           {"vad", true},
                           {"vad_min_silence_ms", 2000},
                           {"flash_attention", true},
                           {"chunk_seconds", 120},
                           {"chunk_min_silence_ms", 200},
                           {"audio_timeline_version", 3},
                           {"audio_decode_version", 1},
                           {"decode_errors", "tolerant"},
                           {"decode_error_limit_seconds", 30},
                           {"timestamp_gaps", "auto"},
                           {"rendering_version", 1},
                           {"chunking_version", 4}};
    require(run_metadata(o) == expected, "CLI defaults or persisted run metadata changed");
}
} // namespace
int main() { return run_tests({{"CLI defaults and complete run metadata", defaults}}); }
