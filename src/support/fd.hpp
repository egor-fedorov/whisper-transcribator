#pragma once
#include <unistd.h>
#include <utility>

namespace wt {
class UniqueFd {
    int descriptor = -1;

  public:
    explicit UniqueFd(int value = -1) noexcept : descriptor(value) {}
    ~UniqueFd() { close(); }
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;
    UniqueFd(UniqueFd&& other) noexcept : descriptor(other.release()) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            close();
            descriptor = other.release();
        }
        return *this;
    }
    int get() const noexcept { return descriptor; }
    int release() noexcept { return std::exchange(descriptor, -1); }
    // Never retry close: the descriptor may already have been released by the OS.
    int close() noexcept { return descriptor < 0 ? 0 : ::close(release()); }
};
} // namespace wt
