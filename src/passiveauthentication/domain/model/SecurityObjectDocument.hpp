#pragma once

#include "shared/exception/DomainException.hpp"
#include <vector>
#include <string>
#include <cstdint>
#include <cstdio>

namespace pa::domain::model {

/**
 * Security Object Document (SOD) from ePassport.
 *
 * SOD is a CMS SignedData (PKCS#7) structure containing:
 * - LDSSecurityObject with hashes of all data groups
 * - Signature created with Document Signer Certificate (DSC)
 * - Hash algorithm identifier (SHA-256, SHA-384, SHA-512)
 * - Signature algorithm identifier (SHA256withRSA, etc.)
 *
 * Used for Passive Authentication verification.
 */
class SecurityObjectDocument {
private:
    std::vector<uint8_t> encodedData_;  // PKCS#7 SignedData binary
    std::string hashAlgorithm_;         // SHA-256, SHA-384, SHA-512
    std::string signatureAlgorithm_;    // SHA256withRSA, SHA384withRSA, etc.

    void validate(const std::vector<uint8_t>& sodBytes) {
        if (sodBytes.empty()) {
            throw shared::exception::DomainException(
                "INVALID_SOD",
                "SOD data cannot be null or empty"
            );
        }

        // Valid SOD formats:
        // 1. ICAO 9303 EF.SOD: starts with Tag 0x77 (Application[23])
        // 2. Raw CMS SignedData: starts with Tag 0x30 (SEQUENCE)
        uint8_t firstByte = sodBytes[0];
        if (firstByte != 0x30 && firstByte != 0x77) {
            throw shared::exception::DomainException(
                "INVALID_SOD_FORMAT",
                "SOD data does not appear to be valid (expected tag 0x30 or 0x77, got 0x" +
                    toHex(firstByte) + ")"
            );
        }
    }

    static std::string toHex(uint8_t byte) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02X", byte);
        return std::string(buf);
    }

    explicit SecurityObjectDocument(std::vector<uint8_t> encodedData)
        : encodedData_(std::move(encodedData)) {
        validate(encodedData_);
    }

public:
    SecurityObjectDocument() = default;

    /**
     * Create SecurityObjectDocument from encoded bytes.
     */
    static SecurityObjectDocument of(const std::vector<uint8_t>& sodBytes) {
        return SecurityObjectDocument(sodBytes);
    }

    /**
     * Create SecurityObjectDocument with algorithms.
     */
    static SecurityObjectDocument withAlgorithms(
        const std::vector<uint8_t>& sodBytes,
        const std::string& hashAlgorithm,
        const std::string& signatureAlgorithm
    ) {
        SecurityObjectDocument sod(sodBytes);
        sod.hashAlgorithm_ = hashAlgorithm;
        sod.signatureAlgorithm_ = signatureAlgorithm;
        return sod;
    }

    /**
     * Get encoded SOD data.
     */
    const std::vector<uint8_t>& getEncodedData() const { return encodedData_; }

    /**
     * Get hash algorithm.
     */
    const std::string& getHashAlgorithm() const { return hashAlgorithm_; }

    /**
     * Get signature algorithm.
     */
    const std::string& getSignatureAlgorithm() const { return signatureAlgorithm_; }

    /**
     * Set hash algorithm (extracted from LDSSecurityObject).
     */
    void setHashAlgorithm(const std::string& algorithm) {
        if (algorithm.empty()) {
            throw shared::exception::DomainException(
                "INVALID_HASH_ALGORITHM",
                "Hash algorithm cannot be null or empty"
            );
        }
        hashAlgorithm_ = algorithm;
    }

    /**
     * Set signature algorithm (extracted from SignerInfo).
     */
    void setSignatureAlgorithm(const std::string& algorithm) {
        if (algorithm.empty()) {
            throw shared::exception::DomainException(
                "INVALID_SIGNATURE_ALGORITHM",
                "Signature algorithm cannot be null or empty"
            );
        }
        signatureAlgorithm_ = algorithm;
    }

    /**
     * Get SOD size in bytes.
     */
    size_t calculateSize() const {
        return encodedData_.size();
    }

    bool operator==(const SecurityObjectDocument& other) const {
        return encodedData_ == other.encodedData_;
    }
};

} // namespace pa::domain::model
