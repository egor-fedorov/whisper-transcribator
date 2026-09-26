#include "support/test.hpp"
#include "transcript/pipeline.hpp"

using namespace wt;
using namespace wt::test;
namespace {
void pause_selection(const fs::path&) {
    Transcript boundary{"en", 30, {{0, 10, "one", 0}, {10, 25, "two", 0}, {25, 30, "tail", 0}}};
    require(committed_cut(30 * sample_rate, 30 * sample_rate, boundary) == 25 * sample_rate);
    require(committed_cut(30 * sample_rate, 24 * sample_rate, boundary) == 10 * sample_rate);
    boundary.segments = {{0, 30, "unsplittable", 0}};
    require(committed_cut(30 * sample_rate, 30 * sample_rate, boundary) == 30 * sample_rate);
    boundary.segments.clear();
    require(committed_cut(30 * sample_rate, 25 * sample_rate, boundary) == 30 * sample_rate);
    require(pause_cut(16000, {{0, 13000}, {15000, 16000}}, 100) == 14000);
    require(pause_cut(16000, {{0, 16000}}, 100) == 16000);
    require(pause_cut(16000, {}, 100) == 16000);
    require(pause_cut(16000, {{0, 12000}}, 100) == 14000);
    require(pause_cut(16000, {{0, 13000}, {15000, 16000}}, 2000) == 16000);
    require(pause_cut(16000, {{0, 12000}, {15200, 16000}}, 200) == 13600);
    require(pause_cut(16000, {{0, 12000}, {15199, 16000}}, 200) == 16000);
    require(pause_cut(16000, {{0, 12000}, {12001, 16000}}, 0) == 12000);
}
} // namespace
int main() {
    return run_tests({
        {"pause selection", pause_selection},
    });
}
