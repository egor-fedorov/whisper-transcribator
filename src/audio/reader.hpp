#pragma once
#include "audio/recovery.hpp"
#include "audio/timeline.hpp"
#include "support/fs.hpp"
#include <cstddef>
#include <memory>
#include <vector>

namespace wt {
class AudioReader {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    explicit AudioReader(const fs::path& path, int stream = -1,
                         TimestampGaps gaps = TimestampGaps::automatic,
                         DecodeErrorPolicy errors = {});
    ~AudioReader();
    std::vector<float> read(size_t limit);
    int stream_index() const;
    double duration() const;
};
} // namespace wt
