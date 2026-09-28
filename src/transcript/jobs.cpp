#include "transcript/jobs.hpp"
#include "support/error.hpp"
#include "support/io.hpp"
#include "support/options.hpp"
#include "support/report.hpp"
#include "transcript/journal.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <iomanip>
#include <iostream>
#include <optional>
#include <regex>
#include <set>
#include <sstream>

namespace wt {
namespace {
struct Identity {
    fs::path path;
    platform::FileId inode;
    bool exists = false;
    explicit Identity(const fs::path& canonical) : path(canonical) {
        if (auto st = platform::status(path)) {
            exists = true;
            inode = st->id;
        } else if (errno != ENOENT && errno != ENOTDIR) {
            throw std::runtime_error("Cannot inspect path: " + path.string());
        }
    }
};
struct PathIndex {
    std::set<fs::path> paths;
    std::set<platform::FileId> inodes;
    bool contains(const Identity& id) const {
        return paths.count(id.path) || (id.exists && inodes.count(id.inode));
    }
    void insert(const Identity& id) {
        paths.insert(id.path);
        if (id.exists)
            inodes.insert(id.inode);
    }
};
fs::path output_path(const fs::path& raw) {
    // Resolve directory aliases, but never follow the output's final component.
    auto path = resolve_path(raw.parent_path().empty() ? "." : raw.parent_path()) / raw.filename();
    if (fs::is_symlink(path))
        throw std::runtime_error("Unsafe output symlink: " + path.string());
    return path;
}
struct Mapping {
    fs::path path;
    Json entries;
};
struct JobPlan {
    std::vector<Job> pending;
    std::set<fs::path> directories;
    std::optional<Mapping> mapping;
};
bool mapping_matches(const Mapping& mapping) {
    return nlohmann::json::parse(read_text(mapping.path)) == nlohmann::json(mapping.entries);
}
void publish_mapping(const Mapping& mapping) {
    try {
        atomic_write(mapping.path, mapping.entries.dump(2) + "\n");
    } catch (const std::runtime_error&) {
        if (!fs::exists(mapping.path) || !mapping_matches(mapping))
            throw;
    }
}
void validate_options(const Options& o) {
    if (o.inputs.empty() && o.input_dir.empty())
        throw UsageError("At least one input is required");
    if (!o.inputs.empty() && !o.input_dir.empty())
        throw UsageError("Use files or --input-dir, not both");
    if (!o.output.empty() && (o.inputs.size() != 1 || !o.output_dir.empty()))
        throw UsageError("-o requires one input and no --output-dir");
    if (o.format == "all" && (!o.output.empty() || o.output_dir.empty()))
        throw UsageError("--format all requires --output-dir, without -o");
    if (o.naming == "numbered" && (o.output_dir.empty() || !o.output.empty()))
        throw UsageError("Numbered outputs require --output-dir without -o");
    if (!std::regex_match(o.prefix, std::regex("[A-Za-z0-9_-]+")))
        throw UsageError("Invalid --prefix: use letters, digits, underscore or hyphen");
}
std::vector<fs::path> discover_inputs(const Options& o) {
    std::vector<fs::path> inputs;
    if (!o.input_dir.empty()) {
        auto directory = resolve_path(o.input_dir);
        if (!fs::is_directory(directory))
            throw std::runtime_error("Input directory not found: " + directory.string());
        const std::set<std::string> extensions = {
            ".aac", ".aiff", ".avi",  ".flac", ".m4a",  ".m4b", ".m4v",  ".mkv", ".mov",
            ".mp3", ".mp4",  ".mpeg", ".mpg",  ".oga",  ".ogg", ".opus", ".wav", ".webm",
            ".wma", ".wmv",  ".ts",   ".mts",  ".m2ts", ".mka", ".aif",  ".3gp", ".asf"};
        for (const auto& entry : fs::directory_iterator(directory)) {
            std::string extension = entry.path().extension().string();
            std::transform(extension.begin(), extension.end(), extension.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (entry.is_regular_file() && extensions.count(extension))
                inputs.push_back(entry.path());
        }
        std::sort(inputs.begin(), inputs.end(), [](const auto& a, const auto& b) {
            return a.filename().native() < b.filename().native();
        });
        for (auto& input : inputs)
            input = resolve_path(input);
    } else {
        for (const auto& input : o.inputs)
            inputs.push_back(resolve_path(input));
    }
    return inputs;
}
JobPlan plan_jobs(const Options& o) {
    validate_options(o);
    auto inputs = discover_inputs(o);
    PathIndex sources, outputs;
    for (const auto& path : inputs) {
        if (!fs::is_regular_file(path))
            throw std::runtime_error("Input file not found: " + path.string());
        sources.insert(Identity(path));
    }
    auto output_directory = o.output_dir.empty() ? fs::path{} : resolve_path(o.output_dir);
    std::vector<Job> jobs, pending;
    std::vector<fs::path> destinations;
    const std::map<std::string, std::string> extensions = {
        {"text", ".txt"}, {"srt", ".srt"}, {"vtt", ".vtt"}, {"json", ".json"}};
    for (size_t i = 0; i < inputs.size(); ++i) {
        Job job{inputs[i], {}};
        for (const auto& [format, extension] : extensions) {
            if (o.format != "all" && format != o.format)
                continue;
            auto directory = o.output_dir.empty() ? inputs[i].parent_path() : output_directory;
            std::ostringstream stem;
            if (o.naming == "numbered")
                stem << o.prefix << '_' << std::setfill('0') << std::setw(3) << i + 1;
            else
                stem << inputs[i].stem().string();
            auto target = output_path(o.output.empty() ? directory / (stem.str() + extension)
                                                       : fs::path(o.output));
            Identity identity(target);
            if (sources.contains(identity))
                throw std::runtime_error("Output would overwrite input: " + target.string());
            if (outputs.contains(identity))
                throw std::runtime_error("Output collision: " + target.string());
            outputs.insert(identity);
            destinations.push_back(target);
            job.outputs[format] = target;
        }
        jobs.push_back(job);
        bool complete = !job.outputs.empty();
        for (const auto& [format, path] : job.outputs)
            complete &= fs::is_regular_file(path) && fs::file_size(path) > 0;
        if (o.skip_existing && !o.overwrite && complete) {
            log_message(LogLevel::info,
                        "Skipping complete result: " + job.source.filename().string());
            continue;
        }
        for (const auto& [format, path] : job.outputs) {
            if ((fs::exists(path) || fs::is_symlink(path)) &&
                ((!o.overwrite && !(o.resume && has_checkpoint(job))) ||
                 !fs::is_regular_file(path)))
                throw std::runtime_error("Output exists; use --overwrite: " + path.string());
        }
        pending.push_back(job);
    }
    JobPlan plan;
    for (const auto& job : pending)
        for (const auto& [format, path] : job.outputs)
            plan.directories.insert(path.parent_path());
    if (o.naming == "numbered" && !jobs.empty()) {
        auto path = output_path(output_directory / (o.prefix + "_files.json"));
        if (sources.contains(Identity(path)))
            throw std::runtime_error("Mapping would overwrite input");
        Json mapping = Json::array();
        for (size_t i = 0; i < jobs.size(); ++i) {
            Json mapped_outputs = Json::object();
            for (const auto& [format, target] : jobs[i].outputs)
                mapped_outputs[format] = target.filename().string();
            mapping.push_back({{"index", i + 1},
                               {"source", jobs[i].source.string()},
                               {"outputs", mapped_outputs}});
        }
        Mapping numbered{path, std::move(mapping)};
        if (fs::exists(path)) {
            if (!mapping_matches(numbered))
                throw std::runtime_error(
                    "Input list changed; use a new --prefix (mapping preserved)");
        } else {
            for (const auto& target : destinations)
                if (fs::exists(target))
                    throw std::runtime_error(
                        "Numbered results exist without their mapping; use a new --prefix");
            plan.mapping = std::move(numbered);
        }
    }
    plan.pending = std::move(pending);
    return plan;
}
} // namespace
void PublishedOutputs::check(const Job& job) const {
    for (const auto& [format, path] : job.outputs) {
        Identity identity(path);
        auto earlier = identity.exists ? files.find(identity.inode) : files.end();
        if (earlier != files.end())
            throw std::runtime_error("Output collision: " + path.string() +
                                     " is the same file as " + earlier->second.string() +
                                     " (the filesystem may ignore letter case); use --naming "
                                     "numbered");
    }
}
void PublishedOutputs::record(const Job& job) {
    for (const auto& [format, path] : job.outputs) {
        Identity identity(path);
        if (identity.exists)
            files.emplace(identity.inode, path);
    }
}
std::vector<Job> prepare_jobs(const Options& options) {
    auto plan = plan_jobs(options);
    for (const auto& directory : plan.directories)
        probe_directory(directory);
    if (plan.mapping)
        publish_mapping(*plan.mapping);
    return std::move(plan.pending);
}
} // namespace wt
