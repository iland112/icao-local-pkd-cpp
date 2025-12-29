/**
 * @file FileHash.hpp
 * @brief Value Object for file hash (SHA-256)
 */

#pragma once

#include "shared/domain/ValueObject.hpp"
#include "shared/exception/DomainException.hpp"
#include <string>
#include <regex>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>
#include <vector>
#include <memory>

namespace fileupload::domain::model {

/**
 * @brief File Hash Value Object (SHA-256)
 */
class FileHash : public shared::domain::StringValueObject {
private:
    static constexpr size_t SHA256_HEX_LENGTH = 64;

    explicit FileHash(std::string value) : StringValueObject(std::move(value)) {
        validate();
    }

    void validate() const override {
        static const std::regex hexRegex("^[0-9a-f]{64}$", std::regex::icase);

        if (!std::regex_match(value_, hexRegex)) {
            throw shared::exception::DomainException(
                "INVALID_FILE_HASH",
                "File hash must be a 64-character hexadecimal SHA-256 hash"
            );
        }
    }

public:
    /**
     * @brief Create from existing hash string
     */
    static FileHash of(const std::string& value) {
        // Normalize to lowercase
        std::string normalized = value;
        for (char& c : normalized) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return FileHash(normalized);
    }

    /**
     * @brief Compute SHA-256 hash from binary data
     */
    static FileHash compute(const std::vector<uint8_t>& data) {
        return compute(data.data(), data.size());
    }

    /**
     * @brief Compute SHA-256 hash from raw bytes
     */
    static FileHash compute(const uint8_t* data, size_t length) {
        unsigned char hash[SHA256_DIGEST_LENGTH];

        // Use EVP API for OpenSSL 3.x compatibility
        std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(
            EVP_MD_CTX_new(), EVP_MD_CTX_free
        );

        if (!ctx) {
            throw shared::exception::DomainException(
                "HASH_COMPUTATION_ERROR",
                "Failed to create EVP_MD_CTX"
            );
        }

        if (EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1 ||
            EVP_DigestUpdate(ctx.get(), data, length) != 1 ||
            EVP_DigestFinal_ex(ctx.get(), hash, nullptr) != 1) {
            throw shared::exception::DomainException(
                "HASH_COMPUTATION_ERROR",
                "Failed to compute SHA-256 hash"
            );
        }

        // Convert to hex string
        std::ostringstream ss;
        ss << std::hex << std::setfill('0');
        for (unsigned char byte : hash) {
            ss << std::setw(2) << static_cast<int>(byte);
        }

        return FileHash(ss.str());
    }

    /**
     * @brief Compute SHA-256 hash from string content
     */
    static FileHash computeFromString(const std::string& content) {
        return compute(reinterpret_cast<const uint8_t*>(content.data()), content.size());
    }

    /**
     * @brief Get string representation
     */
    [[nodiscard]] std::string toString() const {
        return value_;
    }
};

} // namespace fileupload::domain::model
