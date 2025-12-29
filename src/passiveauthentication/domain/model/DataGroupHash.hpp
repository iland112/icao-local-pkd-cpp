#pragma once

#include "shared/exception/DomainException.hpp"
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>
#include <openssl/evp.h>
#include <algorithm>
#include <cctype>

namespace pa::domain::model {

/**
 * Hash value of a Data Group.
 *
 * Represents the cryptographic hash (SHA-256, SHA-384, SHA-512) of a data group content.
 * Used for integrity verification in Passive Authentication.
 */
class DataGroupHash {
private:
    std::string value_;  // Hex-encoded hash (lowercase)

    void validate(const std::string& hexValue) {
        if (hexValue.empty()) {
            throw shared::exception::DomainException(
                "INVALID_HASH",
                "Hash value cannot be null or empty"
            );
        }

        // Validate hex format (SHA-256: 64 chars, SHA-384: 96 chars, SHA-512: 128 chars)
        if (hexValue.length() < 64 || hexValue.length() > 128) {
            throw shared::exception::DomainException(
                "INVALID_HASH_FORMAT",
                "Hash must be hex string (64-128 characters). Got: " + std::to_string(hexValue.length())
            );
        }

        for (char c : hexValue) {
            if (!std::isxdigit(static_cast<unsigned char>(c))) {
                throw shared::exception::DomainException(
                    "INVALID_HASH_FORMAT",
                    "Hash contains invalid hex characters"
                );
            }
        }
    }

    explicit DataGroupHash(const std::string& hexValue) {
        validate(hexValue);
        // Convert to lowercase for consistent comparison
        value_ = hexValue;
        std::transform(value_.begin(), value_.end(), value_.begin(),
            [](unsigned char c) { return std::tolower(c); });
    }

public:
    DataGroupHash() = default;

    /**
     * Create DataGroupHash from hex-encoded string.
     */
    static DataGroupHash of(const std::string& hexValue) {
        return DataGroupHash(hexValue);
    }

    /**
     * Create DataGroupHash from byte array.
     */
    static DataGroupHash of(const std::vector<uint8_t>& hashBytes) {
        if (hashBytes.empty()) {
            throw shared::exception::DomainException(
                "INVALID_HASH",
                "Hash bytes cannot be null or empty"
            );
        }

        std::ostringstream oss;
        for (uint8_t byte : hashBytes) {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
        }
        return DataGroupHash(oss.str());
    }

    /**
     * Calculate hash from data group content.
     *
     * @param content data group content bytes
     * @param algorithm hash algorithm (SHA-256, SHA-384, SHA-512)
     * @return calculated DataGroupHash
     */
    static DataGroupHash calculate(const std::vector<uint8_t>& content, const std::string& algorithm) {
        if (content.empty()) {
            throw shared::exception::DomainException(
                "INVALID_CONTENT",
                "Content cannot be null or empty"
            );
        }

        const EVP_MD* md = nullptr;
        if (algorithm == "SHA-256") {
            md = EVP_sha256();
        } else if (algorithm == "SHA-384") {
            md = EVP_sha384();
        } else if (algorithm == "SHA-512") {
            md = EVP_sha512();
        } else if (algorithm == "SHA-1") {
            md = EVP_sha1();  // Deprecated but still used in some old passports
        } else {
            throw shared::exception::DomainException(
                "UNSUPPORTED_ALGORITHM",
                "Hash algorithm not supported: " + algorithm
            );
        }

        std::vector<uint8_t> hash(EVP_MD_size(md));
        unsigned int hashLen = 0;

        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        if (!ctx) {
            throw shared::exception::DomainException(
                "HASH_ERROR",
                "Failed to create EVP_MD_CTX"
            );
        }

        if (EVP_DigestInit_ex(ctx, md, nullptr) != 1 ||
            EVP_DigestUpdate(ctx, content.data(), content.size()) != 1 ||
            EVP_DigestFinal_ex(ctx, hash.data(), &hashLen) != 1) {
            EVP_MD_CTX_free(ctx);
            throw shared::exception::DomainException(
                "HASH_ERROR",
                "Failed to calculate hash"
            );
        }

        EVP_MD_CTX_free(ctx);
        hash.resize(hashLen);

        return of(hash);
    }

    /**
     * Get hex-encoded hash value.
     */
    const std::string& getValue() const { return value_; }

    /**
     * Get raw hash bytes.
     */
    std::vector<uint8_t> getBytes() const {
        std::vector<uint8_t> bytes;
        bytes.reserve(value_.length() / 2);

        for (size_t i = 0; i < value_.length(); i += 2) {
            uint8_t byte = static_cast<uint8_t>(
                std::stoi(value_.substr(i, 2), nullptr, 16)
            );
            bytes.push_back(byte);
        }

        return bytes;
    }

    bool operator==(const DataGroupHash& other) const {
        return value_ == other.value_;
    }

    bool operator!=(const DataGroupHash& other) const {
        return !(*this == other);
    }
};

} // namespace pa::domain::model
