#pragma once
#include "audio/timeline.hpp"
#include "support/fs.hpp"
#include "support/json.hpp"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace wt {
class AudioReader {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    explicit AudioReader(const fs::path& path, int stream = -1,
                         TimestampGaps gaps = TimestampGaps::automatic);
    ~AudioReader();
    std::vector<float> read(size_t limit);
    int stream_index() const;
    double duration() const;
};
std::string audio_backend_version();
void configure_audio_logging(bool verbose = false);
Json audio_diagnostics();
} // namespace wt
