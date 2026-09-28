#pragma once
#include "support/json.hpp"
#include "support/permissions.hpp"
#include "transcript/options.hpp"
#include "transcript/types.hpp"
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>

namespace wt {
namespace checkpoint_detail {
class Privacy;
}
fs::path checkpoint_path(const Job& job);
bool has_checkpoint(const Job& job);
using RenderOutput = std::function<void(std::ostream&, const std::string&)>;
class Journal {
    struct Lock;
    std::unique_ptr<checkpoint_detail::Privacy> privacy;
    std::unique_ptr<Lock> lock;
    fs::path directory;
    Json state;
    bool persisted = false;
    void save();
    fs::path chunk_path(int64_t index) const;

  public:
    Journal(const Job& job, const CheckpointOptions& options, const Json& fingerprint,
            const Json& output_metadata, PermissionProbe permissions = probe_permissions);
    ~Journal();
    int64_t samples() const;
    std::string language() const;
    std::vector<std::string> languages() const;
    bool finished() const;
    const Json& output_metadata() const;
    void append(int64_t count, const Transcript& transcript);
    void finish();
    void visit(const std::function<void(const Segment&)>& consumer) const;
    void publish(const Job& job, bool overwrite, const RenderOutput& render);
};
} // namespace wt
