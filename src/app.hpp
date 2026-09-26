#pragma once
#include "json.hpp"
#include <csignal>
#include <filesystem>
#include <functional>
#include <map>
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
    bool overwrite = false, skip_existing = false, continue_on_error = false;
    bool local_files_only = false, no_vad = false, json = false;
};
struct Job {
    fs::path source;
    std::map<std::string, fs::path> outputs;
};
struct Segment {
    double start, end;
    std::string text;
    double no_speech_probability;
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
std::string read_text(const fs::path& path);
std::string sha256(const fs::path& path);
std::string trim(const std::string& value);
std::string env(const char* key);
std::vector<Job> prepare_jobs(const Options& options);
Json render_json(const Job& job, const Options& options, const Transcript& result);
void write_outputs(const Job& job, const Options& options, const Transcript& result);
std::string timestamp(double seconds);
struct Model {
    std::string name, file, url, hash;
    uint64_t bytes;
};
std::vector<Model> model_catalog();
fs::path model_root(const Options& options);
using Fetch = std::function<void(const std::string&, const fs::path&)>;
void fetch_https(const std::string& url, const fs::path& target);
fs::path ensure_cached(const Model& model, const fs::path& root, bool offline,
                       const Fetch& fetch = fetch_https);
fs::path prepare_model(const std::string& name, const Options& options);
Json list_models(const Options& options);
std::vector<float> decode_audio(const fs::path& path);
std::string select_device(const std::string& requested);
Json doctor(const Options& options);
int transcribe(Options options);
int run_cli(int argc, char** argv);
} // namespace wt
