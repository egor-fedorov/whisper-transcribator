#include "transcript/checkpoint/detail/publication.hpp"
#include "support/cancel.hpp"
#include "support/hash.hpp"
#include "support/io.hpp"
#include "transcript/checkpoint/detail/storage.hpp"
#include "transcript/checkpoint/journal.hpp"
#include <stdexcept>

namespace wt {
namespace checkpoint_detail {
void validate_existing_outputs(const Job& job, const CheckpointOptions& options,
                               const Json& state) {
    for (const auto& [format, path] : job.outputs) {
        if (!exists_entry(path))
            continue;
        if (!fs::is_regular_file(path) || fs::is_symlink(path))
            throw std::runtime_error("Unsafe output: " + path.string());
        const auto& hashes = state.at("published_hashes");
        if (options.resume && state.at("finished").get<bool>() && hashes.contains(format) &&
            sha256(path) == hashes.at(format).get<std::string>())
            continue;
        if (!options.overwrite)
            throw std::runtime_error("Output exists and is not a verified resumed result; use "
                                     "--overwrite: " +
                                     path.string());
    }
}
} // namespace checkpoint_detail
void Journal::publish(const Job& job, bool overwrite, const RenderOutput& render) {
    if (!finished())
        throw std::runtime_error("Cannot publish unfinished transcript");
    Json hashes = Json::object();
    for (const auto& [format, path] : job.outputs) {
        auto staged = directory / ("output." + format);
        atomic_write_stream(
            staged, [&, format = format](auto& out) { render(out, format); }, true, true);
        hashes[format] = sha256(staged);
    }
    if (!state.at("published_hashes").empty() && hashes != state.at("published_hashes"))
        throw std::runtime_error("Reconstructed outputs differ from checkpoint");
    state["published_hashes"] = hashes;
    save();
    for (const auto& [format, path] : job.outputs) {
        check_cancelled();
        if (fs::exists(path) || fs::is_symlink(path)) {
            if (!fs::is_regular_file(path) || fs::is_symlink(path) || same_file(path, job.source))
                throw std::runtime_error("Unsafe output: " + path.string());
            if (sha256(path) == hashes.at(format).get<std::string>())
                continue;
            if (!overwrite)
                throw std::runtime_error("Output changed during transcription: " + path.string());
        }
        publish_file(directory / ("output." + format), path, overwrite);
    }
    // Retire the checkpoint atomically before deleting its individual records.
    auto completed = fs::path(directory.string() + ".completed");
    fs::rename(directory, completed);
    sync_directory(directory.parent_path());
    fs::remove_all(completed);
    sync_directory(directory.parent_path());
}
} // namespace wt
