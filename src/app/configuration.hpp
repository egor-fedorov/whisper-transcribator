#pragma once
#include "app/options.hpp"
#include "transcript/metadata.hpp"

namespace wt {
inline Json describe_run(const CliOptions& options) {
    return run_metadata(options.inference, options.audio, options.chunking, options.rendering,
                        options.device);
}
inline Json describe_output(const CliOptions& options) {
    return {{"model", options.model}, {"run", describe_run(options)}};
}
} // namespace wt
