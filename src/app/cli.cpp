#include "app/cli.hpp"
#include "app/commands.hpp"
#include "app/options.hpp"
#include "models/models.hpp"
#include "support/error.hpp"
#include "support/report.hpp"
#include "support/strings.hpp"
#include "version.hpp"
#include <CLI/CLI.hpp>
#include <algorithm>
#include <cctype>
#include <iostream>

namespace wt {
int run_cli(int argc, char** argv) {
    CliOptions o;
    std::string timestamp_gaps = o.audio.gaps == TimestampGaps::preserve ? "preserve" : "auto";
    std::string decode_errors = o.audio.errors.strict ? "strict" : "tolerant";
    if (!env("WHISPER_MODEL").empty())
        o.model = env("WHISPER_MODEL");
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i)
        args.emplace_back(argv[i]);
    if (!args.empty() && (args[0] == "transcribe" || args[0] == "models" || args[0] == "doctor")) {
        o.command = args[0];
        args.erase(args.begin());
    }
    CLI::App app{"Local media transcription via whisper.cpp", "whisper-transcribator"};
    app.set_version_flag("--version", WT_VERSION);
    auto* quiet = app.add_flag("--quiet", o.quiet, "Only warnings and errors; no progress");
    app.add_flag("--verbose", o.verbose, "Include backend diagnostics")->excludes(quiet);
    app.add_option("--download-root", o.cache.download_root,
                   "Model cache directory (XDG by default)");
    app.add_flag("--local-files-only", o.cache.local_files_only,
                 "Never download models or VAD weights");
    if (o.command == "models") {
        std::string action, name;
        app.add_option("action", action)->required()->check(CLI::IsMember({"list", "download"}));
        app.add_option("model", name);
        app.add_flag("--json", o.json);
        std::reverse(args.begin(), args.end());
        try {
            app.parse(args);
        } catch (const CLI::ParseError& error) {
            app.exit(error);
            return error.get_exit_code() ? 2 : 0;
        }
        configure_reporting(o.quiet, o.verbose);
        if (action == "download") {
            if (name.empty())
                throw UsageError("models download requires a model name or GGML path");
            std::cout << prepare_model(name, o.cache).path.string() << '\n';
        } else {
            if (!name.empty())
                throw UsageError("models list does not accept a model name");
            auto models = list_models(o.cache);
            if (o.json)
                std::cout << models.dump(2) << '\n';
            else
                for (const auto& item : models)
                    std::cout << item["name"].get<std::string>() << '\t'
                              << item["status"].get<std::string>() << '\t'
                              << item["path"].get<std::string>() << '\n';
        }
        return 0;
    }
    app.add_option("--device", o.device,
                   "Inference device; Vulkan is experimental and explicit GPU never falls back")
        ->check(CLI::IsMember({"auto", "cpu", "cuda", "metal", "vulkan"}))
        ->capture_default_str();
    app.add_option("--cpu-threads", o.inference.cpu_threads,
                   "0 selects physical cores within affinity and CPU quota")
        ->check(CLI::NonNegativeNumber);
    if (o.command == "doctor") {
        app.add_flag("--json", o.json);
    } else {
        app.footer(
            "Commands: transcribe (default), models list/download, doctor. Python-only jobs, "
            "batching, compute-type, prompts and word timestamps are not supported.");
        app.add_option("inputs", o.jobs.inputs,
                       "Media files (use -- before names starting with -)");
        app.add_option("--input-dir", o.jobs.input_dir,
                       "Discover media files in filename order, non-recursive");
        app.add_option("-o,--output", o.jobs.output, "Output path for one input and one format");
        app.add_option("--output-dir", o.jobs.output_dir,
                       "Destination directory; created if missing");
        app.add_option("--format", o.jobs.format,
                       "all writes TXT, SRT, VTT and JSON from one inference")
            ->check(CLI::IsMember({"text", "srt", "vtt", "json", "all"}))
            ->capture_default_str();
        app.add_option("--text-layout", o.rendering.text_layout, "TXT and JSON text layout")
            ->check(CLI::IsMember({"paragraphs", "single-line"}))
            ->capture_default_str();
        app.add_option("--paragraph-pause-ms", o.rendering.paragraph_pause_ms,
                       "Pause that starts a new paragraph")
            ->check(CLI::NonNegativeNumber)
            ->capture_default_str();
        app.add_option("--naming", o.jobs.naming,
                       "Source stems or stable numbers with a JSON mapping")
            ->check(CLI::IsMember({"source", "numbered"}))
            ->capture_default_str();
        app.add_option("--prefix", o.jobs.prefix, "Numbered filename prefix")
            ->capture_default_str();
        app.add_flag("--skip-existing", o.jobs.skip_existing, "Skip complete nonempty output sets");
        app.add_flag("--overwrite", o.checkpoint.overwrite,
                     "Replace outputs; restart saved progress unless --resume is also set");
        app.add_flag("--resume", o.checkpoint.resume,
                     "Continue compatible saved progress; otherwise start new");
        app.add_option("--chunk-seconds", o.chunking.chunk_seconds,
                       "Maximum audio window in seconds")
            ->check(CLI::Range(30, 600))
            ->capture_default_str();
        app.add_flag("--continue-on-error", o.continue_on_error,
                     "Continue after file failures; still exit nonzero");
        app.add_option("--chunk-min-silence-ms", o.chunking.chunk_min_silence_ms,
                       "Minimum VAD pause for an audio window boundary")
            ->check(CLI::NonNegativeNumber)
            ->capture_default_str();
        app.add_option("--model", o.model, "Catalog name or a local GGML model file")
            ->capture_default_str();
        app.add_option("--language", o.inference.language, "Language code or auto")
            ->capture_default_str();
        app.add_option("--audio-stream", o.audio.stream,
                       "Absolute container stream index; default selects the best audio stream")
            ->check(CLI::NonNegativeNumber);
        app.add_option("--timestamp-gaps", timestamp_gaps,
                       "auto joins forward transport timestamp jumps over 10s; preserve keeps gaps")
            ->check(CLI::IsMember({"auto", "preserve"}))
            ->capture_default_str();
        app.add_option("--decode-errors", decode_errors,
                       "tolerant skips damaged audio; strict stops on the first decoder error")
            ->check(CLI::IsMember({"strict", "tolerant"}))
            ->capture_default_str();
        app.add_option("--decode-error-limit-seconds", o.audio.errors.limit_seconds,
                       "Tolerant mode: input audio without a good frame after errors; 0 disables")
            ->check(CLI::NonNegativeNumber)
            ->capture_default_str();
        app.add_option("--beam-size", o.inference.beam_size, "Beam search width")
            ->check(CLI::Range(1, 8))
            ->capture_default_str();
        app.add_flag("--no-vad", o.inference.no_vad, "Disable voice activity detection");
        app.add_option("--vad-model", o.vad_model,
                       "Local GGML VAD model; otherwise downloaded automatically");
        app.add_option("--vad-min-silence-ms", o.inference.vad_min_silence_ms,
                       "Silence required to split speech")
            ->check(CLI::NonNegativeNumber)
            ->capture_default_str();
    }
    std::reverse(args.begin(), args.end());
    try {
        app.parse(args);
    } catch (const CLI::ParseError& error) {
        app.exit(error);
        return error.get_exit_code() ? 2 : 0;
    }
    configure_reporting(o.quiet, o.verbose);
    if (o.command == "doctor") {
        auto report = doctor(o);
        if (o.json)
            std::cout << report.dump(2) << '\n';
        else
            for (const auto& item : report.items())
                std::cout << item.key() << ": " << item.value() << '\n';
        return report["errors"].empty() ? 0 : 1;
    }
    o.audio.gaps =
        timestamp_gaps == "preserve" ? TimestampGaps::preserve : TimestampGaps::automatic;
    o.audio.errors.strict = decode_errors == "strict";
    o.inference.language = trim(o.inference.language);
    std::transform(o.inference.language.begin(), o.inference.language.end(),
                   o.inference.language.begin(), [](unsigned char c) { return std::tolower(c); });
    return transcribe(o);
}
} // namespace wt
