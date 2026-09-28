#include "support/fixtures/jobs.hpp"
#include "support/atomic.hpp"

namespace wt::test {
CliOptions input(const fs::path& root, const std::string& name) {
    atomic_write(root / name, "media");
    CliOptions o;
    o.jobs.inputs = {(root / name).string()};
    return o;
}
} // namespace wt::test
