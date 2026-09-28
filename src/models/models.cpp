#include "models/models.hpp"
#include "catalog.hpp"
#include "models/download.hpp"
#include "platform/file.hpp"
#include "platform/system.hpp"
#include "support/atomic.hpp"
#include "support/cancel.hpp"
#include "support/error.hpp"
#include "support/files.hpp"
#include "support/hash.hpp"
#include "support/report.hpp"
#include "support/strings.hpp"
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>

namespace wt {
std::vector<Model> model_catalog() {
    std::vector<Model> result;
    for (const auto& item : Json::parse(wt_model_catalog))
        result.push_back({item["name"], item["file"], item["url"], item["sha256"], item["bytes"]});
    return result;
}
fs::path model_root(const ModelCacheOptions& options) {
    if (!options.download_root.empty())
        return resolve_path(options.download_root);
    if (!env("WHISPER_DOWNLOAD_ROOT").empty())
        return resolve_path(env("WHISPER_DOWNLOAD_ROOT"));
    fs::path cache = env("XDG_CACHE_HOME");
    if (cache.empty())
        cache = platform::cache_directory();
    if (cache.empty())
        throw std::runtime_error("Set HOME or --download-root");
    return resolve_path(cache / "whisper-transcribator" / "models");
}
fs::path partial_model_path(const Model& model, const fs::path& root) {
    return root / ("." + model.file + "." + model.hash + ".part");
}
static std::string stable_hash(const fs::path& path) {
    auto before = platform::status(path);
    if (!before || before->type != platform::FileType::regular)
        throw std::runtime_error("Cannot inspect model: " + path.string());
    auto hash = sha256(path);
    auto after = platform::status(path);
    if (!after || before->id != after->id || before->size != after->size ||
        before->times != after->times)
        throw std::runtime_error("Model changed while hashing: " + path.string());
    return hash;
}
static bool valid_model(const Model& model, const fs::path& path) {
    return fs::is_regular_file(path) && fs::file_size(path) == model.bytes &&
           stable_hash(path) == model.hash;
}
PreparedModel ensure_cached(const Model& model, const fs::path& root, bool offline,
                            const Fetch& fetch, const PermissionProbe& permissions) {
    auto target = root / model.file;
    auto cached = [&]() -> PreparedModel {
        if (!valid_model(model, target))
            throw std::runtime_error(
                "Corrupt model cache; remove or replace this file explicitly: " + target.string());
        return {target, model.hash};
    };
    check_cancelled();
    if (fs::exists(target) || fs::is_symlink(target))
        return cached();
    if (offline)
        throw std::runtime_error("Model missing in offline mode: " + target.string());
    fs::create_directories(root);
    struct Lock {
        platform::File file;
        explicit Lock(const fs::path& path) : file(platform::open_private(path)) {
            if (!file)
                throw std::runtime_error("Cannot open model lock");
            auto st = platform::status(file);
            if (!st || st->type != platform::FileType::regular || st->links != 1)
                throw std::runtime_error("Unsafe model lock");
            bool announced = false;
            for (auto result = platform::try_lock(file); result != platform::Lock::acquired;
                 result = platform::try_lock(file)) {
                if (result == platform::Lock::failed)
                    throw std::runtime_error("Cannot acquire model lock");
                check_cancelled();
                if (!announced) {
                    log_message(LogLevel::info, "Waiting for model preparation: " + path.string());
                    announced = true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            check_cancelled();
        }
    } lock(root / (model.file + ".lock"));
    if (fs::exists(target) || fs::is_symlink(target))
        return cached();
    auto temporary = partial_model_path(model, root);
    auto partial = open_partial_model(temporary, permissions);
    partial.close();
    sync_directory(root);
    std::error_code ignored;
    if (fs::file_size(temporary) > model.bytes) {
        fs::remove(temporary, ignored);
        sync_directory(root);
        throw std::runtime_error("Removed oversized partial model; retry the download");
    }
    if (fs::file_size(temporary) != model.bytes) {
        log_message(LogLevel::info, "Downloading " + model.name + " (" +
                                        std::to_string(model.bytes / 1024 / 1024) + " MiB)");
        fetch(model, temporary);
    }
    check_cancelled();
    if (!valid_model(model, temporary)) {
        fs::remove(temporary, ignored);
        sync_directory(root);
        throw std::runtime_error(
            "Model size/SHA-256 verification failed; removed corrupt partial download");
    }
    if (!platform::rename_noreplace(temporary, target))
        throw std::runtime_error("Cannot publish downloaded model; target preserved: " +
                                 std::string(std::strerror(errno)));
    sync_directory(root);
    return {target, model.hash};
}
PreparedModel ensure_cached(const Model& model, const fs::path& root, bool offline) {
    return ensure_cached(model, root, offline, fetch_https);
}
PreparedModel prepare_model(const std::string& value, const ModelCacheOptions& options) {
    std::string name = value == "large" ? "large-v3" : value == "turbo" ? "large-v3-turbo" : value;
    for (const auto& model : model_catalog())
        if (model.name == name) {
            log_message(LogLevel::info, "Preparing model: " + name);
            return ensure_cached(model, model_root(options), options.local_files_only);
        }
    if (fs::exists(resolve_path(value)) || value.find('/') != std::string::npos ||
        value.rfind("~", 0) == 0) {
        auto path = resolve_path(value);
        if (!fs::is_regular_file(path) || fs::file_size(path) == 0)
            throw UsageError("Expected a nonempty local GGML file, not a CTranslate2 directory: " +
                             path.string());
        return {path, stable_hash(path)};
    }
    throw UsageError("Unknown model: " + value + "; use models list or a local GGML file");
}
Json list_models(const ModelCacheOptions& options) {
    Json result = Json::array();
    auto root = model_root(options);
    for (const auto& model : model_catalog()) {
        auto path = root / model.file;
        std::string status =
            !fs::exists(path)
                ? (fs::exists(partial_model_path(model, root)) ? "partial" : "missing")
            : valid_model(model, path) ? "cached"
                                       : "corrupt";
        result.push_back({{"name", model.name},
                          {"status", status},
                          {"path", path.string()},
                          {"bytes", model.bytes}});
    }
    return result;
}
} // namespace wt
