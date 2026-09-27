#include "audio/recovery.hpp"
#include "support/test.hpp"
extern "C" {
#include <libavutil/error.h>
}

using namespace wt;
using namespace wt::test;
namespace {
void classification(const fs::path&) {
    DecodeRecovery policy;
    for (int code :
         {0, AVERROR(EAGAIN), AVERROR_EOF, AVERROR(EIO), AVERROR(ENOMEM), AVERROR(EINVAL)})
        require(!policy.recover(code));
    require(policy.errors() == 0);
    policy.packet();
    require(policy.recover(AVERROR_INVALIDDATA));
    require(policy.errors() == 1);
}
void consecutive(const fs::path&) {
    for (bool new_packet : {false, true}) {
        DecodeRecovery policy;
        for (int i = 0; i < 7; ++i) {
            if (new_packet)
                policy.packet();
            require(policy.recover(AVERROR_INVALIDDATA));
        }
        rejects([&] { policy.recover(AVERROR_INVALIDDATA); }, "consecutive audio decoder errors");
    }
    DecodeRecovery policy;
    for (int i = 0; i < 1000; ++i) {
        policy.packet();
        if (i % 8 == 0)
            require(policy.recover(AVERROR_INVALIDDATA));
        policy.frame();
    }
    require(policy.errors() == 125);
}
void density(const fs::path&) {
    DecodeRecovery policy;
    for (int i = 0; i < 31; ++i) {
        policy.packet();
        require(policy.recover(AVERROR_INVALIDDATA));
        policy.frame();
        policy.packet();
        policy.frame();
    }
    policy.packet();
    rejects([&] { policy.recover(AVERROR_INVALIDDATA); }, "32 within 128 input packets");
    DecodeRecovery aged;
    for (int i = 0; i < 31; ++i) {
        aged.packet();
        require(aged.recover(AVERROR_INVALIDDATA));
        aged.frame();
    }
    for (int i = 0; i < 128; ++i)
        aged.packet();
    require(aged.recover(AVERROR_INVALIDDATA));
    require(aged.errors() == 32);
}
} // namespace
int main() {
    return run_tests({{"only invalid decoder data is recoverable", classification},
                      {"bounded consecutive receive and send failures", consecutive},
                      {"rolling decoder-error budget and expiry", density}});
}
