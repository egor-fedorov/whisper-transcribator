#include "transcript/metadata.hpp"
#include "support/hash.hpp"
#include "support/io.hpp"
#include "transcript/types.hpp"
#include "version.hpp"

namespace wt {
Json run_metadata(const InferenceOptions& inference, const AudioOptions& audio,
                  const ChunkOptions& chunking, const RenderOptions& rendering,
                  const std::string& device) {
    return {{"backend", "whisper.cpp"},
            {"version", WT_VERSION},
            {"source_revision", WT_SOURCE_REVISION},
            {"source_dirty", Json::parse(WT_SOURCE_DIRTY_JSON)},
            {"backend_revision", WT_WHISPER_REVISION},
            {"device", device},
            {"beam_size", inference.beam_size},
            {"cpu_threads", inference.cpu_threads},
            {"audio_stream", audio.stream},
            {"text_layout", rendering.text_layout},
            {"paragraph_pause_ms", rendering.paragraph_pause_ms},
            {"vad", !inference.no_vad},
            {"vad_min_silence_ms", inference.vad_min_silence_ms},
            {"flash_attention", true},
            {"chunk_seconds", chunking.chunk_seconds},
            {"chunk_min_silence_ms", chunking.chunk_min_silence_ms},
            {"audio_timeline_version", 3},
            {"audio_decode_version", 1},
            {"decode_errors", audio.errors.strict ? "strict" : "tolerant"},
            {"decode_error_limit_seconds", audio.errors.limit_seconds},
            {"timestamp_gaps", audio.gaps == TimestampGaps::preserve ? "preserve" : "auto"},
            {"rendering_version", 1},
            {"chunking_version", chunking_version}};
}
Json job_destinations(const Job& job) {
    Json result = Json::object();
    for (const auto& [format, path] : job.outputs)
        result[format] = resolve_path(path).string();
    return result;
}
Json job_fingerprint(const Job& job, const Json& run, const std::string& language,
                     const Json& backend) {
    auto size = fs::file_size(job.source);
    auto modified = fs::last_write_time(job.source);
    auto hash = sha256(job.source);
    if (size != fs::file_size(job.source) || modified != fs::last_write_time(job.source))
        throw std::runtime_error("Input changed while hashing");
    auto compatibility = run;
    for (const auto* key : {"cpu_threads", "version", "source_revision", "source_dirty"})
        compatibility.erase(key);
    return {{"source", job.source.string()},
            {"size", size},
            {"sha256", hash},
            {"outputs", job_destinations(job)},
            {"run", compatibility},
            {"language", language},
            {"backend", backend}};
}
} // namespace wt
