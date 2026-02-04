#pragma once

#include "../domain/models/certificate.h"
#include "db_connection_pool.h"
#include <memory>
#include <vector>
#include <string>
#include <libpq-fe.h>

namespace icao::relay::repositories {

/**
 * @brief Repository for certificate table operations (reconciliation subset)
 *
 * Handles certificate-related database operations for DB-LDAP synchronization.
 * All queries use parameterized statements for SQL injection prevention.
 *
 * Thread-safe: Uses DbConnectionPool for concurrent request handling.
 */
class CertificateRepository {
public:
    /**
     * @brief Constructor
     * @param dbPool Shared database connection pool
     */
    explicit CertificateRepository(std::shared_ptr<common::DbConnectionPool> dbPool);

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
     * @brief Convert PostgreSQL result row to Certificate domain object
     * @param res PGresult pointer
     * @param row Row number in result set
     * @return Certificate domain object
     */
    domain::Certificate resultToCertificate(PGresult* res, int row);

    std::shared_ptr<common::DbConnectionPool> dbPool_;
};

} // namespace icao::relay::repositories
