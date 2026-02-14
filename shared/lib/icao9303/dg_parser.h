/**
 * @file data_group_parser_service.h
 * @brief Service for Data Group parsing and verification
 *
 * @author SmartCore Inc.
 * @date 2026-02-01
 */

#pragma once

#include <string>
#include <vector>
#include <json/json.h>
#include "models/data_group.h"

namespace icao {

/**
 * @brief Data Group parser for ICAO 9303 compliant documents
 *
 * Handles DG1 (MRZ), DG2 (face image), hash verification, and hash computation.
 */
class DgParser {
public:
    DgParser();
    ~DgParser() = default;

    /**
     * @brief Parse DG1 data group (Machine Readable Zone)
     * @param dg1Data Raw DG1 binary data
     * @return JSON with parsed MRZ fields
     */
    Json::Value parseDg1(const std::vector<uint8_t>& dg1Data);

    /**
     * @brief Parse MRZ text directly (without DG1 ASN.1 wrapper)
     * @param mrzText Raw MRZ text string
     * @return JSON with parsed MRZ fields
     */
    Json::Value parseMrzText(const std::string& mrzText);

    /**
     * @brief Parse DG2 data group (face image extraction)
     * @param dg2Data Raw DG2 binary data
     * @return JSON with face image data (base64-encoded)
     */
    Json::Value parseDg2(const std::vector<uint8_t>& dg2Data);

    /**
     * @brief Verify a data group hash against expected value
     * @param dgData Raw data group binary data
     * @param expectedHash Expected hash (hex-encoded)
     * @param hashAlgorithm Hash algorithm name (e.g., "SHA-256")
     * @return true if computed hash matches expected hash
     */
    bool verifyDataGroupHash(
        const std::vector<uint8_t>& dgData,
        const std::string& expectedHash,
        const std::string& hashAlgorithm
    );

    /**
     * @brief Compute hash of binary data
     * @param data Binary data to hash
     * @param algorithm Hash algorithm name (e.g., "SHA-256", "SHA-1")
     * @return Hex-encoded hash string
     */
    std::string computeHash(
        const std::vector<uint8_t>& data,
        const std::string& algorithm
    );

private:
    /** @brief Trim whitespace from both ends of a string */
    std::string trim(const std::string& str);
    /** @brief Convert MRZ date (YYMMDD) to YYYY-MM-DD for birth dates */
    std::string convertMrzDate(const std::string& yymmdd);
    /** @brief Convert MRZ date (YYMMDD) to YYYY-MM-DD for expiry dates */
    std::string convertMrzExpiryDate(const std::string& yymmdd);
    /** @brief Remove trailing '<' filler characters from MRZ field */
    std::string cleanMrzField(const std::string& field);

    /** @brief Parse TD3 format MRZ (2 lines x 44 chars, passport) */
    Json::Value parseMrzTd3(const std::string& mrzData);
    /** @brief Parse TD2 format MRZ (2 lines x 36 chars) */
    Json::Value parseMrzTd2(const std::string& mrzData);
    /** @brief Parse TD1 format MRZ (3 lines x 30 chars, ID card) */
    Json::Value parseMrzTd1(const std::string& mrzData);
};

} // namespace icao
