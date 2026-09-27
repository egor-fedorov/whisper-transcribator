#include "support/hash.hpp"
#include "support/cancel.hpp"
#include "support/report.hpp"
#include <fstream>
#include <iomanip>
#include <memory>
#include <openssl/evp.h>
#include <sstream>
#include <stdexcept>

namespace wt {
std::string sha256_text(const std::string& text) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int size = 0;
    if (EVP_Digest(text.data(), text.size(), digest, &size, EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("SHA-256 failed");
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < size; ++i)
        output << std::setw(2) << static_cast<int>(digest[i]);
    return output.str();
}
std::string sha256(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw std::runtime_error("Cannot hash: " + path.string());
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("SHA-256 initialization failed");
    char buffer[65536];
    uint64_t hashed = 0;
    while (input) {
        check_cancelled();
        input.read(buffer, sizeof(buffer));
        hashed += static_cast<uint64_t>(input.gcount());
        report_progress("Verifying SHA-256", path.filename().string() + " | " +
                                                 std::to_string(hashed / 1024 / 1024) + " MiB");
        if (EVP_DigestUpdate(ctx.get(), buffer, static_cast<size_t>(input.gcount())) != 1)
            throw std::runtime_error("SHA-256 update failed");
    }
    if (!input.eof())
        throw std::runtime_error("Hash read failed");
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int size = 0;
    if (EVP_DigestFinal_ex(ctx.get(), digest, &size) != 1)
        throw std::runtime_error("SHA-256 failed");
    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < size; ++i)
        output << std::setw(2) << static_cast<int>(digest[i]);
    return output.str();
}
} // namespace wt
