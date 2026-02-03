#pragma once

#include "../domain/models/crl.h"
#include <memory>
#include <vector>
#include <string>
#include <libpq-fe.h>

namespace icao::relay::repositories {

/**
 * @brief Repository for crl table operations
 *
 * Handles CRL-related database operations for DB-LDAP synchronization.
 * All queries use parameterized statements for SQL injection prevention.
 */
class CrlRepository {
public:
    /**
     * @brief Constructor
     * @param conninfo PostgreSQL connection string
     */
    explicit CrlRepository(const std::string& conninfo);

    /**
     * @brief Destructor - closes database connection
     */
    ~CrlRepository();

    // Disable copy and move
    CrlRepository(const CrlRepository&) = delete;
    CrlRepository& operator=(const CrlRepository&) = delete;
    CrlRepository(CrlRepository&&) = delete;
    CrlRepository& operator=(CrlRepository&&) = delete;

    /**
     * @brief Count total CRLs
     * @return Total number of CRLs
     */
    int countAll();

    /**
     * @brief Find CRLs not yet stored in LDAP
     * @param limit Maximum number of results (default: 1000)
     * @return Vector of CRLs with stored_in_ldap = FALSE
     */
    std::vector<domain::Crl> findNotInLdap(int limit = 1000);

    /**
     * @brief Mark CRLs as stored in LDAP
     * @param fingerprints Vector of SHA-256 fingerprints to mark
     * @return Number of rows updated
     */
    int markStoredInLdap(const std::vector<std::string>& fingerprints);

    /**
     * @brief Mark a single CRL as stored in LDAP
     * @param fingerprint SHA-256 fingerprint
     * @return true if successful, false otherwise
     */
    bool markStoredInLdap(const std::string& fingerprint);

private:
    /**
     * @brief Convert PostgreSQL result row to Crl domain object
     * @param res PGresult pointer
     * @param row Row number in result set
     * @return Crl domain object
     */
    domain::Crl resultToCrl(PGresult* res, int row);

    /**
     * @brief Get database connection (reconnects if needed)
     * @return PGconn pointer
     */
    PGconn* getConnection();

    std::string conninfo_;
    PGconn* conn_ = nullptr;
};

} // namespace icao::relay::repositories
