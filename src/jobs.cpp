#include "app.hpp"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <iostream>
#include <regex>
#include <set>
#include <sstream>

namespace wt {
std::vector<Job> prepare_jobs(const Options& o) {
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
    std::vector<fs::path> inputs;
    if (!o.input_dir.empty()) {
        auto directory = resolve_path(o.input_dir);
        if (!fs::is_directory(directory))
            throw std::runtime_error("Input directory not found: " + directory.string());
        const std::set<std::string> extensions = {
            ".aac", ".aiff", ".avi", ".flac", ".m4a", ".m4b",  ".m4v", ".mkv",  ".mov", ".mp3",
            ".mp4", ".mpeg", ".mpg", ".oga",  ".ogg", ".opus", ".wav", ".webm", ".wma", ".wmv"};
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
    for (const auto& path : inputs)
        if (!fs::is_regular_file(path))
            throw std::runtime_error("Input file not found: " + path.string());
    std::vector<Job> jobs, pending;
    std::vector<fs::path> destinations;
    const std::map<std::string, std::string> extensions = {
        {"text", ".txt"}, {"srt", ".srt"}, {"json", ".json"}};
    for (size_t i = 0; i < inputs.size(); ++i) {
        Job job{inputs[i], {}};
        for (const auto& [format, extension] : extensions) {
            if (o.format != "all" && format != o.format)
                continue;
            auto directory =
                o.output_dir.empty() ? inputs[i].parent_path() : resolve_path(o.output_dir);
            std::ostringstream stem;
            if (o.naming == "numbered")
                stem << o.prefix << '_' << std::setfill('0') << std::setw(3) << i + 1;
            else
                stem << inputs[i].stem().string();
            auto target =
                o.output.empty() ? directory / (stem.str() + extension) : resolve_path(o.output);
            for (const auto& source : inputs)
                if (same_file(target, source))
                    throw std::runtime_error("Output would overwrite input: " + target.string());
            for (const auto& other : destinations)
                if (same_file(target, other))
                    throw std::runtime_error("Output collision: " + target.string());
            destinations.push_back(target);
            job.outputs[format] = target;
        }
        jobs.push_back(job);
        bool complete = !job.outputs.empty();
        for (const auto& [format, path] : job.outputs)
            complete &= fs::is_regular_file(path) && fs::file_size(path) > 0;
        if (o.skip_existing && !o.overwrite && complete) {
            std::cerr << "Skipping complete result: " << job.source.filename() << '\n';
            continue;
        }
        for (const auto& [format, path] : job.outputs) {
            if ((fs::exists(path) || fs::is_symlink(path)) &&
                (!o.overwrite || !fs::is_regular_file(path)))
                throw std::runtime_error("Output exists; use --overwrite: " + path.string());
        }
        pending.push_back(job);
    }
    std::set<fs::path> directories;
    for (const auto& path : destinations)
        directories.insert(path.parent_path());
    for (const auto& directory : directories)
        probe_directory(directory);
    if (o.naming == "numbered" && !jobs.empty()) {
        auto path = resolve_path(o.output_dir) / (o.prefix + "_files.json");
        for (const auto& source : inputs)
            if (same_file(path, source))
                throw std::runtime_error("Mapping would overwrite input");
        Json mapping = Json::array();
        for (size_t i = 0; i < jobs.size(); ++i) {
            Json outputs = Json::object();
            for (const auto& [format, target] : jobs[i].outputs)
                outputs[format] = target.filename().string();
            mapping.push_back(
                {{"index", i + 1}, {"source", jobs[i].source.string()}, {"outputs", outputs}});
        }
        auto matches = [&] {
            return nlohmann::json::parse(read_text(path)) == nlohmann::json(mapping);
        };
        if (fs::exists(path)) {
            if (!matches())
                throw std::runtime_error(
                    "Input list changed; use a new --prefix (mapping preserved)");
        } else {
            for (const auto& target : destinations)
                if (fs::exists(target))
                    throw std::runtime_error(
                        "Numbered results exist without their mapping; use a new --prefix");
            try {
                atomic_write(path, mapping.dump(2) + "\n");
            } catch (const std::runtime_error&) {
                if (!fs::exists(path) || !matches())
                    throw;
            }
        }
    }
    return pending;
}
} // namespace wt
