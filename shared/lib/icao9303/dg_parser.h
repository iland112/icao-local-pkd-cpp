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

class DgParser {
public:
    DgParser();
    ~DgParser() = default;

    // DG1 parsing
    Json::Value parseDg1(const std::vector<uint8_t>& dg1Data);

    // MRZ text parsing
    Json::Value parseMrzText(const std::string& mrzText);

    // DG2 parsing
    Json::Value parseDg2(const std::vector<uint8_t>& dg2Data);

    // Hash verification
    bool verifyDataGroupHash(
        const std::vector<uint8_t>& dgData,
        const std::string& expectedHash,
        const std::string& hashAlgorithm
    );

    // Hash computation
    std::string computeHash(
        const std::vector<uint8_t>& data,
        const std::string& algorithm
    );

private:
    // Helper functions for MRZ parsing
    std::string trim(const std::string& str);
    std::string convertMrzDate(const std::string& yymmdd);
    std::string convertMrzExpiryDate(const std::string& yymmdd);
    std::string cleanMrzField(const std::string& field);

    // MRZ parsing by format
    Json::Value parseMrzTd3(const std::string& mrzData);  // 2 lines x 44 chars (passport)
    Json::Value parseMrzTd2(const std::string& mrzData);  // 2 lines x 36 chars
    Json::Value parseMrzTd1(const std::string& mrzData);  // 3 lines x 30 chars (ID card)
};

} // namespace icao
