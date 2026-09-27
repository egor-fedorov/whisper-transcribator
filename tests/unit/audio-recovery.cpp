#include "audio/recovery.hpp"
#include "support/test.hpp"
#include <limits>
extern "C" {
#include <libavutil/error.h>
}

using namespace wt;
using namespace wt::test;
namespace {
void classification(const fs::path&) {
    DecodeRecovery policy;
    for (int code :
         {0, 1, AVERROR(EAGAIN), AVERROR_EOF, AVERROR(ENOMEM), AVERROR_EXIT, AVERROR(EINTR)})
        require(!policy.recover(code));
    require(policy.errors() == 0);
    for (int code : {AVERROR_INVALIDDATA, AVERROR_PATCHWELCOME, AVERROR(EINVAL), AVERROR(EIO),
                     AVERROR_BUG, AVERROR_UNKNOWN}) {
        policy.packet(0.02);
        require(policy.recover(code));
    }
    require(policy.errors() == 6);
    DecodeRecovery strict({true, 0});
    require(!strict.recover(AVERROR(EAGAIN)));
    rejects([&] { strict.recover(AVERROR_PATCHWELCOME); }, "--decode-errors tolerant");
    rejects([&] { strict.recover(AVERROR_INVALIDDATA); }, "Repeating --resume");
    rejects<std::invalid_argument>([] { DecodeRecovery invalid({false, -1}); }, "Negative");
}
void duration(const fs::path&) {
    // The same input duration has the same budget, regardless of packetization.
    for (int packets_per_second : {1, 50, 100}) {
        DecodeRecovery policy;
        for (int i = 0; i < 29 * packets_per_second; ++i) {
            policy.packet(1.0 / packets_per_second);
            require(policy.recover(AVERROR_INVALIDDATA));
        }
        policy.packet(2);
        rejects([&] { policy.recover(AVERROR_INVALIDDATA); }, "30 seconds of input audio");
        rejects([&] { policy.awaiting_input(); }, "--decode-error-limit-seconds");
        rejects([&] { policy.awaiting_input(); }, "--overwrite without --resume");
    }
    DecodeRecovery aac;
    for (int i = 0; i < 47; ++i) {
        aac.packet(1024.0 / 48000);
        require(aac.recover(AVERROR_INVALIDDATA));
    }
    aac.frame();
    require(aac.errors() == 47);
}
void progress(const fs::path&) {
    DecodeRecovery policy({false, 2});
    policy.packet(1);
    require(policy.recover(AVERROR_INVALIDDATA));
    policy.packet(1);
    require(!policy.recover(0)); // Accepting a packet is not a successful frame.
    rejects([&] { policy.awaiting_input(); }, "2 seconds");
    policy.frame();
    policy.awaiting_input();
    for (int i = 0; i < 1000; ++i) {
        policy.packet(1);
        require(policy.recover(AVERROR_PATCHWELCOME));
        policy.frame();
    }
    require(policy.errors() == 1001);
    // Multiple receive errors for one packet must not multiply its duration.
    policy.packet(1);
    for (int i = 0; i < 100; ++i)
        require(policy.recover(AVERROR_INVALIDDATA));
    policy.awaiting_input();
}
void unlimited(const fs::path&) {
    DecodeRecovery policy({false, 0});
    for (int i = 0; i < 10000; ++i) {
        policy.packet(60);
        require(policy.recover(AVERROR_INVALIDDATA));
        policy.awaiting_input();
    }
    require(policy.errors() == 10000);
    DecodeRecovery unknown;
    for (double duration : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                            std::numeric_limits<double>::quiet_NaN()}) {
        unknown.packet(duration);
        require(unknown.recover(AVERROR_INVALIDDATA));
        unknown.awaiting_input();
    }
}
void stalled(const fs::path&) {
    for (int limit : {0, 30}) {
        DecodeRecovery policy({false, limit});
        policy.packet(0.02);
        for (int i = 0; i < 1023; ++i)
            require(policy.recover(AVERROR_INVALIDDATA));
        rejects([&] { policy.recover(AVERROR_INVALIDDATA); }, "no progress after 1024 errors");
        policy.packet(0.02);
        for (int i = 0; i < 1023; ++i)
            require(policy.recover(AVERROR_INVALIDDATA));
        policy.frame();
        require(policy.recover(AVERROR_INVALIDDATA));
    }
}
} // namespace
int main() {
    return run_tests({{"decoder error classification and strict mode", classification},
                      {"input-duration budget instead of packet limits", duration},
                      {"successful frames reset the error budget", progress},
                      {"disabled time limit and unknown durations", unlimited},
                      {"no-progress guard stays enabled without a time limit", stalled}});
}
