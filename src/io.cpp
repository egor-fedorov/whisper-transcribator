#include "app.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <memory>
#include <openssl/evp.h>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace wt {
volatile std::sig_atomic_t stop_signal = 0;
void check_cancelled() {
    if (stop_signal)
        throw Cancelled{};
}
std::string env(const char* key) {
    const char* value = std::getenv(key);
    return value ? value : "";
}
fs::path resolve_path(const fs::path& path) {
    auto text = path.string();
    if (text == "~" || text.rfind("~/", 0) == 0) {
        if (env("HOME").empty())
            throw UsageError("HOME is not set");
        text = env("HOME") + text.substr(1);
    }
    return fs::weakly_canonical(fs::absolute(text));
}
bool same_file(const fs::path& a, const fs::path& b) {
    return resolve_path(a) == resolve_path(b) ||
           (fs::exists(a) && fs::exists(b) && fs::equivalent(a, b));
}
std::string read_text(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        throw std::runtime_error("Cannot read: " + path.string());
    std::ostringstream data;
    data << stream.rdbuf();
    if (stream.bad())
        throw std::runtime_error("Read failed: " + path.string());
    return data.str();
}
void probe_directory(const fs::path& path) {
    fs::create_directories(path);
    auto pattern = (path / ".whisper-probe-XXXXXX").string();
    int fd = mkstemp(pattern.data());
    if (fd < 0)
        throw std::runtime_error("Cannot write directory: " + path.string());
    close(fd);
    unlink(pattern.c_str());
}
void atomic_write(const fs::path& path, const std::string& content, bool overwrite) {
    check_cancelled();
    auto pattern = (path.parent_path() / ".whisper-output-XXXXXX").string();
    int fd = mkstemp(pattern.data());
    if (fd < 0)
        throw std::runtime_error("Cannot create temporary output: " + path.string());
    try {
        struct stat status {};
        mode_t mask = umask(0);
        umask(mask);
        mode_t mode = 0666 & ~mask;
        if (overwrite && stat(path.c_str(), &status) == 0)
            mode = status.st_mode & 0777;
        size_t offset = 0;
        while (offset < content.size()) {
            check_cancelled();
            auto count = write(fd, content.data() + offset, content.size() - offset);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                throw std::runtime_error("Cannot write: " + path.string());
            offset += static_cast<size_t>(count);
        }
        if (fchmod(fd, mode) || fsync(fd))
            throw std::runtime_error("Cannot flush output");
        if (close(fd)) {
            fd = -1;
            throw std::runtime_error("Cannot close output");
        }
        fd = -1;
        check_cancelled();
        int result =
            overwrite ? rename(pattern.c_str(), path.c_str()) : link(pattern.c_str(), path.c_str());
        if (result)
            throw std::runtime_error("Cannot publish " + path.string() + ": " +
                                     std::strerror(errno));
    } catch (...) {
        if (fd >= 0)
            close(fd);
        unlink(pattern.c_str());
        throw;
    }
    unlink(pattern.c_str());
}
std::string sha256(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot hash: " + path.string());
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("SHA-256 initialization failed");
    char buffer[65536];
    while (input) {
        check_cancelled();
        input.read(buffer, sizeof(buffer));
        if (EVP_DigestUpdate(ctx.get(), buffer, static_cast<size_t>(input.gcount())) != 1)
            throw std::runtime_error("SHA-256 update failed");
    }
    if (!input.eof())
        throw std::runtime_error("Hash read failed");
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int size = 0;
    if (EVP_DigestFinal_ex(ctx.get(), digest, &size) != 1)
        throw std::runtime_error("SHA-256 failed");
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < size; ++i)
        output << std::setw(2) << static_cast<int>(digest[i]);
    return output.str();
}
std::string trim(const std::string& value) {
    auto first = value.find_first_not_of(" \t\r\n");
    return first == std::string::npos
               ? ""
               : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
} // namespace wt
