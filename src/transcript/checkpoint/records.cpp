#include "transcript/checkpoint/detail/records.hpp"
#include "support/cancel.hpp"
#include "support/hash.hpp"
#include "transcript/checkpoint/detail/storage.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace wt::checkpoint_detail {
void validate_segment(const Segment& segment, double from, double to) {
    if (!std::isfinite(segment.start) || !std::isfinite(segment.end) ||
        !std::isfinite(segment.no_speech_probability) || segment.start < from ||
        segment.end < segment.start || segment.end > to)
        throw std::runtime_error("Invalid checkpoint segment");
}
void add_language(std::vector<std::string>& languages, const std::string& language) {
    if (language.empty())
        return;
    if (language.size() > 16 || !std::all_of(language.begin(), language.end(), [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || c == '-';
        }))
        throw std::runtime_error("Invalid checkpoint language");
    if (std::find(languages.begin(), languages.end(), language) == languages.end())
        languages.push_back(language);
    if (languages.size() > 256)
        throw std::runtime_error("Too many checkpoint languages");
}
void validate_manifest(const Json& state) {
    if (state.at("schema_version") != 3)
        throw std::runtime_error("Incompatible checkpoint schema; finish with the previous "
                                 "binary or restart explicitly with --overwrite");
    if (!state.at("fingerprint").is_object() || !state.at("chunks").is_number_integer() ||
        state.at("chunks").get<int64_t>() < 0 || !state.at("samples").is_number_integer() ||
        state.at("samples").get<int64_t>() < 0 || !state.at("languages").is_array() ||
        !state.at("finished").is_boolean() || !state.at("published_hashes").is_object() ||
        !state.at("output_metadata").at("model").is_string() ||
        !state.at("output_metadata").at("run").is_object())
        throw std::runtime_error("Invalid checkpoint manifest");
}
void visit_records(const fs::path& directory, const Json& state, Privacy& privacy,
                   const std::function<void(const Segment&)>& consumer) {
    int64_t position = 0;
    std::string hash;
    std::vector<std::string> detected;
    auto limit =
        state.at("fingerprint").at("run").at("chunk_seconds").get<int>() * int64_t(sample_rate);
    for (int64_t i = 0; i < state.at("chunks").get<int64_t>(); ++i) {
        check_cancelled();
        auto record = read_record(directory / ("chunk-" + std::to_string(i) + ".json"), privacy);
        auto end = record.at("end_sample").get<int64_t>();
        auto lang = record.at("language").get<std::string>();
        if (record.at("index") != i || record.at("start_sample") != position || end <= position ||
            end - position > limit || record.at("previous_hash") != hash ||
            !record.at("segments").is_array())
            throw std::runtime_error("Corrupt checkpoint sequence");
        for (const auto& item : record.at("segments")) {
            Segment segment{item.at("start").get<double>(), item.at("end").get<double>(),
                            item.at("text").get<std::string>(),
                            item.at("no_speech_prob").get<double>(), lang};
            validate_segment(segment, position / double(sample_rate), end / double(sample_rate));
            if (segment.text.empty() || lang.empty())
                throw std::runtime_error("Corrupt checkpoint text");
            consumer(segment);
        }
        if (record.at("segments").empty() && !lang.empty())
            throw std::runtime_error("Language attached to a silent chunk");
        add_language(detected, lang);
        hash = sha256_text(record.dump());
        position = end;
    }
    if (position != state.at("samples").get<int64_t>() || hash != state.at("last_hash") ||
        detected != state.at("languages").get<std::vector<std::string>>())
        throw std::runtime_error("Checkpoint commit mismatch");
}
} // namespace wt::checkpoint_detail
