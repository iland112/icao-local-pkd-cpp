/**
 * @file X509Data.hpp
 * @brief Value Object for X.509 certificate binary data
 */

#pragma once

#include "shared/domain/ValueObject.hpp"
#include <vector>
#include <string>
#include <stdexcept>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <sstream>
#include <iomanip>

namespace certificatevalidation::domain::model {

/**
 * @brief X.509 certificate binary data Value Object
 *
 * Contains:
 * - DER-encoded certificate binary
 * - Serial number
 * - SHA-256 fingerprint
 */
class X509Data : public shared::domain::ValueObject {
private:
    std::vector<uint8_t> certificateBinary_;
    std::string serialNumber_;
    std::string fingerprintSha256_;

    X509Data(
        std::vector<uint8_t> certificateBinary,
        std::string serialNumber,
        std::string fingerprintSha256
    ) : certificateBinary_(std::move(certificateBinary)),
        serialNumber_(std::move(serialNumber)),
        fingerprintSha256_(std::move(fingerprintSha256)) {
        validate();
    }

    void validate() const {
        if (certificateBinary_.empty()) {
            throw std::invalid_argument("Certificate binary cannot be empty");
        }
        if (serialNumber_.empty()) {
            throw std::invalid_argument("Serial number cannot be empty");
        }
    }

    static std::string computeSha256(const std::vector<uint8_t>& data) {
        unsigned char hash[SHA256_DIGEST_LENGTH];
        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
        EVP_DigestUpdate(ctx, data.data(), data.size());
        EVP_DigestFinal_ex(ctx, hash, nullptr);
        EVP_MD_CTX_free(ctx);

        std::stringstream ss;
        for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        return ss.str();
    }

public:
    /**
     * @brief Create X509Data from binary and serial number
     * @param certificateBinary DER-encoded certificate
     * @param serialNumber Certificate serial number (hex string)
     */
    static X509Data of(
        std::vector<uint8_t> certificateBinary,
        const std::string& serialNumber
    ) {
        std::string fingerprint = computeSha256(certificateBinary);
        return X509Data(std::move(certificateBinary), serialNumber, fingerprint);
    }

    /**
     * @brief Create X509Data with pre-computed fingerprint
     */
    static X509Data of(
        std::vector<uint8_t> certificateBinary,
        const std::string& serialNumber,
        const std::string& fingerprintSha256
    ) {
        return X509Data(std::move(certificateBinary), serialNumber, fingerprintSha256);
    }

    // Getters
    [[nodiscard]] const std::vector<uint8_t>& getCertificateBinary() const noexcept {
        return certificateBinary_;
    }

    [[nodiscard]] const std::string& getSerialNumber() const noexcept {
        return serialNumber_;
    }

    [[nodiscard]] const std::string& getFingerprintSha256() const noexcept {
        return fingerprintSha256_;
    }

    [[nodiscard]] size_t getSize() const noexcept {
        return certificateBinary_.size();
    }

    [[nodiscard]] bool isComplete() const noexcept {
        return !certificateBinary_.empty() &&
               !serialNumber_.empty() &&
               !fingerprintSha256_.empty();
    }

    bool operator==(const X509Data& other) const noexcept {
        return fingerprintSha256_ == other.fingerprintSha256_;
    }
};

} // namespace certificatevalidation::domain::model
