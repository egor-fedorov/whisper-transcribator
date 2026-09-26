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
#include <streambuf>
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
namespace {
class FileBuffer : public std::streambuf {
    int fd;
    char buffer[65536];
    int sync() override {
        size_t offset = 0, size = static_cast<size_t>(pptr() - pbase());
        while (offset < size) {
            check_cancelled();
            auto count = ::write(fd, buffer + offset, size - offset);
            if (count < 0 && errno == EINTR)
                continue;
            if (count <= 0)
                throw std::runtime_error("Output write failed: " +
                                         std::string(std::strerror(errno)));
            offset += static_cast<size_t>(count);
        }
        setp(buffer, buffer + sizeof(buffer));
        return 0;
    }
    int_type overflow(int_type value) override {
        sync();
        if (!traits_type::eq_int_type(value, traits_type::eof())) {
            *pptr() = traits_type::to_char_type(value);
            pbump(1);
        }
        return traits_type::not_eof(value);
    }

  public:
    explicit FileBuffer(int descriptor) : fd(descriptor) { setp(buffer, buffer + sizeof(buffer)); }
};
mode_t output_mode(const fs::path& path, bool overwrite) {
    struct stat status {};
    mode_t mask = umask(0);
    umask(mask);
    return overwrite && stat(path.c_str(), &status) == 0 ? status.st_mode & 0777 : 0666 & ~mask;
}
} // namespace
void sync_directory(const fs::path& path) {
    int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        throw std::runtime_error("Cannot open directory for sync: " + path.string());
    int status = fsync(fd);
    close(fd);
    if (status)
        throw std::runtime_error("Cannot sync directory: " + path.string());
}
void publish_file(const fs::path& temporary, const fs::path& target, bool overwrite) {
    check_cancelled();
    int fd = open(temporary.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    if (fd < 0)
        throw std::runtime_error("Cannot open staged output");
    int status = fchmod(fd, output_mode(target, overwrite));
    if (!status)
        status = fsync(fd);
    close(fd);
    if (status)
        throw std::runtime_error("Cannot flush staged output");
    status = overwrite ? rename(temporary.c_str(), target.c_str())
                       : link(temporary.c_str(), target.c_str());
    if (status)
        throw std::runtime_error("Cannot publish " + target.string() + ": " + std::strerror(errno));
    sync_directory(target.parent_path());
    if (!overwrite && unlink(temporary.c_str()))
        throw std::runtime_error("Cannot remove staged output");
    sync_directory(temporary.parent_path());
}
void atomic_write_stream(const fs::path& path, const std::function<void(std::ostream&)>& write,
                         bool overwrite, bool private_file) {
    check_cancelled();
    auto pattern = (path.parent_path() / ".whisper-output-XXXXXX").string();
    int fd = mkstemp(pattern.data());
    if (fd < 0)
        throw std::runtime_error("Cannot create temporary output: " + path.string());
    try {
        FileBuffer buffer(fd);
        std::ostream stream(&buffer);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        write(stream);
        stream.flush();
        if (fchmod(fd, private_file ? 0600 : output_mode(path, overwrite)) || fsync(fd))
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
    sync_directory(path.parent_path());
}
void atomic_write(const fs::path& path, const std::string& content, bool overwrite) {
    atomic_write_stream(path, [&](auto& stream) { stream << content; }, overwrite);
}
std::string sha256_text(const std::string& text) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int size = 0;
    if (EVP_Digest(text.data(), text.size(), digest, &size, EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("SHA-256 failed");
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < size; ++i)
        output << std::setw(2) << static_cast<int>(digest[i]);
    return output.str();
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
