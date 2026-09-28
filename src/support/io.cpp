#include "support/io.hpp"
#include "platform/file.hpp"
#include "platform/system.hpp"
#include "support/cancel.hpp"
#include "support/error.hpp"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <streambuf>

namespace wt {
std::string env(const char* key) {
    const char* value = std::getenv(key);
    return value ? value : "";
}
fs::path resolve_path(const fs::path& path) {
    auto text = path.string();
    if (text == "~" || text.rfind("~/", 0) == 0) {
        auto home = platform::home_directory();
        if (home.empty())
            throw UsageError("HOME is not set");
        text = home.string() + text.substr(1);
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
    fs::path probe;
    auto file = platform::create_temporary(path, ".whisper-probe-", probe);
    if (!file)
        throw std::runtime_error("Cannot write directory: " + path.string());
    file.close();
    std::error_code ignored;
    fs::remove(probe, ignored);
}
namespace {
class FileBuffer : public std::streambuf {
    platform::File& file;
    char buffer[65536];
    int sync() override {
        size_t offset = 0, size = static_cast<size_t>(pptr() - pbase());
        while (offset < size) {
            check_cancelled();
            auto count = platform::write(file, buffer + offset, size - offset);
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
    explicit FileBuffer(platform::File& file) : file(file) {
        setp(buffer, buffer + sizeof(buffer));
    }
};
unsigned output_permissions(const fs::path& path, bool overwrite) {
    auto existing = overwrite ? platform::status(path) : std::nullopt;
    return existing ? existing->permissions : platform::default_permissions();
}
// Sets the permissions of a file this program created. A filesystem reporting another owner for
// it cannot store owners or modes and may refuse (exFAT mounted for another user); its mount
// options then decide the permissions.
bool apply_permissions(platform::File& file, unsigned permissions) {
    if (platform::set_permissions(file, permissions))
        return true;
    auto status = platform::status(file);
    return status && !status->owned;
}
StoredPermissions probe_with_file(const fs::path& directory) {
    fs::path probe;
    auto file = platform::create_temporary(directory, ".whisper-probe-", probe);
    // Keep every check when the filesystem cannot be probed.
    if (!file)
        return {};
    bool changed = platform::set_permissions(file, 0600);
    auto status = platform::status(file);
    file.close();
    std::error_code ignored;
    fs::remove(probe, ignored);
    if (!status)
        return {};
    return {status->owned && !platform::ownership_ignored(directory),
            changed && status->permissions == 0600};
}
// Replaces `to` or, without `overwrite`, fails if it exists.
void rename_output(const fs::path& from, const fs::path& to, bool overwrite) {
    std::error_code error;
    if (!(overwrite ? platform::rename_replace(from, to) : platform::rename_noreplace(from, to)))
        error.assign(errno, std::generic_category());
    if (error)
        throw std::runtime_error("Cannot publish " + to.string() + ": " + error.message());
}
} // namespace
StoredPermissions (*probe_permissions)(const fs::path&) = probe_with_file;
bool StoredPermissions::accepts(const platform::FileStatus& status, bool owner_only) const {
    return (status.owned || !owner) && (owner_only || !mode);
}
void sync_directory(const fs::path& path) {
    auto directory = platform::open_directory(path);
    if (!directory)
        throw std::runtime_error("Cannot open directory for sync: " + path.string());
    bool synced = platform::sync(directory);
    directory.close();
    if (!synced)
        throw std::runtime_error("Cannot sync directory: " + path.string());
}
void publish_file(const fs::path& temporary, const fs::path& target, bool overwrite) {
    check_cancelled();
    auto file = platform::open_for_reading(temporary);
    if (!file)
        throw std::runtime_error("Cannot open staged output");
    bool flushed =
        apply_permissions(file, output_permissions(target, overwrite)) && platform::sync(file);
    file.close();
    if (!flushed)
        throw std::runtime_error("Cannot flush staged output");
    rename_output(temporary, target, overwrite);
    sync_directory(target.parent_path());
    sync_directory(temporary.parent_path());
}
void atomic_write_stream(const fs::path& path, const std::function<void(std::ostream&)>& write,
                         bool overwrite, bool private_file) {
    check_cancelled();
    fs::path temporary;
    auto file = platform::create_temporary(path.parent_path(), ".whisper-output-", temporary);
    if (!file)
        throw std::runtime_error("Cannot create temporary output: " + path.string());
    try {
        FileBuffer buffer(file);
        std::ostream stream(&buffer);
        stream.exceptions(std::ios::badbit | std::ios::failbit);
        write(stream);
        stream.flush();
        if (!apply_permissions(file, private_file ? 0600 : output_permissions(path, overwrite)) ||
            !platform::sync(file))
            throw std::runtime_error("Cannot flush output");
        if (file.close())
            throw std::runtime_error("Cannot close output");
        check_cancelled();
        rename_output(temporary, path, overwrite);
    } catch (...) {
        file.close();
        std::error_code ignored;
        fs::remove(temporary, ignored);
        throw;
    }
    sync_directory(path.parent_path());
}
void atomic_write(const fs::path& path, const std::string& content, bool overwrite) {
    atomic_write_stream(path, [&](auto& stream) { stream << content; }, overwrite);
}
std::string trim(const std::string& value) {
    auto first = value.find_first_not_of(" \t\r\n");
    return first == std::string::npos
               ? ""
               : value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}
} // namespace wt
