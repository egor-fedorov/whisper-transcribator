#include "support/fixtures/directory.hpp"
#include "support/scoped.hpp"
#include "support/test.hpp"
#include <cstdlib>
#include <sstream>

using namespace wt::test;
namespace {
void assertions(const wt::fs::path&) {
    rejects([] { throw std::runtime_error("expected reason"); }, "expected reason");
    rejects<AssertionFailure>([] { rejects([] {}, "missing"); }, "expected failure");
    rejects<AssertionFailure>(
        [] { rejects([] { throw std::logic_error("wrong type"); }, "wrong type"); },
        "unexpected exception type");
    rejects<AssertionFailure>(
        [] { rejects([] { throw std::runtime_error("unrelated"); }, "expected"); },
        "expected error containing");
    rejects<AssertionFailure>([] { rejects([] { require(false, "inner assertion"); }, "inner"); },
                              "unexpected exception type");
    rejects<AssertionFailure>([] { require(false); }, "assertions.cpp:");
    rejects<AssertionFailure>([] { rejects([] { throw 7; }, "expected"); },
                              "unexpected non-standard exception");
}
void scoped_state_is_restored(const wt::fs::path& root) {
    ScopedEnv original("WT_TEST_SCOPE", "original");
    auto previous_path = wt::fs::current_path();
    std::ostringstream output, captured;
    rejects(
        [&] {
            ScopedEnv changed("WT_TEST_SCOPE", "changed");
            ScopedCurrentPath current(root);
            StreamCapture capture(output, captured.rdbuf());
            require(std::string(std::getenv("WT_TEST_SCOPE")) == "changed");
            require(wt::fs::current_path() == root);
            output << "inside";
            throw std::runtime_error("unwind scopes");
        },
        "unwind scopes");
    require(std::string(std::getenv("WT_TEST_SCOPE")) == "original");
    require(wt::fs::current_path() == previous_path);
    output << "outside";
    require(captured.str() == "inside" && output.str() == "outside");
    {
        ScopedEnv absent("WT_TEST_SCOPE", std::nullopt);
        require(!std::getenv("WT_TEST_SCOPE"));
    }
    require(std::string(std::getenv("WT_TEST_SCOPE")) == "original");
}
void temporary_directory_lifetime(const wt::fs::path& root) {
    wt::fs::path removed, retained;
    {
        TempDirectory first(root), second(root);
        removed = first.path;
        retained = second.path;
        require(removed != retained && wt::fs::is_directory(removed));
        second.preserve();
    }
    require(!wt::fs::exists(removed) && wt::fs::is_directory(retained));
}
} // namespace
int main() {
    return run_tests({{"assertion locations and expected exception contracts", assertions},
                      {"scoped state is restored after failure", scoped_state_is_restored},
                      {"temporary directory lifetime", temporary_directory_lifetime}});
}
