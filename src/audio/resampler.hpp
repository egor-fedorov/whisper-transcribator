#pragma once
#include <cstdint>
#include <memory>
#include <vector>

struct AVFrame;
namespace wt {
class FrameResampler {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    FrameResampler();
    ~FrameResampler();
    std::vector<float> convert(const AVFrame& frame);
    std::vector<float> drain();
    int64_t delay() const;
    void reset();
};
} // namespace wt
