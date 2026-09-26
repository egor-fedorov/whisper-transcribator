#pragma once
#include "support/fs.hpp"

namespace wt {
struct Model;
void fetch_https(const Model& model, const fs::path& target);
fs::path partial_model_path(const Model& model, const fs::path& root);
int open_partial_model(const fs::path& path);
} // namespace wt
