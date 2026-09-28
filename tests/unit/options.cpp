#include "app/configuration.hpp"
#include "support/test.hpp"
#include "transcript/metadata.hpp"
#include "version.hpp"

using namespace wt;
using namespace wt::test;
namespace {
void defaults(const fs::path&) {
    CliOptions o;
    require(o.command == "transcribe" && o.jobs.inputs.empty() && o.jobs.input_dir.empty() &&
            o.jobs.output.empty() && o.jobs.output_dir.empty());
    require(o.jobs.format == "text" && o.jobs.naming == "source" && o.jobs.prefix == "result");
    require(o.model == "small" && o.inference.language == "ru" && o.vad_model.empty() &&
            o.cache.download_root.empty());
    require(!o.checkpoint.resume && !o.checkpoint.overwrite && !o.jobs.skip_existing &&
            !o.continue_on_error && !o.cache.local_files_only && !o.json && !o.quiet && !o.verbose);
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
    require(describe_run(o) == expected, "CLI defaults or persisted run metadata changed");
    require(describe_output(o) == Json({{"model", "small"}, {"run", expected}}));
}
void explicit_settings(const fs::path&) {
    CliOptions o;
    o.inference = {"en", 3, 2, 500, true};
    o.audio = {2, TimestampGaps::preserve, {true, 0}};
    o.chunking = {60, 100};
    o.rendering = {"single-line", 900};
    o.model = "./custom.bin";
    o.device = "cpu";
    const auto run = describe_run(o);
    require(run.at("device") == "cpu" && run.at("cpu_threads") == 3 && run.at("beam_size") == 2);
    require(run.at("audio_stream") == 2 && run.at("timestamp_gaps") == "preserve" &&
            run.at("decode_errors") == "strict" && run.at("decode_error_limit_seconds") == 0);
    require(run.at("vad") == false && run.at("vad_min_silence_ms") == 500 &&
            run.at("chunk_seconds") == 60 && run.at("chunk_min_silence_ms") == 100);
    require(run.at("text_layout") == "single-line" && run.at("paragraph_pause_ms") == 900);
    require(describe_output(o) == Json({{"model", "./custom.bin"}, {"run", run}}));
}
} // namespace
int main() {
    return run_tests({{"CLI defaults and complete run metadata", defaults},
                      {"explicit subsystem settings reach metadata", explicit_settings}});
}
