#pragma once
#include <stdexcept>

namespace wt {
struct UsageError : std::runtime_error {
    using std::runtime_error::runtime_error;
};
} // namespace wt
