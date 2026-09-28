#include "transcript/checkpoint/detail/storage.hpp"
#include "platform/file.hpp"
#include "support/cancel.hpp"
#include "support/io.hpp"
#include "transcript/checkpoint/detail/privacy.hpp"
#include <ostream>
#include <stdexcept>

namespace wt::checkpoint_detail {
Json read_record(const fs::path& path, Privacy& privacy) {
    auto file = platform::open_for_reading(path);
    if (!file)
        throw std::runtime_error("Cannot read checkpoint: " + path.string());
    auto st = platform::status(file);
    if (!st || st->type != platform::FileType::regular || !privacy.accepts(path, *st, true) ||
        st->size > 16 * 1024 * 1024)
        throw std::runtime_error("Invalid checkpoint file: " + path.string());
    std::string bytes(static_cast<size_t>(st->size), '\0');
    size_t offset = 0;
    while (offset < bytes.size()) {
        check_cancelled();
        auto n = platform::read(file, bytes.data() + offset, bytes.size() - offset);
        if (n <= 0)
            throw std::runtime_error("Checkpoint read failed");
        offset += static_cast<size_t>(n);
    }
    return Json::parse(bytes);
}
void write_record(const fs::path& path, const Json& record) {
    auto bytes = record.dump();
    if (bytes.size() > 16 * 1024 * 1024)
        throw std::runtime_error("Checkpoint record exceeds size limit");
    atomic_write_stream(path, [&](auto& out) { out << bytes; }, true, true);
}
bool exists_entry(const fs::path& path) { return fs::exists(path) || fs::is_symlink(path); }
void discard_checkpoint(const fs::path& directory) {
    fs::remove_all(directory);
    sync_directory(directory.parent_path());
}
} // namespace wt::checkpoint_detail
