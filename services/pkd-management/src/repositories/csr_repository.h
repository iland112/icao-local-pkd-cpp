#pragma once

#include <string>
#include <json/json.h>
#include "i_query_executor.h"

/**
 * @file csr_repository.h
 * @brief CSR Repository - Database access layer for csr_request table
 *
 * ICAO PKD CSR (Certificate Signing Request) management.
 * Supports PostgreSQL and Oracle via IQueryExecutor.
 */

namespace repositories {

class CsrRepository {
public:
    explicit CsrRepository(common::IQueryExecutor* queryExecutor);
    ~CsrRepository() = default;

    /**
     * @brief Save a new CSR request to database
     * @return CSR ID or empty string on failure
     */
    std::string save(const std::string& subjectDn,
                     const std::string& countryCode,
                     const std::string& organization,
                     const std::string& commonName,
                     const std::string& keyAlgorithm,
                     const std::string& signatureAlgorithm,
                     const std::string& csrPem,
                     const std::vector<uint8_t>& csrDer,
                     const std::string& publicKeyFingerprint,
                     const std::string& privateKeyEncrypted,
                     const std::string& memo,
                     const std::string& createdBy);

    /**
     * @brief Find CSR by ID
     */
    Json::Value findById(const std::string& id);

    /**
     * @brief Find all CSRs with pagination
     */
    Json::Value findAll(int limit, int offset, const std::string& statusFilter = "");

    /**
     * @brief Count all CSRs
     */
    int countAll(const std::string& statusFilter = "");

    /**
     * @brief Update CSR status
     */
    bool updateStatus(const std::string& id, const std::string& status);

    /**
     * @brief Delete CSR by ID
     */
    bool deleteById(const std::string& id);

private:
    common::IQueryExecutor* queryExecutor_;
};

} // namespace repositories
