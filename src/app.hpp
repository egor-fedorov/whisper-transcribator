#pragma once
#include "report.hpp"
#include <csignal>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <ostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace wt {
namespace fs = std::filesystem;
using Json = nlohmann::ordered_json;
struct UsageError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
struct Cancelled {};
extern volatile std::sig_atomic_t stop_signal;
void check_cancelled();
struct Options {
    std::string command = "transcribe";
    std::vector<std::string> inputs;
    std::string input_dir, output, output_dir;
    std::string format = "text", naming = "source", prefix = "result";
    std::string model = "small", download_root, language = "ru", device = "auto", vad_model;
    int cpu_threads = 0, beam_size = 5, vad_min_silence_ms = 2000;
    int chunk_seconds = 120;
    int chunk_min_silence_ms = 200;
    int audio_stream = -1;
    bool resume = false;
    bool overwrite = false, skip_existing = false, continue_on_error = false;
    bool local_files_only = false, no_vad = false, json = false;
    bool quiet = false, verbose = false;
};
struct Job {
    fs::path source;
    std::map<std::string, fs::path> outputs;
};
struct Segment {
    double start, end;
    std::string text;
    double no_speech_probability;
    std::string language = "";
};
struct Transcript {
    std::string language;
    double duration;
    std::vector<Segment> segments;
};
fs::path resolve_path(const fs::path& path);
bool same_file(const fs::path& a, const fs::path& b);
void probe_directory(const fs::path& path);
void atomic_write(const fs::path& path, const std::string& content, bool overwrite = false);
void atomic_write_stream(const fs::path& path, const std::function<void(std::ostream&)>& write,
                         bool overwrite = false, bool private_file = false);
void sync_directory(const fs::path& path);
void publish_file(const fs::path& temporary, const fs::path& target, bool overwrite);
std::string read_text(const fs::path& path);
std::string sha256(const fs::path& path);
std::string sha256_text(const std::string& text);
std::string trim(const std::string& value);
std::string env(const char* key);
std::vector<Job> prepare_jobs(const Options& options);
std::string timestamp(double seconds);
struct Model {
    std::string name, file, url, hash;
    uint64_t bytes;
};
std::vector<Model> model_catalog();
fs::path model_root(const Options& options);
using Fetch = std::function<void(const std::string&, const fs::path&)>;
struct PreparedModel {
    fs::path path;
    std::string hash;
};
void fetch_https(const std::string& url, const fs::path& target);
PreparedModel ensure_cached(const Model& model, const fs::path& root, bool offline,
                            const Fetch& fetch = fetch_https);
PreparedModel prepare_model(const std::string& name, const Options& options);
Json list_models(const Options& options);
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
std::string select_device(const std::string& requested);
Json doctor(const Options& options);
int transcribe(Options options);
int run_cli(int argc, char** argv);
} // namespace wt
