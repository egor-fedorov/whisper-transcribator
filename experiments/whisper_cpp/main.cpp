#include "whisper.h"
#include "ggml-backend.h"
#include "json.hpp"
#include <algorithm>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

namespace fs = std::filesystem;
using json = nlohmann::ordered_json;
using Clock = std::chrono::steady_clock;
std::vector<float> decode_audio(const std::string &path);
static volatile std::sig_atomic_t interrupted = 0;
static void stop(int signum) { interrupted = signum; }
static double elapsed(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

static void publish(const fs::path &path, const std::string &text) {
    std::string pattern = (path.parent_path() / ".whisper-native-XXXXXX").string();
    std::vector<char> name(pattern.begin(), pattern.end());
    name.push_back(0);
    int fd = mkstemp(name.data());
    if (fd < 0) throw std::runtime_error("cannot create temporary output");
    try {
        size_t offset = 0;
        while (offset < text.size()) {
            ssize_t written = write(fd, text.data() + offset, text.size() - offset);
            if (written <= 0) throw std::runtime_error("cannot write output");
            offset += static_cast<size_t>(written);
        }
        mode_t mask = umask(0);
        umask(mask);
        if (fchmod(fd, 0666 & ~mask) || fsync(fd)) throw std::runtime_error("cannot flush output");
        if (close(fd)) { fd = -1; throw std::runtime_error("cannot close output"); }
        fd = -1;
        if (link(name.data(), path.c_str())) throw std::runtime_error("cannot publish output (already exists or filesystem does not support links)");
    } catch (...) {
        if (fd >= 0) close(fd);
        unlink(name.data());
        throw;
    }
    unlink(name.data());
}

static std::string trim(const std::string &text) {
    auto first = text.find_first_not_of(" \r\n\t");
    return first == std::string::npos ? "" : text.substr(first, text.find_last_not_of(" \r\n\t") - first + 1);
}

static std::string timestamp(int64_t ticks) {
    int64_t ms = ticks * 10;
    char out[64];
    snprintf(out, sizeof(out), "%02lld:%02lld:%02lld,%03lld",
             static_cast<long long>(ms / 3600000), static_cast<long long>(ms / 60000 % 60),
             static_cast<long long>(ms / 1000 % 60), static_cast<long long>(ms % 1000));
    return out;
}

int main(int argc, char **argv) {
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    try {
        std::string model, vad_model, output, device = "cpu", language = "ru", format = "all";
        int threads = 4, beam = 5;
        bool vad = true;
        std::vector<fs::path> inputs;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            auto value = [&]() { if (++i >= argc) throw std::invalid_argument("missing value: " + arg); return std::string(argv[i]); };
            if (arg == "--version") { std::cout << "0.2.0-experimental whisper.cpp " << whisper_version() << '\n'; return 0; }
            if (arg == "--help") {
                std::cout << "Usage: whisper-transcribator-native FILE... --model GGML_FILE --output-dir DIR\n"
                    "  --device cpu|cuda --language ru|en|auto --format text|srt|json|all\n"
                    "  --cpu-threads N --beam-size N --vad-model FILE | --no-vad\n"
                    "Experimental: local models only, no overwrite, batching or word timestamps.\n";
                return 0;
            }
            if (arg == "--model") model = value();
            else if (arg == "--output-dir") output = value();
            else if (arg == "--device") device = value();
            else if (arg == "--language") language = value();
            else if (arg == "--format") format = value();
            else if (arg == "--cpu-threads") threads = std::stoi(value());
            else if (arg == "--beam-size") beam = std::stoi(value());
            else if (arg == "--vad-model") vad_model = value();
            else if (arg == "--no-vad") vad = false;
            else if (arg == "--") { while (++i < argc) inputs.emplace_back(argv[i]); }
            else if (arg.rfind("-", 0) == 0) throw std::invalid_argument("unsupported option: " + arg);
            else inputs.emplace_back(arg);
        }
        if (inputs.empty() || output.empty() || !fs::is_regular_file(model)) throw std::invalid_argument("inputs, output directory and local GGML model are required");
        if (threads < 1 || beam < 1) throw std::invalid_argument("threads and beam size must be positive");
        if (device != "cpu" && device != "cuda") throw std::invalid_argument("device must be cpu or cuda");
        if (language != "auto" && whisper_lang_id(language.c_str()) < 0) throw std::invalid_argument("unknown language");
        if (vad && !fs::is_regular_file(vad_model)) throw std::invalid_argument("VAD requires --vad-model; alternatively use --no-vad");
        std::vector<std::string> extensions;
        if (format == "all") extensions = {"txt", "srt", "json"};
        else if (format == "text") extensions = {"txt"};
        else if (format == "srt" || format == "json") extensions = {format};
        else throw std::invalid_argument("unsupported format");
        fs::create_directories(output);
        std::set<fs::path> destinations;
        for (auto &input : inputs) {
            input = fs::canonical(input);
            if (!fs::is_regular_file(input)) throw std::invalid_argument("not a media file");
            for (const auto &ext : extensions) {
                auto target = fs::weakly_canonical(fs::path(output) / (input.stem().string() + "." + ext));
                if (fs::exists(target) || !destinations.insert(target).second) throw std::invalid_argument("output exists or collides: " + target.string());
            }
        }
        ggml_backend_load_all();
        bool cuda = false;
        for (size_t i = 0; i < ggml_backend_dev_count(); ++i) {
            auto dev = ggml_backend_dev_get(i);
            if (std::string(ggml_backend_reg_name(ggml_backend_dev_backend_reg(dev))) == "CUDA" &&
                ggml_backend_dev_type(dev) == GGML_BACKEND_DEVICE_TYPE_GPU) cuda = true;
        }
        if (device == "cuda" && !cuda) throw std::runtime_error("CUDA requested but unavailable; no CPU fallback");
        auto options = whisper_context_default_params();
        options.use_gpu = device == "cuda";
        options.flash_attn = true;
        auto load_start = Clock::now();
        std::unique_ptr<whisper_context, decltype(&whisper_free)> context(
            whisper_init_from_file_with_params(model.c_str(), options), whisper_free);
        if (!context) throw std::runtime_error("cannot initialize model");
        double model_seconds = elapsed(load_start);
        for (const auto &input : inputs) {
            if (interrupted) return 128 + interrupted;
            auto start = Clock::now();
            std::cerr << "Transcribing " << input << '\n';
            auto pcm = decode_audio(input.string());
            double decode_seconds = elapsed(start);
            if (pcm.size() > static_cast<size_t>(std::numeric_limits<int>::max())) throw std::runtime_error("audio too long for whisper_full");
            auto params = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
            params.n_threads = threads;
            params.beam_search.beam_size = beam;
            params.language = language.c_str();
            params.print_progress = params.print_realtime = params.print_timestamps = false;
            params.vad = vad;
            params.vad_model_path = vad_model.c_str();
            params.vad_params.min_silence_duration_ms = 2000;
            params.abort_callback = [](void *) { return interrupted != 0; };
            params.encoder_begin_callback = [](whisper_context *, whisper_state *, void *) { return interrupted == 0; };
            int progress = -1;
            params.progress_callback_user_data = &progress;
            params.progress_callback = [](whisper_context *, whisper_state *, int value, void *data) {
                auto &last = *static_cast<int *>(data);
                if (value / 10 != last) { last = value / 10; std::cerr << "Progress: " << value << "%\n"; }
            };
            auto inference_start = Clock::now();
            int status = whisper_full(context.get(), params, pcm.data(), static_cast<int>(pcm.size()));
            if (interrupted) return 128 + interrupted;
            if (status != 0) throw std::runtime_error("inference failed");
            double inference_seconds = elapsed(inference_start);
            json segments = json::array();
            std::string text, srt;
            for (int i = 0; i < whisper_full_n_segments(context.get()); ++i) {
                auto part = trim(whisper_full_get_segment_text(context.get(), i));
                if (part.empty()) continue;
                auto from = whisper_full_get_segment_t0(context.get(), i);
                auto to = whisper_full_get_segment_t1(context.get(), i);
                segments.push_back({{"id", segments.size()}, {"start", from / 100.0}, {"end", to / 100.0}, {"text", part},
                                    {"avg_logprob", nullptr}, {"compression_ratio", nullptr},
                                    {"no_speech_prob", whisper_full_get_segment_no_speech_prob(context.get(), i)}});
                if (!text.empty()) text += ' ';
                text += part;
                srt += std::to_string(segments.size()) + '\n' + timestamp(from) + " --> " + timestamp(to) + '\n' + part + "\n\n";
            }
            if (text.empty()) throw std::runtime_error("no transcript produced");
            json result = {{"schema_version", 1}, {"source", input.string()}, {"model", fs::path(model).filename().string()},
                {"language", whisper_lang_str(whisper_full_lang_id(context.get()))}, {"language_probability", nullptr},
                {"duration", pcm.size() / 16000.0}, {"duration_after_vad", nullptr}, {"text", text}, {"segments", segments},
                {"run", {{"backend", "whisper.cpp"}, {"backend_version", whisper_version()}, {"device", device},
                         {"beam_size", beam}, {"cpu_threads", threads}, {"vad", vad}, {"flash_attention", true},
                         {"model_load_seconds", model_seconds}, {"decode_seconds", decode_seconds},
                         {"inference_seconds", inference_seconds}}}};
            for (const auto &ext : extensions) {
                auto target = fs::path(output) / (input.stem().string() + "." + ext);
                publish(target, ext == "txt" ? text + '\n' : ext == "srt" ? srt : result.dump(2) + '\n');
            }
            std::cerr << "Done: " << input.filename() << "; elapsed " << elapsed(start) << "s\n";
        }
        return 0;
    } catch (const std::invalid_argument &error) {
        std::cerr << error.what() << '\n'; return 2;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return interrupted ? 128 + interrupted : 1;
    }
}
