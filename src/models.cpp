#include "app.hpp"
#include "catalog.hpp"
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <curl/curl.h>
#include <fcntl.h>
#include <iostream>
#include <memory>
#include <sys/file.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace wt {
std::vector<Model> model_catalog() {
    std::vector<Model> result;
    for (const auto& item : Json::parse(wt_model_catalog))
        result.push_back({item["name"], item["file"], item["url"], item["sha256"], item["bytes"]});
    return result;
}
fs::path model_root(const Options& options) {
    if (!options.download_root.empty())
        return resolve_path(options.download_root);
    if (!env("WHISPER_DOWNLOAD_ROOT").empty())
        return resolve_path(env("WHISPER_DOWNLOAD_ROOT"));
    auto cache = env("XDG_CACHE_HOME");
    if (cache.empty()) {
        if (env("HOME").empty())
            throw std::runtime_error("Set HOME or --download-root");
        cache = (fs::path(env("HOME")) / ".cache").string();
    }
    return resolve_path(fs::path(cache) / "whisper-transcribator" / "models");
}
void fetch_https(const std::string& url, const fs::path& target) {
    if (url.rfind("https://", 0) != 0)
        throw std::runtime_error("Model downloads require HTTPS");
    struct Global {
        Global() {
            if (curl_global_init(CURL_GLOBAL_DEFAULT))
                throw std::runtime_error("Cannot initialize curl");
        }
        ~Global() { curl_global_cleanup(); }
    };
    static Global global;
    std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    auto close_file = [](FILE* file) { fclose(file); };
    std::unique_ptr<FILE, decltype(close_file)> file(fopen(target.c_str(), "wb"), close_file);
    if (!curl || !file)
        throw std::runtime_error("Cannot initialize model download");
    curl_easy_setopt(curl.get(), CURLOPT_URL, url.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
    curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
    curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "whisper-transcribator");
    std::string ca = env("SSL_CERT_FILE");
    if (ca.empty()) {
        std::error_code error;
        auto executable = fs::read_symlink("/proc/self/exe", error);
        auto bundled = executable.parent_path().parent_path() / "share" / "cacert.pem";
        if (!error && fs::is_regular_file(bundled))
            ca = bundled.string();
    }
    if (!ca.empty())
        curl_easy_setopt(curl.get(), CURLOPT_CAINFO, ca.c_str());
    curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, file.get());
    curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(
        curl.get(), CURLOPT_XFERINFOFUNCTION,
        +[](void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) -> int {
            return stop_signal ? 1 : 0;
        });
    auto code = curl_easy_perform(curl.get());
    check_cancelled();
    if (code != CURLE_OK)
        throw std::runtime_error(std::string("Model download failed: ") + curl_easy_strerror(code));
    if (fflush(file.get()) || fsync(fileno(file.get())))
        throw std::runtime_error("Cannot flush downloaded model");
}
static std::string stable_hash(const fs::path& path) {
    struct stat before {
    }, after{};
    if (stat(path.c_str(), &before) || !S_ISREG(before.st_mode))
        throw std::runtime_error("Cannot inspect model: " + path.string());
    auto hash = sha256(path);
    if (stat(path.c_str(), &after) || before.st_dev != after.st_dev ||
        before.st_ino != after.st_ino || before.st_size != after.st_size ||
        before.st_mtim.tv_sec != after.st_mtim.tv_sec ||
        before.st_mtim.tv_nsec != after.st_mtim.tv_nsec ||
        before.st_ctim.tv_sec != after.st_ctim.tv_sec ||
        before.st_ctim.tv_nsec != after.st_ctim.tv_nsec)
        throw std::runtime_error("Model changed while hashing: " + path.string());
    return hash;
}
static bool valid_model(const Model& model, const fs::path& path) {
    return fs::is_regular_file(path) && fs::file_size(path) == model.bytes &&
           stable_hash(path) == model.hash;
}
PreparedModel ensure_cached(const Model& model, const fs::path& root, bool offline,
                            const Fetch& fetch) {
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
        int fd;
        explicit Lock(const fs::path& path)
            : fd(open(path.c_str(), O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC | O_NONBLOCK, 0600)) {
            if (fd < 0)
                throw std::runtime_error("Cannot open model lock");
            try {
                struct stat st {};
                if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_nlink != 1)
                    throw std::runtime_error("Unsafe model lock");
                bool announced = false;
                while (flock(fd, LOCK_EX | LOCK_NB)) {
                    if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR)
                        throw std::runtime_error("Cannot acquire model lock");
                    check_cancelled();
                    if (!announced) {
                        std::cerr << "Waiting for model preparation: " << path << '\n';
                        announced = true;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                check_cancelled();
            } catch (...) {
                close(fd);
                throw;
            }
        }
        ~Lock() { close(fd); }
    } lock(root / (model.file + ".lock"));
    if (fs::exists(target) || fs::is_symlink(target))
        return cached();
    auto temporary = (root / ".download-XXXXXX").string();
    int fd = mkstemp(temporary.data());
    if (fd < 0)
        throw std::runtime_error("Cannot create download file");
    close(fd);
    try {
        std::cerr << "Downloading " << model.name << " (" << model.bytes / 1024 / 1024 << " MiB)\n";
        fetch(model.url, temporary);
        check_cancelled();
        if (!valid_model(model, temporary))
            throw std::runtime_error("Model size/SHA-256 verification failed");
        if (link(temporary.c_str(), target.c_str()))
            throw std::runtime_error("Cannot publish downloaded model; target preserved");
        sync_directory(root);
    } catch (...) {
        unlink(temporary.c_str());
        throw;
    }
    unlink(temporary.c_str());
    return {target, model.hash};
}
PreparedModel prepare_model(const std::string& value, const Options& options) {
    std::string name = value == "large" ? "large-v3" : value == "turbo" ? "large-v3-turbo" : value;
    for (const auto& model : model_catalog())
        if (model.name == name) {
            std::cerr << "Preparing model: " << name << '\n';
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
Json list_models(const Options& options) {
    Json result = Json::array();
    auto root = model_root(options);
    for (const auto& model : model_catalog()) {
        auto path = root / model.file;
        std::string status = !fs::exists(path)          ? "missing"
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
