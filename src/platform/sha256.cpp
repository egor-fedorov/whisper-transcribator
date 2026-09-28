#include "platform/sha256.hpp"
#include <algorithm>
#include <stdexcept>
#if defined(__APPLE__)
#include <CommonCrypto/CommonDigest.h>
#elif defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// This needs the definitions of windows.h.
#include <bcrypt.h>
#else
#include <openssl/evp.h>
#endif

namespace wt::platform {
#if defined(__APPLE__)
struct Sha256::State {
    CC_SHA256_CTX context{};
};
Sha256::Sha256() : state(std::make_unique<State>()) { CC_SHA256_Init(&state->context); }
void Sha256::update(const char* data, size_t size) {
    while (size) {
        auto chunk = std::min<size_t>(size, 1U << 30);
        CC_SHA256_Update(&state->context, data, static_cast<CC_LONG>(chunk));
        data += chunk;
        size -= chunk;
    }
}
std::string Sha256::finish() {
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &state->context);
    return std::string(reinterpret_cast<const char*>(digest), sizeof(digest));
}
#elif defined(_WIN32)
struct Sha256::State {
    BCRYPT_HASH_HANDLE hash = nullptr;
    ~State() {
        if (hash)
            BCryptDestroyHash(hash);
    }
};
Sha256::Sha256() : state(std::make_unique<State>()) {
    // One provider serves every hash for the life of the process; CNG allocates hash objects.
    static const BCRYPT_ALG_HANDLE provider = [] {
        BCRYPT_ALG_HANDLE handle = nullptr;
        return BCRYPT_SUCCESS(
                   BCryptOpenAlgorithmProvider(&handle, BCRYPT_SHA256_ALGORITHM, nullptr, 0))
                   ? handle
                   : nullptr;
    }();
    if (!provider ||
        !BCRYPT_SUCCESS(BCryptCreateHash(provider, &state->hash, nullptr, 0, nullptr, 0, 0)))
        throw std::runtime_error("SHA-256 initialization failed");
}
void Sha256::update(const char* data, size_t size) {
    while (size) {
        auto chunk = std::min<size_t>(size, 1U << 30);
        if (!BCRYPT_SUCCESS(BCryptHashData(state->hash,
                                           reinterpret_cast<PUCHAR>(const_cast<char*>(data)),
                                           static_cast<ULONG>(chunk), 0)))
            throw std::runtime_error("SHA-256 update failed");
        data += chunk;
        size -= chunk;
    }
}
std::string Sha256::finish() {
    std::string digest(32, '\0');
    if (!BCRYPT_SUCCESS(BCryptFinishHash(state->hash, reinterpret_cast<PUCHAR>(digest.data()),
                                         static_cast<ULONG>(digest.size()), 0)))
        throw std::runtime_error("SHA-256 failed");
    return digest;
}
#else
struct Sha256::State {
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context{EVP_MD_CTX_new(),
                                                                    EVP_MD_CTX_free};
};
Sha256::Sha256() : state(std::make_unique<State>()) {
    if (!state->context || EVP_DigestInit_ex(state->context.get(), EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("SHA-256 initialization failed");
}
void Sha256::update(const char* data, size_t size) {
    if (EVP_DigestUpdate(state->context.get(), data, size) != 1)
        throw std::runtime_error("SHA-256 update failed");
}
std::string Sha256::finish() {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int size = 0;
    if (EVP_DigestFinal_ex(state->context.get(), digest, &size) != 1)
        throw std::runtime_error("SHA-256 failed");
    return std::string(reinterpret_cast<const char*>(digest), size);
}
#endif
Sha256::~Sha256() = default;
} // namespace wt::platform
