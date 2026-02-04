/**
 * @file sod_data.h
 * @brief Domain model for SOD (Security Object Document) parsing result
 *
 * Represents parsed SOD data from CMS SignedData structure.
 * Used for passing SOD information between Service layers.
 *
 * @author SmartCore Inc.
 * @date 2026-02-04
 */

#pragma once

#include <string>
#include <map>
#include <vector>
#include <optional>
#include <json/json.h>
#include <openssl/x509.h>

namespace icao {
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
    // Key: "1", "2", ..., "15" (number only, to match frontend)
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
    SodData(const SodData& other)
        : signatureAlgorithm(other.signatureAlgorithm),
          signatureAlgorithmOid(other.signatureAlgorithmOid),
          hashAlgorithm(other.hashAlgorithm),
          hashAlgorithmOid(other.hashAlgorithmOid),
          dscCertificate(other.dscCertificate ? X509_dup(other.dscCertificate) : nullptr),
          dataGroupHashes(other.dataGroupHashes),
          signedAttributes(other.signedAttributes),
          ldsSecurityObjectVersion(other.ldsSecurityObjectVersion),
          ldsSecurityObjectOid(other.ldsSecurityObjectOid),
          rawSodData(other.rawSodData),
          parsingErrors(other.parsingErrors),
          parsingSuccess(other.parsingSuccess)
    {
    }

    /**
     * @brief Copy assignment (deep copy of X509 certificate)
     */
    SodData& operator=(const SodData& other) {
        if (this != &other) {
            if (dscCertificate) {
                X509_free(dscCertificate);
            }
            signatureAlgorithm = other.signatureAlgorithm;
            signatureAlgorithmOid = other.signatureAlgorithmOid;
            hashAlgorithm = other.hashAlgorithm;
            hashAlgorithmOid = other.hashAlgorithmOid;
            dscCertificate = other.dscCertificate ? X509_dup(other.dscCertificate) : nullptr;
            dataGroupHashes = other.dataGroupHashes;
            signedAttributes = other.signedAttributes;
            ldsSecurityObjectVersion = other.ldsSecurityObjectVersion;
            ldsSecurityObjectOid = other.ldsSecurityObjectOid;
            rawSodData = other.rawSodData;
            parsingErrors = other.parsingErrors;
            parsingSuccess = other.parsingSuccess;
        }
        return *this;
    }

    /**
     * @brief Move constructor
     */
    SodData(SodData&& other) noexcept
        : signatureAlgorithm(std::move(other.signatureAlgorithm)),
          signatureAlgorithmOid(std::move(other.signatureAlgorithmOid)),
          hashAlgorithm(std::move(other.hashAlgorithm)),
          hashAlgorithmOid(std::move(other.hashAlgorithmOid)),
          dscCertificate(other.dscCertificate),
          dataGroupHashes(std::move(other.dataGroupHashes)),
          signedAttributes(std::move(other.signedAttributes)),
          ldsSecurityObjectVersion(std::move(other.ldsSecurityObjectVersion)),
          ldsSecurityObjectOid(std::move(other.ldsSecurityObjectOid)),
          rawSodData(std::move(other.rawSodData)),
          parsingErrors(std::move(other.parsingErrors)),
          parsingSuccess(other.parsingSuccess)
    {
        other.dscCertificate = nullptr;
    }

    /**
     * @brief Move assignment
     */
    SodData& operator=(SodData&& other) noexcept {
        if (this != &other) {
            if (dscCertificate) {
                X509_free(dscCertificate);
            }
            signatureAlgorithm = std::move(other.signatureAlgorithm);
            signatureAlgorithmOid = std::move(other.signatureAlgorithmOid);
            hashAlgorithm = std::move(other.hashAlgorithm);
            hashAlgorithmOid = std::move(other.hashAlgorithmOid);
            dscCertificate = other.dscCertificate;
            dataGroupHashes = std::move(other.dataGroupHashes);
            signedAttributes = std::move(other.signedAttributes);
            ldsSecurityObjectVersion = std::move(other.ldsSecurityObjectVersion);
            ldsSecurityObjectOid = std::move(other.ldsSecurityObjectOid);
            rawSodData = std::move(other.rawSodData);
            parsingErrors = std::move(other.parsingErrors);
            parsingSuccess = other.parsingSuccess;
            other.dscCertificate = nullptr;
        }
        return *this;
    }

    /**
     * @brief Default constructor
     */
    SodData() = default;

    /**
     * @brief Convert to JSON for API response
     * @return Json::Value representation (without raw binary data)
     */
    Json::Value toJson() const {
        Json::Value json;
        json["signatureAlgorithm"] = signatureAlgorithm;
        json["signatureAlgorithmOid"] = signatureAlgorithmOid;
        json["hashAlgorithm"] = hashAlgorithm;
        json["hashAlgorithmOid"] = hashAlgorithmOid;
        json["ldsSecurityObjectVersion"] = ldsSecurityObjectVersion;
        json["dataGroupCount"] = static_cast<int>(dataGroupHashes.size());
        json["parsingSuccess"] = parsingSuccess;

        if (parsingErrors) {
            json["parsingErrors"] = *parsingErrors;
        }

        // Data group hashes
        Json::Value dgHashes(Json::objectValue);
        for (const auto& [dgNum, hash] : dataGroupHashes) {
            dgHashes[dgNum] = hash;
        }
        json["dataGroupHashes"] = dgHashes;

        return json;
    }

    /**
     * @brief Get data group count
     * @return Number of data groups in SOD
     */
    size_t getDataGroupCount() const {
        return dataGroupHashes.size();
    }

    /**
     * @brief Check if specific data group exists
     * @param dgNumber Data group number (e.g., "1", "2")
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
} // namespace icao
