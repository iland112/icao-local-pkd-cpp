/**
 * @file api_key_generator.cpp
 * @brief API Key generation using OpenSSL RAND_bytes + SHA-256 hashing
 */

#include "api_key_generator.h"
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <spdlog/spdlog.h>
#include <sstream>
#include <iomanip>
#include <stdexcept>

namespace auth {

namespace {

// Base62 characters for human-readable key generation
const char BASE62[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
constexpr int BASE62_LEN = 62;

std::string randomBase62(int length) {
    std::vector<unsigned char> buf(length);
    if (RAND_bytes(buf.data(), length) != 1) {
        throw std::runtime_error("RAND_bytes failed");
    }

    std::string result;
    result.reserve(length);
    for (int i = 0; i < length; ++i) {
        result += BASE62[buf[i] % BASE62_LEN];
    }
    return result;
}

std::string sha256Hex(const std::string& input) {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) throw std::runtime_error("EVP_MD_CTX_new failed");

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, input.data(), input.size()) != 1 ||
        EVP_DigestFinal_ex(ctx, hash, &hashLen) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("SHA-256 digest failed");
    }
    EVP_MD_CTX_free(ctx);

    std::ostringstream oss;
    for (unsigned int i = 0; i < hashLen; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hash[i]);
    }
    return oss.str();
}

} // anonymous namespace

ApiKeyInfo generateApiKey() {
    // Format: icao_{prefix8}_{random32}
    std::string prefix8 = randomBase62(8);
    std::string random32 = randomBase62(32);
    std::string key = "icao_" + prefix8 + "_" + random32;
    std::string displayPrefix = "icao_" + prefix8;

    ApiKeyInfo info;
    info.key = key;
    info.hash = sha256Hex(key);
    info.prefix = displayPrefix;

    spdlog::debug("[ApiKeyGenerator] Generated key with prefix: {}", displayPrefix);
    return info;
}

std::string hashApiKey(const std::string& apiKey) {
    return sha256Hex(apiKey);
}

} // namespace auth
