#pragma once
#include "support/fs.hpp"
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace wt {
class AudioReader {
    struct Impl;
    std::unique_ptr<Impl> impl;

  public:
    explicit AudioReader(const fs::path& path, int stream = -1);
    ~AudioReader();
    std::vector<float> read(size_t limit);
    int stream_index() const;
    double duration() const;
};
std::string audio_backend_version();
void configure_audio_logging();
} // namespace wt
