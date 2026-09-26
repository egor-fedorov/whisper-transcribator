#include "support/fixtures.hpp"
#include "support/io.hpp"
#include "support/test.hpp"
#include "transcript/journal.hpp"

using namespace wt;
using namespace wt::test;
namespace {
void bounded_windows_and_byte_identical_resume(const fs::path& root) {
    Fixture f(root / "roundtrip");
    auto fingerprint = f.fingerprint();
    {
        Journal journal(f.job, f.options, fingerprint);
        Audio audio;
        run(f, journal, audio);
        require(audio.recognized == 43 && audio.peak <= 16);
        require(!has_checkpoint(f.job));
    }
    std::map<std::string, std::string> expected;
    for (const auto& [format, path] : f.job.outputs) {
        expected[format] = read_text(path);
        fs::remove(path);
    }
    auto data = Json::parse(expected.at("json"));
    require(data.at("segments").size() == 9 && data.at("duration") == 35 / 16000.0);
    require(data.at("text").get<std::string>() + "\n" == expected.at("text"));
    {
        Journal journal(f.job, f.options, fingerprint);
        Audio audio;
        rejects([&] { run(f, journal, audio, 1); });
        require(journal.samples() == 12);
        rejects([&] { Journal busy(f.job, f.options, fingerprint); });
    }
    rejects([&] { Journal needs_flag(f.job, f.options, fingerprint); });
    f.options.resume = true;
    {
        Journal journal(f.job, f.options, fingerprint);
        Audio audio;
        run(f, journal, audio);
        require(audio.cursor == 35 && audio.recognized == 27);
    }
    for (const auto& [format, path] : f.job.outputs)
        require(read_text(path) == expected.at(format), "resumed output changed");
}
} // namespace
int main() {
    return run_tests({
        {"bounded windows and byte-identical resume", bounded_windows_and_byte_identical_resume},
    });
}
