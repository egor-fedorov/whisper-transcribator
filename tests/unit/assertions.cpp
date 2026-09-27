#include "support/test.hpp"

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
} // namespace
int main() {
    return run_tests({{"assertion locations and expected exception contracts", assertions}});
}
