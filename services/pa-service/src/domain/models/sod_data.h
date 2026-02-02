/**
 * @file sod_data.h
 * @brief Domain model for SOD (Security Object Document) parsing result
 *
 * Represents parsed SOD data from CMS SignedData structure.
 * Used for passing SOD information between Service layers.
 *
 * @author SmartCore Inc.
 * @date 2026-02-01
 */

#pragma once

#include <string>
#include <map>
#include <vector>
#include <optional>
#include <json/json.h>
#include <openssl/x509.h>

namespace domain {
namespace models {

/**
 * @brief SOD (Security Object Document) data model
 *
 * Contains parsed information from ICAO 9303 SOD including:
 * - Signature algorithm
 * - Hash algorithm
 * - Data group hashes
 * - DSC certificate
 * - LDS Security Object version
 */
struct SodData {
    // Algorithms
    std::string signatureAlgorithm;  // OID or name (e.g., "SHA256withRSA")
    std::string signatureAlgorithmOid;
    std::string hashAlgorithm;  // OID or name (e.g., "SHA-256")
    std::string hashAlgorithmOid;

    // DSC certificate (extracted from SOD)
    X509* dscCertificate = nullptr;  // Ownership: caller must X509_free()

    // Data group hashes (DG number → hex hash)
    // Key: "DG1", "DG2", ..., "DG15"
    // Value: hex-encoded hash
    std::map<std::string, std::string> dataGroupHashes;

    // Signed attributes (optional)
    std::map<std::string, std::string> signedAttributes;

    // LDS Security Object information
    std::string ldsSecurityObjectVersion;  // e.g., "V0" (0), "V1" (1)
    std::optional<std::string> ldsSecurityObjectOid;

    // Raw SOD data (optional, for debugging)
    std::optional<std::vector<uint8_t>> rawSodData;

    // Parsing metadata
    std::optional<std::string> parsingErrors;
    bool parsingSuccess = false;

    /**
     * @brief Destructor - frees X509 certificate if allocated
     */
    ~SodData() {
        if (dscCertificate) {
            X509_free(dscCertificate);
            dscCertificate = nullptr;
        }
    }

    /**
     * @brief Copy constructor (deep copy of X509 certificate)
     */
    SodData(const SodData& other);

    /**
     * @brief Copy assignment (deep copy of X509 certificate)
     */
    SodData& operator=(const SodData& other);

    /**
     * @brief Move constructor
     */
    SodData(SodData&& other) noexcept;

    /**
     * @brief Move assignment
     */
    SodData& operator=(SodData&& other) noexcept;

    /**
     * @brief Default constructor
     */
    SodData() = default;

    /**
     * @brief Convert to JSON for API response
     * @return Json::Value representation (without raw binary data)
     */
    Json::Value toJson() const;

    /**
     * @brief Get data group count
     * @return Number of data groups in SOD
     */
    size_t getDataGroupCount() const {
        return dataGroupHashes.size();
    }

    /**
     * @brief Check if specific data group exists
     * @param dgNumber Data group number (e.g., "DG1", "DG2")
     * @return true if data group hash exists in SOD
     */
    bool hasDataGroup(const std::string& dgNumber) const {
        return dataGroupHashes.find(dgNumber) != dataGroupHashes.end();
    }

    /**
     * @brief Get hash for specific data group
     * @param dgNumber Data group number
     * @return Hex-encoded hash or empty string if not found
     */
    std::string getDataGroupHash(const std::string& dgNumber) const {
        auto it = dataGroupHashes.find(dgNumber);
        return (it != dataGroupHashes.end()) ? it->second : "";
    }
};

} // namespace models
} // namespace domain
