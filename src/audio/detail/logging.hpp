#pragma once

namespace wt::audio_detail {
// Only the metadata re-probe with raw timestamps suppresses its benign timing warning.
class DecodeProbeLogging {
    const void* previous;

  public:
    explicit DecodeProbeLogging(const void* context);
    ~DecodeProbeLogging();
    DecodeProbeLogging(const DecodeProbeLogging&) = delete;
    DecodeProbeLogging& operator=(const DecodeProbeLogging&) = delete;
};
} // namespace wt::audio_detail
