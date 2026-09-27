#include "transcript/metadata.hpp"
#include "support/hash.hpp"
#include "support/io.hpp"
#include "support/options.hpp"
#include "transcript/types.hpp"
#include "version.hpp"

namespace wt {
Json run_metadata(const Options& options) {
    return {{"backend", "whisper.cpp"},
            {"version", WT_VERSION},
            {"source_revision", WT_SOURCE_REVISION},
            {"source_dirty", Json::parse(WT_SOURCE_DIRTY_JSON)},
            {"backend_revision", WT_WHISPER_REVISION},
            {"device", options.device},
            {"beam_size", options.beam_size},
            {"cpu_threads", options.cpu_threads},
            {"audio_stream", options.audio_stream},
            {"text_layout", options.text_layout},
            {"paragraph_pause_ms", options.paragraph_pause_ms},
            {"vad", !options.no_vad},
            {"vad_min_silence_ms", options.vad_min_silence_ms},
            {"flash_attention", true},
            {"chunk_seconds", options.chunk_seconds},
            {"chunk_min_silence_ms", options.chunk_min_silence_ms},
            {"audio_timeline_version", 3},
            {"audio_decode_version", 1},
            {"decode_errors", options.decode_errors},
            {"decode_error_limit_seconds", options.decode_error_limit_seconds},
            {"timestamp_gaps", options.timestamp_gaps},
            {"rendering_version", 1},
            {"chunking_version", chunking_version}};
}
Json job_destinations(const Job& job) {
    Json result = Json::object();
    for (const auto& [format, path] : job.outputs)
        result[format] = resolve_path(path).string();
    return result;
}
Json job_fingerprint(const Job& job, const Options& options, const Json& backend) {
    auto size = fs::file_size(job.source);
    auto modified = fs::last_write_time(job.source);
    auto hash = sha256(job.source);
    if (size != fs::file_size(job.source) || modified != fs::last_write_time(job.source))
        throw std::runtime_error("Input changed while hashing");
    auto compatibility = run_metadata(options);
    for (const auto* key : {"cpu_threads", "version", "source_revision", "source_dirty"})
        compatibility.erase(key);
    return {{"source", job.source.string()},
            {"size", size},
            {"sha256", hash},
            {"outputs", job_destinations(job)},
            {"run", compatibility},
            {"language", options.language},
            {"backend", backend}};
}
} // namespace wt
