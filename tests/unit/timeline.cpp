#include "audio/timeline.hpp"
#include "support/test.hpp"
#include <limits>

using namespace wt;
using namespace wt::test;
namespace {
void positions(const fs::path&) {
    AudioTimeline timeline;
    auto first = timeline.locate(80000, 0, 1, false);
    require(first.start == 80000 && first.discontinuity);
    timeline.advance(first.start, 16000);
    require(timeline.locate(std::nullopt, 16, 1, false).start == 96016);
    require(!timeline.locate(96015, 16, 1, false).discontinuity);
    require(timeline.locate(96000 + 6 * 16000, 0, 1, false).discontinuity);
    auto overlap = timeline.locate(95500, 0, 1, false);
    require(overlap.start == 96000 && !overlap.discontinuity);
    rejects([&] { timeline.locate(1000, 0, 1, false); }, "backwards");
    auto reset = timeline.locate(1000, 0, 1, true);
    require(reset.reset && reset.start == 96000);
    timeline.advance(reset.start, 1024);
    require(!timeline.locate(2024, 0, 1, true).discontinuity);
    AudioTimeline preroll;
    require(preroll.locate(-1024, 0, 1, false).start == -1024);
    preroll.advance(-1024, 2048);
    require(preroll.end() == 1024);
    AudioTimeline long_preroll;
    require(long_preroll.locate(-8000, 0, 1, false).start == -8000);
    long_preroll.advance(-8000, 1000);
    require(long_preroll.locate(-7000, 0, 1, false).start == -7000);
    long_preroll.advance(-7000, 8000);
    require(long_preroll.end() == 1000);
    AudioTimeline huge;
    auto location = huge.locate(int64_t(86400) * 16000, 0, 1, false);
    huge.advance(location.start, 160);
    require(huge.end() == int64_t(86400) * 16000 + 160);
    rejects([&] { huge.locate(std::numeric_limits<int64_t>::max(), 0, 1, false); }, "out of range");
}
void jitter(const fs::path&) {
    for (int64_t first : {-800, 400}) {
        AudioTimeline initial;
        require(initial.locate(first, 0, 1, false).start == first);
    }
    AudioTimeline clock;
    clock.locate(0, 0, 1, false);
    clock.advance(0, 16000);
    for (int i = 0; i < 2000; ++i) {
        auto end = clock.end();
        auto point = clock.locate(end + 16 + (i % 2 ? -128 : 128), 16, 1, false);
        require(point.start == end + 16 && !point.discontinuity);
        clock.advance(end, 1024);
    }
    for (int64_t delta : {-1600, -1599, 1599, 1600})
        require(!clock.locate(clock.end() + delta, 0, 1, false).discontinuity);
    require(clock.locate(clock.end() + 1601, 0, 1, false).discontinuity);
    rejects([&] { clock.locate(clock.end() - 1601, 0, 1, false); }, "backwards");
    require(!clock.locate(clock.end() + 2000, 0, 2000, false).discontinuity);
    // Small timestamp drift must accumulate against samples, not a moving PTS anchor.
    AudioTimeline drift;
    drift.locate(0, 0, 1, false);
    for (int i = 1; i <= 17; ++i) {
        drift.advance(drift.end(), 1024);
        auto point = drift.locate(drift.end() + i * 100, 0, 1, false);
        require(point.discontinuity == (i == 17));
    }
}
void gap_policy(const fs::path&) {
    for (bool transport : {false, true})
        for (auto policy : {TimestampGaps::automatic, TimestampGaps::preserve})
            for (int64_t gap : {int64_t(6 * 16000), int64_t(10 * 16000), int64_t(10 * 16000 + 1),
                                int64_t(3 * 3600 * 16000)}) {
                AudioTimeline clock;
                clock.locate(0, 0, 1, transport, policy);
                clock.advance(0, 16000);
                auto point = clock.locate(16000 + gap, 0, 1, transport, policy);
                bool reset = transport && policy == TimestampGaps::automatic && gap > 160000;
                require(point.reset == reset);
                require(point.start == (reset ? 16000 : 16000 + gap));
                clock.advance(point.start, 16000);
                require(!clock.locate(32000 + gap, 0, 1, transport, policy).discontinuity);
            }
    AudioTimeline leading;
    require(!leading.locate(3 * 3600 * 16000, 0, 1, true).reset);
    AudioTimeline outlier;
    outlier.locate(0, 0, 1, true);
    outlier.advance(0, 16000);
    require(outlier.locate(3 * 3600 * 16000, 0, 1, true).reset);
    outlier.advance(16000, 16000);
    require(outlier.locate(32000, 0, 1, true).reset);
    outlier.advance(32000, 16000);
    require(!outlier.locate(48000, 0, 1, true).discontinuity);
}
} // namespace
int main() {
    return run_tests({{"timestamps, missing PTS, overlaps and discontinuities", positions},
                      {"codec jitter and accumulated clock drift", jitter},
                      {"transport gap policies and isolated outliers", gap_policy}});
}
