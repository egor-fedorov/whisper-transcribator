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
    require(overlap.start == 95500 && !overlap.reset);
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
} // namespace
int main() {
    return run_tests({{"timestamps, missing PTS, overlaps and discontinuities", positions}});
}
