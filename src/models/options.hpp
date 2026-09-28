#pragma once
#include <string>

namespace wt {
struct ModelCacheOptions {
    std::string download_root;
    bool local_files_only = false;
};
} // namespace wt
