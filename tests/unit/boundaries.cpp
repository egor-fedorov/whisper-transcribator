#include "support/test.hpp"
#include "transcript/pipeline.hpp"
#include <limits>

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
void boundary_validation(const fs::path&) {
    auto rejected = [](const auto& operation) {
        bool failed = false;
        try {
            operation();
        } catch (const std::runtime_error&) {
            failed = true;
        }
        require(failed, "invalid boundary input accepted");
    };
    rejected([] { pause_cut(16000, {{-1, 100}}, 100); });
    rejected([] { pause_cut(16000, {{0, 100}, {99, 200}}, 100); });
    rejected([] { pause_cut(16000, {{0, 16001}}, 100); });
    Transcript transcript{"en", 4, {{0, 1, "one", 0}, {0.5, 2, "overlap", 0}, {2, 4, "tail", 0}}};
    require(committed_cut(4 * sample_rate, 4 * sample_rate, transcript, 0) == 4 * sample_rate);
    require(committed_cut(4 * sample_rate, 3 * sample_rate, transcript, 0) == 2 * sample_rate);
    rejected([&] { committed_cut(4 * sample_rate, 0, transcript); });
    rejected([&] { committed_cut(4 * sample_rate, 4 * sample_rate + 1, transcript); });
    for (double end : {-1.0, 4.1, std::numeric_limits<double>::quiet_NaN()}) {
        transcript.segments = {{0, end, "invalid", 0}};
        rejected([&] { committed_cut(4 * sample_rate, 4 * sample_rate, transcript); });
    }
}
} // namespace
int main() {
    return run_tests({
        {"pause selection", pause_selection},
        {"boundary validation and overlapping segments", boundary_validation},
    });
}
