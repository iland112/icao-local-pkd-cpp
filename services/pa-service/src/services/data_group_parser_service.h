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
#include "../domain/models/data_group.h"

namespace services {

class DataGroupParserService {
public:
    DataGroupParserService();
    ~DataGroupParserService() = default;

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
};

} // namespace services
