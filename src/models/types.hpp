#pragma once
#include "support/fs.hpp"
#include <cstdint>
#include <string>

namespace wt {
struct Model {
    std::string name, file, url, hash;
    uint64_t bytes;
};
struct PreparedModel {
    fs::path path;
    std::string hash;
};
} // namespace wt
