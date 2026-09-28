#pragma once
#include "inference/options.hpp"
#include "transcript/types.hpp"
#include <cstddef>
#include <functional>
#include <memory>

namespace wt {
struct PreparedModel;
struct DeviceSelection;
class WhisperSession {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    WhisperSession(const InferenceOptions& options, const PreparedModel& model,
                   const PreparedModel& vad, const DeviceSelection& device);
    ~WhisperSession();
    size_t choose_cut(const std::vector<float>& pcm, int minimum_silence_ms);
    Transcript recognize(const std::vector<float>& pcm, const std::function<void()>& begin,
                         const std::function<void(int)>& progress);
};
} // namespace wt
