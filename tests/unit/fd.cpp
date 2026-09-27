#include "support/fd.hpp"
#include "support/test.hpp"
#include <cerrno>
#include <fcntl.h>
#include <type_traits>

using namespace wt;
using namespace wt::test;
static_assert(!std::is_copy_constructible_v<UniqueFd>);
static_assert(!std::is_copy_assignable_v<UniqueFd>);
namespace {
void ownership(const fs::path&) {
    int original = -1;
    rejects(
        [&] {
            UniqueFd first(open("/dev/null", O_RDONLY | O_CLOEXEC));
            original = first.get();
            require(original >= 0);
            UniqueFd second(std::move(first));
            require(first.get() == -1 && second.get() == original);
            UniqueFd third(open("/dev/null", O_RDONLY | O_CLOEXEC));
            auto replaced = third.get();
            require(replaced >= 0);
            third = std::move(second);
            require(second.get() == -1 && third.get() == original);
            require(fcntl(replaced, F_GETFD) == -1 && errno == EBADF);
            throw std::runtime_error("unwind descriptor");
        },
        "unwind descriptor");
    require(fcntl(original, F_GETFD) == -1 && errno == EBADF);
    UniqueFd fd(open("/dev/null", O_RDONLY | O_CLOEXEC));
    require(fd.get() >= 0);
    auto released = fd.release();
    require(fd.get() == -1 && fd.close() == 0 && fcntl(released, F_GETFD) >= 0);
    UniqueFd owner(released);
    require(owner.close() == 0 && owner.get() == -1 && owner.close() == 0);
}
} // namespace
int main() {
    return run_tests({{"descriptor move, release, close and exception cleanup", ownership}});
}
