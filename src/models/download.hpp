#pragma once
#include "models/types.hpp"
#include "platform/file.hpp"
#include "support/fs.hpp"
#include "support/permissions.hpp"

namespace wt {
void fetch_https(const Model& model, const fs::path& target);
fs::path partial_model_path(const Model& model, const fs::path& root);
platform::File open_partial_model(const fs::path& path,
                                  const PermissionProbe& permissions = probe_permissions);
} // namespace wt
