#include "app.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <curl/curl.h>
#include <fcntl.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace wt {
int open_partial_model(const fs::path& path) {
    int fd = open(path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    struct stat st {};
    if (fd < 0 || fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_uid != geteuid() ||
        st.st_nlink != 1 || (st.st_mode & 0077)) {
        if (fd >= 0)
            close(fd);
        throw std::runtime_error("Unsafe partial model file: " + path.string());
    }
    return fd;
}
namespace {
struct Transfer {
    int fd;
    const Model& model;
    uint64_t offset = 0, written = 0;
    long status = 0;
    bool range_valid = false;
    std::string error;
    ~Transfer() { close(fd); }
};
size_t headers(char* bytes, size_t size, size_t count, void* opaque) noexcept {
    auto& t = *static_cast<Transfer*>(opaque);
    try {
        auto length = size * count;
        if (length > 8192)
            return 0;
        std::string line(bytes, length);
        if (line.rfind("HTTP/", 0) == 0) {
            std::istringstream input(line);
            std::string version;
            input >> version >> t.status;
            t.range_valid = false;
        } else {
            std::transform(line.begin(), line.end(), line.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            if (line.rfind("content-range:", 0) == 0) {
                std::istringstream input(line.substr(14));
                std::string unit;
                uint64_t from = 0, to = 0, total = 0;
                char dash = 0, slash = 0, extra = 0;
                bool parsed = bool(input >> unit >> from >> dash >> to >> slash >> total);
                t.range_valid = parsed && !(input >> extra) && unit == "bytes" && dash == '-' &&
                                slash == '/' && from == t.offset && from <= to &&
                                to < t.model.bytes && to + 1 == t.model.bytes &&
                                total == t.model.bytes;
            }
        }
        return length;
    } catch (...) {
        return 0;
    }
}
size_t body(char* bytes, size_t size, size_t count, void* opaque) noexcept {
    auto& t = *static_cast<Transfer*>(opaque);
    try {
        auto length = size * count;
        if (t.status >= 300 && t.status < 400)
            return length;
        if (!((t.status == 200 && !t.offset) || (t.status == 206 && t.range_valid))) {
            t.error = "Invalid HTTP status or byte range";
            return 0;
        }
        if (length > t.model.bytes - t.offset - t.written) {
            t.error = "Model response exceeds pinned size";
            return 0;
        }
        size_t done = 0;
        while (done < length) {
            auto n = write(t.fd, bytes + done, length - done);
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0) {
                t.error = "Cannot write partial model";
                return 0;
            }
            done += static_cast<size_t>(n);
            t.written += static_cast<uint64_t>(n);
        }
        return length;
    } catch (...) {
        return 0;
    }
}
} // namespace
void fetch_https(const Model& model, const fs::path& target) {
    if (!model.bytes)
        throw std::runtime_error("Model size must be positive");
    if (model.url.rfind("https://", 0) != 0)
        throw std::runtime_error("Model downloads require HTTPS");
    struct Global {
        Global() {
            if (curl_global_init(CURL_GLOBAL_DEFAULT))
                throw std::runtime_error("Cannot initialize curl");
        }
        ~Global() { curl_global_cleanup(); }
    };
    static Global global;
    Transfer transfer{open_partial_model(target), model, 0, 0, 0, false, {}};
    auto end = lseek(transfer.fd, 0, SEEK_END);
    if (end < 0 || static_cast<uint64_t>(end) > model.bytes)
        throw std::runtime_error(
            "Invalid partial model size; remove this partial file explicitly: " + target.string());
    transfer.offset = static_cast<uint64_t>(end);
    std::string ca = env("SSL_CERT_FILE");
    if (ca.empty()) {
        std::error_code error;
        auto executable = fs::read_symlink("/proc/self/exe", error);
        auto bundled = executable.parent_path().parent_path() / "share" / "cacert.pem";
        if (!error && fs::is_regular_file(bundled))
            ca = bundled.string();
    }
    for (int attempt = 0; attempt < 2; ++attempt) {
        check_cancelled();
        std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(),
                                                                 curl_easy_cleanup);
        if (!curl)
            throw std::runtime_error("Cannot initialize model download");
        curl_easy_setopt(curl.get(), CURLOPT_URL, model.url.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
        curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS_STR, "https");
        curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS_STR, "https");
#else
        curl_easy_setopt(curl.get(), CURLOPT_PROTOCOLS, CURLPROTO_HTTPS);
        curl_easy_setopt(curl.get(), CURLOPT_REDIR_PROTOCOLS, CURLPROTO_HTTPS);
#endif
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_SSL_VERIFYHOST, 2L);
        if (!ca.empty())
            curl_easy_setopt(curl.get(), CURLOPT_CAINFO, ca.c_str());
        curl_easy_setopt(curl.get(), CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_MAXREDIRS, 8L);
        curl_easy_setopt(curl.get(), CURLOPT_FAILONERROR, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_CONNECTTIMEOUT, 30L);
        curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_LIMIT, 1024L);
        curl_easy_setopt(curl.get(), CURLOPT_LOW_SPEED_TIME, 60L);
        curl_easy_setopt(curl.get(), CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl.get(), CURLOPT_USERAGENT, "whisper-transcribator");
        curl_easy_setopt(curl.get(), CURLOPT_RESUME_FROM_LARGE,
                         static_cast<curl_off_t>(transfer.offset));
        curl_easy_setopt(curl.get(), CURLOPT_HEADERFUNCTION, headers);
        curl_easy_setopt(curl.get(), CURLOPT_HEADERDATA, &transfer);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEFUNCTION, body);
        curl_easy_setopt(curl.get(), CURLOPT_WRITEDATA, &transfer);
        curl_easy_setopt(curl.get(), CURLOPT_NOPROGRESS, 0L);
        curl_easy_setopt(curl.get(), CURLOPT_XFERINFODATA, &transfer);
        curl_easy_setopt(
            curl.get(), CURLOPT_XFERINFOFUNCTION,
            +[](void* opaque, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept -> int {
                if (stop_signal)
                    return 1;
                try {
                    const auto& t = *static_cast<Transfer*>(opaque);
                    auto done = t.offset + t.written;
                    report_progress("Downloading",
                                    t.model.name + " | " + std::to_string(done / 1024 / 1024) +
                                        " / " + std::to_string(t.model.bytes / 1024 / 1024) +
                                        " MiB (" + std::to_string(done * 100 / t.model.bytes) +
                                        "%)");
                    return 0;
                } catch (...) {
                    return 1;
                }
            });
        auto code = curl_easy_perform(curl.get());
        if (fsync(transfer.fd))
            throw std::runtime_error("Cannot sync partial model");
        check_cancelled();
        long status = 0;
        curl_easy_getinfo(curl.get(), CURLINFO_RESPONSE_CODE, &status);
        if (attempt == 0 && transfer.offset && (status == 200 || status == 416)) {
            log_message(LogLevel::info, "Server cannot resume this range; restarting download");
            if (ftruncate(transfer.fd, 0) || lseek(transfer.fd, 0, SEEK_SET) < 0)
                throw std::runtime_error("Cannot restart partial model");
            transfer.offset = transfer.written = 0;
            transfer.status = 0;
            transfer.range_valid = false;
            transfer.error.clear();
            continue;
        }
        if (code != CURLE_OK || !transfer.error.empty() ||
            !((status == 200 && !transfer.offset) || (status == 206 && transfer.range_valid)))
            throw std::runtime_error(
                "Model download failed; partial data retained: " +
                (transfer.error.empty() ? std::string(curl_easy_strerror(code)) : transfer.error));
        return;
    }
}
} // namespace wt
