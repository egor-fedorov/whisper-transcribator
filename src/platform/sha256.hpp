#pragma once
#include <cstddef>
#include <memory>
#include <string>

namespace wt::platform {
// SHA-256 from the system's cryptography library where it has one (CommonCrypto on macOS, CNG on
// Windows), so archives bundle no OpenSSL there, and from OpenSSL elsewhere.
class Sha256 {
  public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;
    void update(const char* data, size_t size);
    // The 32 digest bytes; the object accepts no further data.
    std::string finish();

  private:
    struct State;
    std::unique_ptr<State> state;
};
} // namespace wt::platform
