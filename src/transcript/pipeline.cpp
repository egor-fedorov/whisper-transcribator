#include "transcript/pipeline.hpp"
#include "support/cancel.hpp"
#include "support/report.hpp"
#include "transcript/boundaries.hpp"
#include "transcript/journal.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace wt {
void run_chunks(Journal& journal, size_t limit, const ReadAudio& read, const Recognize& recognize,
                const ChooseCut& cut, const WindowProgress& progress, size_t guard_samples) {
    if (!limit || limit > 600 * sample_rate)
        throw std::runtime_error("Invalid chunk size");
    if (journal.finished())
        return;
    int64_t discard = journal.samples();
    bool warned = false;
    if (discard)
        log_message(LogLevel::info, "Resume: decoding prefix without inference to " +
                                        format_seconds(discard / double(sample_rate)) + "s");
    while (discard) {
        check_cancelled();
        auto count = static_cast<size_t>(std::min<int64_t>(discard, 65536));
        auto part = read(count);
        if (part.empty() || part.size() > count)
            throw std::runtime_error("Audio ends before checkpoint position");
        discard -= static_cast<int64_t>(part.size());
        report_progress("Resume decoding",
                        format_seconds((journal.samples() - discard) / double(sample_rate)) +
                            " / " + format_seconds(journal.samples() / double(sample_rate)) + "s");
    }
    std::vector<float> buffer;
    buffer.reserve(limit);
    bool eof = false;
    while (true) {
        check_cancelled();
        while (!eof && buffer.size() < limit) {
            auto count = std::min<size_t>(65536, limit - buffer.size());
            auto part = read(count);
            if (part.size() > count)
                throw std::runtime_error("Audio reader exceeded requested size");
            eof = part.empty();
            buffer.insert(buffer.end(), part.begin(), part.end());
        }
        if (buffer.empty()) {
            if (!journal.samples())
                throw std::runtime_error("empty audio stream");
            journal.finish();
            return;
        }
        if (journal.samples() > std::numeric_limits<int64_t>::max() - int64_t(buffer.size()))
            throw std::runtime_error("Audio sample counter overflow");
        bool silent = std::all_of(buffer.begin(), buffer.end(), [](float x) { return x == 0; });
        Transcript transcript{"", buffer.size() / double(sample_rate), {}};
        size_t count = buffer.size();
        if (silent)
            log_message(LogLevel::debug,
                        "Skipping digital silence: " + format_seconds(transcript.duration) + "s");
        else {
            if (progress.recognizing)
                progress.recognizing(journal.samples(), buffer.size());
            log_message(LogLevel::debug,
                        "Recognizing " + format_seconds(journal.samples() / double(sample_rate)) +
                            "-" +
                            format_seconds((journal.samples() + int64_t(buffer.size())) /
                                           double(sample_rate)) +
                            "s");
            transcript = recognize(buffer);
            check_cancelled();
            count = eof ? buffer.size()
                        : committed_cut(buffer.size(), cut(buffer), transcript, guard_samples);
        }
        check_cancelled();
        if (!eof && count == buffer.size() && !transcript.segments.empty() && !warned) {
            log_message(LogLevel::warning, "No safe segment boundary; committing a full window. "
                                           "Boundary words may be less accurate.");
            warned = true;
        }
        transcript.segments.erase(
            std::remove_if(
                transcript.segments.begin(), transcript.segments.end(),
                [&](const auto& segment) { return segment.end > count / double(sample_rate); }),
            transcript.segments.end());
        transcript.duration = count / double(sample_rate);
        journal.append(static_cast<int64_t>(count), transcript);
        buffer.erase(buffer.begin(), buffer.begin() + count);
        log_message(LogLevel::debug,
                    "Checkpoint: " + format_seconds(journal.samples() / double(sample_rate)) + "s");
        if (progress.committed)
            progress.committed(journal.samples());
    }
}
} // namespace wt
