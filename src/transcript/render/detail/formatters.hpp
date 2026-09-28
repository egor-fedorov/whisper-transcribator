#pragma once
#include "transcript/render/render.hpp"

namespace wt::render_detail {
std::string normalized_text(const std::string& value);
bool render_paragraphs(const TranscriptSource& source, const RenderOptions& options,
                       const std::function<void(const std::string&)>& emit);
bool render_text(std::ostream& stream, const RenderOptions& options,
                 const TranscriptSource& source);
bool render_json(std::ostream& stream, const Job& job, const RenderOptions& options,
                 const TranscriptSource& source);
bool render_srt(std::ostream& stream, const TranscriptSource& source);
bool render_vtt(std::ostream& stream, const TranscriptSource& source);
} // namespace wt::render_detail
