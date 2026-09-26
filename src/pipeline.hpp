#pragma once
#include "app.hpp"

namespace wt {
constexpr int sample_rate = 16000;
constexpr int chunking_version = 2;
fs::path checkpoint_path(const Job& job);
bool has_checkpoint(const Job& job);
Json run_metadata(const Options& options);
Json job_fingerprint(const Job& job, const Options& options, const Json& backend);

class Journal {
    struct Lock;
    std::unique_ptr<Lock> lock;
    fs::path directory;
    Json state;
    void save();
    fs::path chunk_path(int64_t index) const;

  public:
    Journal(const Job& job, const Options& options, const Json& fingerprint);
    ~Journal();
    int64_t samples() const;
    std::string language() const;
    bool finished() const;
    void append(int64_t count, const Transcript& transcript);
    void finish();
    void visit(const std::function<void(const Segment&)>& consumer) const;
    void publish(const Job& job, const Options& options);
};
using ReadAudio = std::function<std::vector<float>(size_t)>;
using Recognize = std::function<Transcript(const std::vector<float>&, const std::string&)>;
using ChooseCut = std::function<size_t(const std::vector<float>&)>;
void run_chunks(Journal& journal, size_t limit, const ReadAudio& read, const Recognize& recognize,
                const ChooseCut& cut);
size_t pause_cut(size_t samples, const std::vector<std::pair<int64_t, int64_t>>& speech,
                 int minimum_silence_ms);
void render_stream(std::ostream& stream, const std::string& format, const Job& job,
                   const Options& options, const Journal& journal);
} // namespace wt
