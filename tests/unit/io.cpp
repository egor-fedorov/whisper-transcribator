#include "support/io.hpp"
#include "support/cancel.hpp"
#include "support/test.hpp"

using namespace wt;
using namespace wt::test;
namespace {
void atomic_no_clobber_and_replacement(const fs::path& root) {

    auto path = root / "result";
    atomic_write(path, "original");
    rejects([&] { atomic_write(path, "new"); }, "Cannot publish");
    require(read_text(path) == "original");
    atomic_write(path, "new", true);
    require(read_text(path) == "new");
    require(std::distance(fs::directory_iterator(root), fs::directory_iterator{}) == 1);
}
void cancellation_preserves_output(const fs::path& root) {

    auto path = root / "result";
    atomic_write(path, "old");
    stop_signal = SIGTERM;
    bool stopped = false;
    try {
        atomic_write(path, "new", true);
    } catch (const Cancelled&) {
        stopped = true;
    }
    stop_signal = 0;
    require(stopped && read_text(path) == "old");
    require(std::distance(fs::directory_iterator(root), fs::directory_iterator{}) == 1);
}
} // namespace
int main() {
    return run_tests({
        {"atomic no clobber and replacement", atomic_no_clobber_and_replacement},
        {"cancellation preserves output", cancellation_preserves_output},
    });
}
