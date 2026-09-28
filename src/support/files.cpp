#include "support/files.hpp"
#include "platform/file.hpp"
#include "platform/system.hpp"
#include "support/error.hpp"
#include <fstream>
#include <sstream>

namespace wt {
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
} // namespace wt
