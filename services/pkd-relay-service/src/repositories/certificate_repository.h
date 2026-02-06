#pragma once

#include "../domain/models/certificate.h"
#include "i_query_executor.h"
#include <memory>
#include <vector>
#include <string>
#include <json/json.h>

namespace icao::relay::repositories {

/**
 * @brief Repository for certificate table operations (Database-agnostic)
 *
 * Handles certificate-related database operations for DB-LDAP synchronization.
 * All queries use parameterized statements for SQL injection prevention.
 * Uses Query Executor Pattern for database independence (PostgreSQL/Oracle).
 *
 * @date 2026-02-05 (Phase 5.2: Query Executor Pattern)
 */
class CertificateRepository {
public:
    /**
     * @brief Constructor with Query Executor injection
     * @param executor Query Executor (must remain valid during repository lifetime)
     * @throws std::invalid_argument if executor is nullptr
     */
    explicit CertificateRepository(common::IQueryExecutor* executor);

    /**
     * @brief Destructor
     */
    ~CertificateRepository() = default;

    // Disable copy and move
    CertificateRepository(const CertificateRepository&) = delete;
    CertificateRepository& operator=(const CertificateRepository&) = delete;
    CertificateRepository(CertificateRepository&&) = delete;
    CertificateRepository& operator=(CertificateRepository&&) = delete;

    /**
     * @brief Count certificates by type
     * @param certificateType Certificate type (CSCA, MLSC, DSC, DSC_NC)
     * @return Count of certificates of given type
     */
    int countByType(const std::string& certificateType);

    /**
     * @brief Find certificates not yet stored in LDAP
     * @param certificateType Certificate type filter (optional, empty = all types)
     * @param limit Maximum number of results (default: 1000)
     * @return Vector of certificates with stored_in_ldap = FALSE
     */
    std::vector<domain::Certificate> findNotInLdap(
        const std::string& certificateType = "",
        int limit = 1000
    );

    /**
     * @brief Mark certificates as stored in LDAP
     * @param fingerprints Vector of SHA-256 fingerprints to mark
     * @return Number of rows updated
     */
    int markStoredInLdap(const std::vector<std::string>& fingerprints);

    /**
     * @brief Mark a single certificate as stored in LDAP
     * @param fingerprint SHA-256 fingerprint
     * @return true if successful, false otherwise
     */
    bool markStoredInLdap(const std::string& fingerprint);

private:
    /**
     * @brief Convert database result row (JSON) to Certificate domain object
     * @param row Database result row as JSON
     * @return Certificate domain object
     */
    domain::Certificate jsonToCertificate(const Json::Value& row);

    common::IQueryExecutor* queryExecutor_;  // Not owned - do not free
};

} // namespace icao::relay::repositories
