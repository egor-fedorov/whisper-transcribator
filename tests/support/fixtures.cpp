#include "support/fixtures.hpp"
#include "app/configuration.hpp"
#include "models/models.hpp"
#include "support/atomic.hpp"
#include "support/test.hpp"
#include "transcript/checkpoint/journal.hpp"
#include "transcript/jobs.hpp"
#include "transcript/metadata.hpp"
#include "transcript/pipeline.hpp"
#include "transcript/publication.hpp"
#include <algorithm>
#include <cmath>

namespace wt::test {
CliOptions input(const fs::path& root, const std::string& name) {
    atomic_write(root / name, "media");
    CliOptions o;
    o.jobs.inputs = {(root / name).string()};
    return o;
}
Model model_fixture() {
    return {"fixture", "fixture.bin", "https://example.invalid/model",
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 3};
}
void write_outputs(const Job& job, const CliOptions& options, const Transcript& result) {
    Journal journal(
        job, options.checkpoint,
        job_fingerprint(job, describe_run(options), options.inference.language, Json::object()),
        describe_output(options));
    journal.append(static_cast<int64_t>(std::llround(result.duration * sample_rate)), result);
    journal.finish();
    publish_outputs(job, options.rendering, journal, options.checkpoint.overwrite);
}
Fixture::Fixture(const fs::path& directory, bool reuse) : root(directory) {
    fs::create_directories(root);
    if (!reuse)
        atomic_write(root / "source.wav", "fake audio");
    options.jobs.inputs = {(root / "source.wav").string()};
    options.jobs.output_dir = root.string();
    options.jobs.format = "all";
    options.inference.language = "auto";
    options.chunking.chunk_seconds = 30;
    job = prepare_jobs(options.jobs, options.checkpoint).at(0);
}
Json Fixture::fingerprint() const {
    return job_fingerprint(job, describe_run(options), options.inference.language,
                           {{"model_sha256", "fixture"}});
}
std::vector<float> Audio::read(size_t limit) {
    auto count = static_cast<size_t>(std::min<int64_t>(limit, total - cursor));
    std::vector<float> result(count);
    for (auto& sample : result)
        sample = static_cast<float>(cursor++);
    return result;
}
Transcript Audio::recognize(const std::vector<float>& pcm) {
    require(!pcm.empty());
    for (size_t i = 1; i < pcm.size(); ++i)
        require(pcm[i] == pcm[i - 1] + 1, "non-contiguous PCM");
    peak = std::max(peak, pcm.size());
    recognized += static_cast<int64_t>(pcm.size());
    double duration = pcm.size() / double(sample_rate);
    Transcript result{"en", duration, {}};
    for (size_t i = 0; i < pcm.size(); i += 4)
        result.segments.push_back({i / double(sample_rate),
                                   std::min(i + 4, pcm.size()) / double(sample_rate),
                                   "chunk \"" + std::to_string(int(pcm[i])) + "\" \\", 0.1});
    return result;
}
void run(Fixture& f, Journal& journal, Audio& audio, int fail_at) {
    int calls = 0;
    run_chunks(
        journal, 16, [&](size_t n) { return audio.read(n); },
        [&](const auto& pcm) {
            if (calls++ == fail_at)
                throw std::runtime_error("injected inference failure");
            return audio.recognize(pcm);
        },
        [](const auto& pcm) { return pcm.size() - 4; }, {}, 4);
    publish_outputs(f.job, f.options.rendering, journal, f.options.checkpoint.overwrite);
}
} // namespace wt::test
