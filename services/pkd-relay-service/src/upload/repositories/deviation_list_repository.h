#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "i_query_executor.h"
#include <dl_parser.h>

/**
 * @file deviation_list_repository.h
 * @brief Deviation List Repository - Database Access Layer for deviation_list and deviation_entry tables
 *
 * Handles all DB operations for ICAO Deviation Lists (Doc 9303 Part 12).
 * Database-agnostic interface using IQueryExecutor (supports PostgreSQL and Oracle).
 *
 * @date 2026-02-11
 */

namespace repositories {

class DeviationListRepository {
public:
    explicit DeviationListRepository(common::IQueryExecutor* queryExecutor);
    ~DeviationListRepository() = default;

    /**
     * @brief Save Deviation List metadata to database
     * @return DL ID or empty string on failure
     */
    std::string save(const std::string& uploadId,
                     const std::string& issuerCountry,
                     int version,
                     const std::string& hashAlgorithm,
                     const std::string& signingTime,
                     const std::vector<uint8_t>& dlBinary,
                     const std::string& fingerprint,
                     const std::string& signerDn,
                     const std::string& signerCertificateId,
                     bool signatureValid,
                     int deviationCount);

    /**
     * @brief Save individual deviation entry to database
     * @return Entry ID or empty string on failure
     */
    std::string saveDeviationEntry(const std::string& deviationListId,
                                    const icao::certificate_parser::DeviationEntry& entry,
                                    const std::string& matchedCertificateId = "");

    /**
     * @brief Find deviation list by ID
     */
    Json::Value findById(const std::string& dlId);

    /**
     * @brief Find deviation lists by upload ID
     */
    Json::Value findByUploadId(const std::string& uploadId);

    /**
     * @brief Find deviations affecting a specific certificate (by issuer DN + serial number)
     */
    Json::Value findDeviationByCertificate(const std::string& issuerDn,
                                            const std::string& serialNumber);

private:
    common::IQueryExecutor* queryExecutor_;

    std::string generateUuid();
};

} // namespace repositories
