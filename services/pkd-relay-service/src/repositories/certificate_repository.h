#pragma once

#include "../domain/models/certificate.h"
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
 */
class CertificateRepository {
public:
    /**
     * @brief Constructor
     * @param conninfo PostgreSQL connection string
     */
    explicit CertificateRepository(const std::string& conninfo);

    /**
     * @brief Destructor - closes database connection
     */
    ~CertificateRepository();

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

    /**
     * @brief Get database connection (reconnects if needed)
     * @return PGconn pointer
     */
    PGconn* getConnection();

    std::string conninfo_;
    PGconn* conn_ = nullptr;
};

} // namespace icao::relay::repositories
