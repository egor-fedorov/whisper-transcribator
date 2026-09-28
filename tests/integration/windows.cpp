#include "platform/file.hpp"
#include "platform/system.hpp"
#include "support/fixtures.hpp"
#include "support/hash.hpp"
#include "support/io.hpp"
#include "support/test.hpp"
#include "transcript/journal.hpp"
#include <algorithm>
#include <cstddef>
#include <vector>
#include <windows.h>
#include <winioctl.h>

using namespace wt;
using namespace wt::test;
namespace {
void private_files(const fs::path& root) {
    auto directory = root / "private";
    require(platform::create_private_directory(directory));
    auto status = platform::status(directory);
    require(status && status->owned && status->permissions == 0700);
    auto file = platform::open_private(directory / "state");
    status = platform::status(file);
    require(status && status->owned && status->permissions == 0600);
    require(platform::write(file, "abc", 3) == 3 && platform::sync(file));
    file.close();
    require(sha256(directory / "state") == sha256_text("abc"));
    wt::test::permissions(directory, fs::perms::all);
    atomic_write(directory / "output", "one");
    status = platform::status(directory / "output");
    require(status && status->permissions == 0666);
    atomic_write(directory / "output", "two", true);
    require(platform::status(directory / "output")->permissions == 0666);
}
void unicode_and_long_paths(const fs::path& root) {
    auto path = root / fs::u8path(u8"\u041b\u0435\u043a\u0446\u0438\u044f \u65e5\u672c\u8a9e");
    for (int i = 0; i < 8; ++i)
        path /= "directory-with-a-long-name-0123456789";
    fs::create_directories(path);
    atomic_write(path / "data", "abc");
    require(sha256(path / "data") == sha256_text("abc"));
    Fixture fixture(path);
    Journal journal(fixture.job, fixture.options, fixture.fingerprint());
    Audio audio;
    run(fixture, journal, audio);
    require(fs::file_size(fixture.job.outputs.at("text")) > 0);
}
void busy_output(const fs::path& root) {
    auto path = root / "output";
    atomic_write(path, "original");
    platform::File held(reinterpret_cast<platform::File::Native>(CreateFileW(
        path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr)));
    require(bool(held));
    rejects([&] { atomic_write(path, "replacement", true); }, "Cannot publish");
    require(read_text(path) == "original");
    held.close();
    atomic_write(path, "replacement", true);
    require(read_text(path) == "replacement");
}
void junction_checkpoint(const fs::path& root) {
    Fixture fixture(root / "job");
    auto outside = root / "outside";
    require(platform::create_private_directory(outside));
    auto link = root / "job/.whisper-transcribator";
    fs::create_directory(link);
    auto target = L"\\??\\" + fs::absolute(outside).wstring();
    struct Junction {
        DWORD tag;
        WORD length, reserved, substitute_offset, substitute_length, print_offset, print_length;
        wchar_t path[4096];
    } data{};
    require(target.size() + 1 < 4096);
    data.tag = IO_REPARSE_TAG_MOUNT_POINT;
    data.substitute_length = static_cast<WORD>(target.size() * sizeof(wchar_t));
    data.print_offset = static_cast<WORD>(data.substitute_length + sizeof(wchar_t));
    data.length = static_cast<WORD>(8 + data.print_offset + sizeof(wchar_t));
    std::copy(target.begin(), target.end(), data.path);
    platform::File handle(reinterpret_cast<platform::File::Native>(
        CreateFileW(link.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr)));
    require(bool(handle));
    DWORD returned = 0;
    require(DeviceIoControl(reinterpret_cast<HANDLE>(handle.native()), FSCTL_SET_REPARSE_POINT,
                            &data, data.length + 8, nullptr, 0, &returned, nullptr) != 0);
    handle.close();
    rejects([&] { Journal journal(fixture.job, fixture.options, fixture.fingerprint()); },
            "Checkpoint directory must be owned by you");
    require(fs::is_empty(outside));
    fs::remove(link);
}
} // namespace
int main() {
    platform::prepare_console();
    return run_tests({{"private and inherited ACLs", private_files},
                      {"Unicode and long paths", unicode_and_long_paths},
                      {"busy output preserves old contents", busy_output},
                      {"checkpoint rejects junction", junction_checkpoint}});
}
