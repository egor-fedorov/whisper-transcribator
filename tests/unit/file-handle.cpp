#include "platform/file.hpp"
#include "support/test.hpp"
#include <cerrno>
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#endif
#include <type_traits>

using namespace wt;
using namespace wt::test;
using platform::File;
static_assert(!std::is_copy_constructible_v<File>);
static_assert(!std::is_copy_assignable_v<File>);
namespace {
bool valid(File::Native handle) {
#ifdef _WIN32
    DWORD flags = 0;
    return GetHandleInformation(reinterpret_cast<HANDLE>(handle), &flags) != 0;
#else
    return fcntl(handle, F_GETFD) >= 0;
#endif
}
void ownership(const fs::path& root) {
    File::Native original = -1;
    rejects(
        [&] {
            File first(platform::open_private(root / "file"));
            original = first.native();
            require(bool(first));
            File second(std::move(first));
            require(!first && second.native() == original);
            File third(platform::open_private(root / "file"));
            auto replaced = third.native();
            require(bool(third));
            third = std::move(second);
            require(!second && third.native() == original);
            require(!valid(replaced));
            throw std::runtime_error("unwind descriptor");
        },
        "unwind descriptor");
    require(!valid(original));
    File file(platform::open_private(root / "file"));
    require(bool(file));
    auto released = file.release();
    require(!file && file.close() == 0 && valid(released));
    File owner(released);
    require(owner.close() == 0 && !owner && owner.close() == 0);
}
} // namespace
int main() { return run_tests({{"handle move, release, close and exception cleanup", ownership}}); }
