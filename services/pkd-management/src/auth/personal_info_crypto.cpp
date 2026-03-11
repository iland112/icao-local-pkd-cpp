/**
 * @file personal_info_crypto.cpp
 * @brief AES-256-GCM encryption/decryption for personal information fields
 *
 * 개인정보보호법 준수를 위한 개인정보 암호화 모듈
 * - AES-256-GCM: 인증된 암호화 (기밀성 + 무결성 동시 보장)
 * - IV (12 bytes): 매 암호화마다 OpenSSL RAND_bytes로 생성
 * - Tag (16 bytes): GCM 인증 태그
 * - 저장 형식: "ENC:" + hex(IV[12] + ciphertext + tag[16])
 */

#include "personal_info_crypto.h"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <spdlog/spdlog.h>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>

namespace auth {
namespace pii {

namespace {

constexpr int AES_KEY_SIZE = 32;   // 256 bits
constexpr int GCM_IV_SIZE = 12;    // 96 bits (NIST recommended)
constexpr int GCM_TAG_SIZE = 16;   // 128 bits
constexpr const char* ENC_PREFIX = "ENC:";
constexpr int ENC_PREFIX_LEN = 4;

// Key storage (loaded once from environment)
static bool s_initialized = false;
static bool s_enabled = false;
static unsigned char s_key[AES_KEY_SIZE] = {};
static std::once_flag s_initFlag;

// Hex encoding/decoding helpers
std::string toHex(const unsigned char* data, size_t len) {
    std::ostringstream oss;
    for (size_t i = 0; i < len; ++i) {
        oss << std::hex << std::setfill('0') << std::setw(2)
            << static_cast<int>(data[i]);
    }
    return oss.str();
}

std::vector<unsigned char> fromHex(const std::string& hex) {
    std::vector<unsigned char> result;
    if (hex.size() % 2 != 0) return result;

    result.reserve(hex.size() / 2);
    for (size_t i = 0; i < hex.size(); i += 2) {
        try {
            unsigned char byte = static_cast<unsigned char>(
                std::stoi(hex.substr(i, 2), nullptr, 16));
            result.push_back(byte);
        } catch (...) {
            return {};
        }
    }
    return result;
}

bool loadKeyFromEnv() {
    const char* keyHex = std::getenv("PII_ENCRYPTION_KEY");
    if (!keyHex || strlen(keyHex) == 0) {
        spdlog::warn("[PII Crypto] PII_ENCRYPTION_KEY not set — personal info encryption DISABLED");
        return false;
    }

    std::string keyStr(keyHex);
    if (keyStr.size() != AES_KEY_SIZE * 2) {
        spdlog::error("[PII Crypto] PII_ENCRYPTION_KEY must be {} hex characters (got {})",
                      AES_KEY_SIZE * 2, keyStr.size());
        return false;
    }

    auto keyBytes = fromHex(keyStr);
    if (keyBytes.size() != AES_KEY_SIZE) {
        spdlog::error("[PII Crypto] Invalid hex in PII_ENCRYPTION_KEY");
        return false;
    }

    std::memcpy(s_key, keyBytes.data(), AES_KEY_SIZE);
    spdlog::info("[PII Crypto] AES-256-GCM encryption ENABLED for personal information fields");
    return true;
}

// UTF-8 aware character counting (returns codepoint count)
size_t utf8Length(const std::string& str) {
    size_t count = 0;
    for (size_t i = 0; i < str.size(); ) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        if (c < 0x80)      i += 1;
        else if (c < 0xE0) i += 2;
        else if (c < 0xF0) i += 3;
        else                i += 4;
        ++count;
    }
    return count;
}

// Get single UTF-8 character at codepoint index
std::string utf8CharAt(const std::string& str, size_t cpIndex) {
    size_t count = 0;
    for (size_t i = 0; i < str.size(); ) {
        unsigned char c = static_cast<unsigned char>(str[i]);
        size_t charLen = 1;
        if (c >= 0xF0)      charLen = 4;
        else if (c >= 0xE0) charLen = 3;
        else if (c >= 0xC0) charLen = 2;

        if (count == cpIndex) {
            return str.substr(i, charLen);
        }
        i += charLen;
        ++count;
    }
    return "";
}

} // anonymous namespace

bool initialize() {
    std::call_once(s_initFlag, []() {
        s_enabled = loadKeyFromEnv();
        s_initialized = true;
    });
    return s_enabled;
}

bool isEnabled() {
    if (!s_initialized) initialize();
    return s_enabled;
}

bool isEncrypted(const std::string& value) {
    return value.size() > ENC_PREFIX_LEN &&
           value.substr(0, ENC_PREFIX_LEN) == ENC_PREFIX;
}

std::string encrypt(const std::string& plaintext) {
    if (!isEnabled() || plaintext.empty()) {
        return plaintext;
    }

    // Generate random IV
    unsigned char iv[GCM_IV_SIZE];
    if (RAND_bytes(iv, GCM_IV_SIZE) != 1) {
        spdlog::error("[PII Crypto] RAND_bytes failed for IV generation");
        return plaintext;  // Fail open: return plaintext rather than crash
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        spdlog::error("[PII Crypto] EVP_CIPHER_CTX_new failed");
        return plaintext;
    }

    int ret = 1;
    int outLen = 0;
    int totalLen = 0;

    // Allocate output buffer (plaintext size + block size)
    std::vector<unsigned char> ciphertext(plaintext.size() + EVP_MAX_BLOCK_LENGTH);
    unsigned char tag[GCM_TAG_SIZE];

    // Init encryption
    ret = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    if (ret != 1) goto cleanup;

    // Set IV length
    ret = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, GCM_IV_SIZE, nullptr);
    if (ret != 1) goto cleanup;

    // Set key and IV
    ret = EVP_EncryptInit_ex(ctx, nullptr, nullptr, s_key, iv);
    if (ret != 1) goto cleanup;

    // Encrypt
    ret = EVP_EncryptUpdate(ctx, ciphertext.data(), &outLen,
                            reinterpret_cast<const unsigned char*>(plaintext.data()),
                            static_cast<int>(plaintext.size()));
    if (ret != 1) goto cleanup;
    totalLen = outLen;

    // Finalize
    ret = EVP_EncryptFinal_ex(ctx, ciphertext.data() + totalLen, &outLen);
    if (ret != 1) goto cleanup;
    totalLen += outLen;

    // Get authentication tag
    ret = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, GCM_TAG_SIZE, tag);
    if (ret != 1) goto cleanup;

    {
        EVP_CIPHER_CTX_free(ctx);

        // Build output: ENC: + hex(IV + ciphertext + tag)
        std::string result = ENC_PREFIX;
        result += toHex(iv, GCM_IV_SIZE);
        result += toHex(ciphertext.data(), static_cast<size_t>(totalLen));
        result += toHex(tag, GCM_TAG_SIZE);
        return result;
    }

cleanup:
    EVP_CIPHER_CTX_free(ctx);
    spdlog::error("[PII Crypto] AES-256-GCM encryption failed");
    return plaintext;
}

std::string decrypt(const std::string& ciphertext) {
    if (!isEnabled() || ciphertext.empty() || !isEncrypted(ciphertext)) {
        return ciphertext;  // Not encrypted or encryption disabled — return as-is
    }

    // Parse: ENC: + hex(IV[12] + ciphertext + tag[16])
    std::string hexData = ciphertext.substr(ENC_PREFIX_LEN);
    auto raw = fromHex(hexData);

    // Minimum size: IV(12) + at least 1 byte ciphertext + tag(16) = 29
    if (raw.size() < static_cast<size_t>(GCM_IV_SIZE + 1 + GCM_TAG_SIZE)) {
        spdlog::warn("[PII Crypto] Encrypted data too short ({}B)", raw.size());
        return ciphertext;
    }

    const unsigned char* iv = raw.data();
    size_t ctLen = raw.size() - GCM_IV_SIZE - GCM_TAG_SIZE;
    const unsigned char* ct = raw.data() + GCM_IV_SIZE;
    const unsigned char* tag = raw.data() + GCM_IV_SIZE + ctLen;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        spdlog::error("[PII Crypto] EVP_CIPHER_CTX_new failed");
        return ciphertext;
    }

    int ret = 1;
    int outLen = 0;
    int totalLen = 0;
    std::vector<unsigned char> plaintext(ctLen + EVP_MAX_BLOCK_LENGTH);

    // Init decryption
    ret = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr);
    if (ret != 1) goto cleanup;

    ret = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, GCM_IV_SIZE, nullptr);
    if (ret != 1) goto cleanup;

    ret = EVP_DecryptInit_ex(ctx, nullptr, nullptr, s_key, iv);
    if (ret != 1) goto cleanup;

    // Decrypt
    ret = EVP_DecryptUpdate(ctx, plaintext.data(), &outLen,
                            ct, static_cast<int>(ctLen));
    if (ret != 1) goto cleanup;
    totalLen = outLen;

    // Set expected tag
    ret = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, GCM_TAG_SIZE,
                              const_cast<unsigned char*>(tag));
    if (ret != 1) goto cleanup;

    // Finalize + verify tag
    ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + totalLen, &outLen);
    if (ret != 1) {
        EVP_CIPHER_CTX_free(ctx);
        spdlog::error("[PII Crypto] GCM tag verification failed — data may be tampered");
        return ciphertext;
    }
    totalLen += outLen;

    {
        EVP_CIPHER_CTX_free(ctx);
        return std::string(reinterpret_cast<char*>(plaintext.data()),
                          static_cast<size_t>(totalLen));
    }

cleanup:
    EVP_CIPHER_CTX_free(ctx);
    spdlog::error("[PII Crypto] AES-256-GCM decryption failed");
    return ciphertext;
}

std::string mask(const std::string& value, const std::string& type) {
    if (value.empty()) return value;

    if (type == "email") {
        // h***@example.com
        auto atPos = value.find('@');
        if (atPos == std::string::npos || atPos == 0) return "***";
        return value.substr(0, 1) + "***" + value.substr(atPos);
    }

    if (type == "phone") {
        // 010-****-5678 or last 4 visible
        if (value.size() <= 4) return "****";
        std::string masked;
        for (size_t i = 0; i < value.size() - 4; ++i) {
            char c = value[i];
            masked += (c == '-' || c == ' ' || c == '+') ? c : '*';
        }
        masked += value.substr(value.size() - 4);
        return masked;
    }

    if (type == "name") {
        // 홍*동 (UTF-8 aware: first char + * + last char)
        size_t len = utf8Length(value);
        if (len <= 1) return "*";
        if (len == 2) return utf8CharAt(value, 0) + "*";
        // First char + asterisks + last char
        std::string result = utf8CharAt(value, 0);
        for (size_t i = 1; i < len - 1; ++i) result += "*";
        result += utf8CharAt(value, len - 1);
        return result;
    }

    // Default (org etc.): show first 2 chars + ***
    size_t len = utf8Length(value);
    if (len <= 2) return value;
    return utf8CharAt(value, 0) + utf8CharAt(value, 1) + "***";
}

} // namespace pii
} // namespace auth
