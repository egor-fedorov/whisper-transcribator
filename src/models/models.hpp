#pragma once
#include "models/download.hpp"
#include "support/json.hpp"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace wt {
struct Options;
struct Model {
    std::string name, file, url, hash;
    uint64_t bytes;
};
std::vector<Model> model_catalog();
fs::path model_root(const Options& options);
using Fetch = std::function<void(const Model&, const fs::path&)>;
struct PreparedModel {
    fs::path path;
    std::string hash;
};
PreparedModel ensure_cached(const Model& model, const fs::path& root, bool offline,
                            const Fetch& fetch = fetch_https);
PreparedModel prepare_model(const std::string& name, const Options& options);
Json list_models(const Options& options);
} // namespace wt
