#include "pipeline.hpp"
#include <algorithm>
#include <iostream>
#include <limits>

namespace wt {
size_t pause_cut(size_t samples, const std::vector<std::pair<int64_t, int64_t>>& speech,
                 int minimum_silence_ms) {
    int64_t previous = 0, selected = static_cast<int64_t>(samples);
    auto gap = [&](int64_t end) {
        auto middle = previous + (end - previous) / 2;
        if (end > previous && end - previous >= int64_t(minimum_silence_ms) * 16 &&
            middle >= static_cast<int64_t>(samples * 3 / 4) &&
            middle < static_cast<int64_t>(samples))
            selected = middle;
    };
    for (const auto& [start, end] : speech) {
        if (start < previous || end < start || end > static_cast<int64_t>(samples))
            throw std::runtime_error("Invalid VAD intervals");
        gap(start);
        previous = end;
    }
    gap(static_cast<int64_t>(samples));
    return static_cast<size_t>(selected);
}
void run_chunks(Journal& journal, size_t limit, const ReadAudio& read, const Recognize& recognize,
                const ChooseCut& cut) {
    if (!limit || limit > 600 * sample_rate)
        throw std::runtime_error("Invalid chunk size");
    if (journal.finished())
        return;
    int64_t discard = journal.samples();
    if (discard)
        log_message(LogLevel::info, "Resume: decoding prefix without inference to " +
                                        std::to_string(discard / double(sample_rate)) + "s");
    while (discard) {
        check_cancelled();
        auto count = static_cast<size_t>(std::min<int64_t>(discard, 65536));
        auto part = read(count);
        if (part.empty() || part.size() > count)
            throw std::runtime_error("Audio ends before checkpoint position");
        discard -= static_cast<int64_t>(part.size());
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
        size_t count = eof ? buffer.size() : cut(buffer);
        if (!count || count > buffer.size())
            throw std::runtime_error("Invalid chunk boundary");
        if (journal.samples() > std::numeric_limits<int64_t>::max() - int64_t(count))
            throw std::runtime_error("Audio sample counter overflow");
        std::vector<float> pcm(buffer.begin(), buffer.begin() + count);
        log_message(LogLevel::debug,
                    "Recognizing " + std::to_string(journal.samples() / double(sample_rate)) + "-" +
                        std::to_string((journal.samples() + int64_t(count)) / double(sample_rate)) +
                        "s");
        auto transcript = recognize(pcm, journal.language());
        check_cancelled();
        journal.append(static_cast<int64_t>(count), transcript);
        buffer.erase(buffer.begin(), buffer.begin() + count);
        log_message(LogLevel::debug,
                    "Checkpoint: " + std::to_string(journal.samples() / double(sample_rate)) + "s");
    }
}
} // namespace wt
