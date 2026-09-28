#pragma once
#include "models/options.hpp"
#include "models/types.hpp"
#include "support/json.hpp"
#include "support/permissions.hpp"
#include <functional>
#include <string>
#include <vector>

namespace wt {
std::vector<Model> model_catalog();
fs::path model_root(const ModelCacheOptions& options);
using Fetch = std::function<void(const Model&, const fs::path&)>;
PreparedModel ensure_cached(const Model& model, const fs::path& root, bool offline);
PreparedModel ensure_cached(const Model& model, const fs::path& root, bool offline,
                            const Fetch& fetch,
                            const PermissionProbe& permissions = probe_permissions);
PreparedModel prepare_model(const std::string& name, const ModelCacheOptions& options);
Json list_models(const ModelCacheOptions& options);
} // namespace wt
