#pragma once
#include "app.hpp"

namespace wt {
constexpr int sample_rate = 16000;
constexpr int chunking_version = 3;
fs::path checkpoint_path(const Job& job);
bool has_checkpoint(const Job& job);
Json run_metadata(const Options& options);
Json job_fingerprint(const Job& job, const Options& options, const Json& backend);

class Journal {
    struct Lock;
    std::unique_ptr<Lock> lock;
    fs::path directory;
    Json state;
    bool persisted = false;
    void save();
    fs::path chunk_path(int64_t index) const;

  public:
    Journal(const Job& job, const Options& options, const Json& fingerprint);
    ~Journal();
    int64_t samples() const;
    std::string language() const;
    std::vector<std::string> languages() const;
    bool finished() const;
    void append(int64_t count, const Transcript& transcript);
    void finish();
    void visit(const std::function<void(const Segment&)>& consumer) const;
    void publish(const Job& job, const Options& options);
};
using ReadAudio = std::function<std::vector<float>(size_t)>;
using Recognize = std::function<Transcript(const std::vector<float>&, const std::string&)>;
using ChooseCut = std::function<size_t(const std::vector<float>&)>;
using WindowProgress = std::function<void(int64_t, size_t, bool)>;
void run_chunks(Journal& journal, size_t limit, const ReadAudio& read, const Recognize& recognize,
                const ChooseCut& cut, const WindowProgress& progress = {},
                size_t guard_samples = 2 * sample_rate);
size_t committed_cut(size_t samples, size_t preferred, const Transcript& transcript,
                     size_t guard_samples = 2 * sample_rate);
size_t pause_cut(size_t samples, const std::vector<std::pair<int64_t, int64_t>>& speech,
                 int minimum_silence_ms);
void render_stream(std::ostream& stream, const std::string& format, const Job& job,
                   const Options& options, const Journal& journal);
} // namespace wt
