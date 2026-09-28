#pragma once
#include "transcript/types.hpp"
#include <cstddef>
#include <functional>
#include <memory>

namespace wt {
struct Options;
struct PreparedModel;
struct DeviceSelection;
class WhisperSession {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    WhisperSession(const Options& options, const PreparedModel& model, const PreparedModel& vad,
                   const DeviceSelection& device);
    ~WhisperSession();
    size_t choose_cut(const std::vector<float>& pcm);
    Transcript recognize(const std::vector<float>& pcm, const std::function<void()>& begin,
                         const std::function<void(int)>& progress);
};
} // namespace wt
