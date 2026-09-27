#include "platform/file.hpp"
#include "support/test.hpp"
#include <cerrno>
#include <fcntl.h>
#include <type_traits>

using namespace wt;
using namespace wt::test;
using platform::File;
static_assert(!std::is_copy_constructible_v<File>);
static_assert(!std::is_copy_assignable_v<File>);
namespace {
// POSIX descriptors show whether a handle was closed.
void ownership(const fs::path&) {
    int original = -1;
    rejects(
        [&] {
            File first(open("/dev/null", O_RDONLY | O_CLOEXEC));
            original = first.native();
            require(bool(first));
            File second(std::move(first));
            require(!first && second.native() == original);
            File third(open("/dev/null", O_RDONLY | O_CLOEXEC));
            auto replaced = third.native();
            require(bool(third));
            third = std::move(second);
            require(!second && third.native() == original);
            require(fcntl(replaced, F_GETFD) == -1 && errno == EBADF);
            throw std::runtime_error("unwind descriptor");
        },
        "unwind descriptor");
    require(fcntl(original, F_GETFD) == -1 && errno == EBADF);
    File file(open("/dev/null", O_RDONLY | O_CLOEXEC));
    require(bool(file));
    auto released = file.release();
    require(!file && file.close() == 0 && fcntl(released, F_GETFD) >= 0);
    File owner(released);
    require(owner.close() == 0 && !owner && owner.close() == 0);
}
} // namespace
int main() { return run_tests({{"handle move, release, close and exception cleanup", ownership}}); }
