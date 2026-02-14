/**
 * @file sod_parser.h
 * @brief ICAO 9303 SOD (Security Object Document) Parser
 *
 * Thread-safe SOD parsing for ICAO 9303 compliant electronic passports.
 * Handles SOD parsing, DSC extraction, and data group hash extraction.
 *
 * @author SmartCore Inc.
 * @date 2026-02-04
 */

#pragma once

#include <string>
#include <vector>
#include <map>
#include <json/json.h>
#include <openssl/x509.h>
#include <openssl/cms.h>
#include "models/sod_data.h"

namespace icao {

/**
 * @brief SOD Parser for ICAO 9303
 *
 * Responsibilities:
 * - Parse SOD from binary data
 * - Extract DSC certificate from SOD
 * - Extract data group hashes
 * - Verify SOD signature
 * - Extract algorithm information
 */
class SodParser {
public:
    /**
     * @brief Constructor
     */
    SodParser();

    /**
     * @brief Destructor
     */
    ~SodParser() = default;

    /// @name Main SOD Parsing Operations

    /**
     * @brief Parse SOD from binary data
     * @param sodBytes SOD data bytes
     * @return SodData domain model with all parsed information
     * @throws std::runtime_error on parsing error
     */
    models::SodData parseSod(const std::vector<uint8_t>& sodBytes);

    /**
     * @brief Extract DSC certificate from SOD
     * @param sodBytes SOD data bytes
     * @return X509* certificate or nullptr on error (caller must X509_free)
     */
    X509* extractDscCertificate(const std::vector<uint8_t>& sodBytes);

    /**
     * @brief Extract data group hashes from SOD
     * @param sodBytes SOD data bytes
     * @return Map of DG number (as string) → hex hash
     */
    std::map<std::string, std::string> extractDataGroupHashes(const std::vector<uint8_t>& sodBytes);

    /**
     * @brief Verify SOD signature using DSC certificate
     * @param sodBytes SOD data bytes
     * @param dscCert DSC certificate extracted from SOD
     * @return true if signature is valid
     */
    bool verifySodSignature(const std::vector<uint8_t>& sodBytes, X509* dscCert);

    /// @name Algorithm Extraction

    /**
     * @brief Extract signature algorithm from SOD
     * @param sodBytes SOD data bytes
     * @return Signature algorithm name (e.g., "SHA256withRSA")
     */
    std::string extractSignatureAlgorithm(const std::vector<uint8_t>& sodBytes);

    /**
     * @brief Extract hash algorithm from SOD
     * @param sodBytes SOD data bytes
     * @return Hash algorithm name (e.g., "SHA-256")
     */
    std::string extractHashAlgorithm(const std::vector<uint8_t>& sodBytes);

    /**
     * @brief Extract signature algorithm OID from SOD
     * @param sodBytes SOD data bytes
     * @return Signature algorithm OID
     */
    std::string extractSignatureAlgorithmOid(const std::vector<uint8_t>& sodBytes);

    /**
     * @brief Extract hash algorithm OID from SOD
     * @param sodBytes SOD data bytes
     * @return Hash algorithm OID
     */
    std::string extractHashAlgorithmOid(const std::vector<uint8_t>& sodBytes);

    /// @name Helper Methods

    /**
     * @brief Unwrap ICAO SOD (remove outer tag if present)
     * @param sodBytes Original SOD bytes
     * @return Unwrapped CMS bytes
     */
    std::vector<uint8_t> unwrapIcaoSod(const std::vector<uint8_t>& sodBytes);

    /**
     * @brief Parse data group hashes from LDSSecurityObject
     * @param sodBytes SOD data bytes
     * @return Map of DG number (int) → hash bytes
     */
    std::map<int, std::vector<uint8_t>> parseDataGroupHashesRaw(const std::vector<uint8_t>& sodBytes);

    /**
     * @brief Convert hash bytes to hex string
     * @param hashBytes Hash bytes
     * @return Hex-encoded string
     */
    std::string hashToHexString(const std::vector<uint8_t>& hashBytes);

    /**
     * @brief Get algorithm name from OID
     * @param oid Algorithm OID
     * @param isHash true for hash algorithm, false for signature algorithm
     * @return Algorithm name or "UNKNOWN"
     */
    std::string getAlgorithmName(const std::string& oid, bool isHash);

    /// @name API-Specific Methods

    /**
     * @brief Parse SOD for API response (includes detailed metadata)
     * @param sodBytes SOD data bytes
     * @return JSON object with all SOD metadata for API response
     */
    Json::Value parseSodForApi(const std::vector<uint8_t>& sodBytes);

private:
    /**
     * @brief Parse CMS ContentInfo from bytes
     * @param cmsBytes CMS data
     * @return CMS_ContentInfo* (caller must CMS_ContentInfo_free)
     */
    CMS_ContentInfo* parseCms(const std::vector<uint8_t>& cmsBytes);

    /**
     * @brief Extract encapsulated content from CMS
     * @param cms CMS ContentInfo
     * @return Encapsulated content bytes
     */
    std::vector<uint8_t> extractEncapsulatedContent(CMS_ContentInfo* cms);

    // Algorithm OID mappings
    static const std::map<std::string, std::string>& getHashAlgorithmNames();
    static const std::map<std::string, std::string>& getSignatureAlgorithmNames();
};

} // namespace icao
