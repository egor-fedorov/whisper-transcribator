#include "support/hash.hpp"
#include "support/cancel.hpp"
#include "support/report.hpp"
#include <algorithm>
#include <fstream>
#include <iomanip>
#include <memory>
#include <sstream>
#include <stdexcept>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/evp.h>
#endif

namespace wt {
namespace {
// macOS uses CommonCrypto from its system library, so archives need no OpenSSL.
class Sha256 {
#ifdef __APPLE__
    CC_SHA256_CTX context{};

  public:
    Sha256() { CC_SHA256_Init(&context); }
    void update(const char* data, size_t size) {
        while (size) {
            auto chunk = std::min<size_t>(size, 1U << 30);
            CC_SHA256_Update(&context, data, static_cast<CC_LONG>(chunk));
            data += chunk;
            size -= chunk;
        }
    }
    std::string hex() {
        unsigned char digest[CC_SHA256_DIGEST_LENGTH];
        CC_SHA256_Final(digest, &context);
        return encode(digest, sizeof(digest));
    }
#else
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context{EVP_MD_CTX_new(),
                                                                    EVP_MD_CTX_free};

  public:
    Sha256() {
        if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1)
            throw std::runtime_error("SHA-256 initialization failed");
    }
    void update(const char* data, size_t size) {
        if (EVP_DigestUpdate(context.get(), data, size) != 1)
            throw std::runtime_error("SHA-256 update failed");
    }
    std::string hex() {
        unsigned char digest[EVP_MAX_MD_SIZE];
        unsigned int size = 0;
        if (EVP_DigestFinal_ex(context.get(), digest, &size) != 1)
            throw std::runtime_error("SHA-256 failed");
        return encode(digest, size);
    }
#endif
  private:
    static std::string encode(const unsigned char* digest, size_t size) {
        std::ostringstream output;
        output << std::hex << std::setfill('0');
        for (size_t i = 0; i < size; ++i)
            output << std::setw(2) << static_cast<int>(digest[i]);
        return output.str();
    }
};
} // namespace
std::string sha256_text(const std::string& text) {
    Sha256 digest;
    digest.update(text.data(), text.size());
    return digest.hex();
}
std::string sha256(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot hash: " + path.string());
    Sha256 digest;
    char buffer[65536];
    uint64_t hashed = 0;
    while (input) {
        check_cancelled();
        input.read(buffer, sizeof(buffer));
        hashed += static_cast<uint64_t>(input.gcount());
        report_progress("Verifying SHA-256", path.filename().string() + " | " +
                                                 std::to_string(hashed / 1024 / 1024) + " MiB");
        digest.update(buffer, static_cast<size_t>(input.gcount()));
    }
    if (!input.eof())
        throw std::runtime_error("Hash read failed");
    return digest.hex();
}
} // namespace wt
