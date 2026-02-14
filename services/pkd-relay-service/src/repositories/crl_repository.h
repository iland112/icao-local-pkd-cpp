/**
 * @file crl_repository.h
 * @brief Repository for CRL table operations
 */
#pragma once

#include "../domain/models/crl.h"
#include "i_query_executor.h"
#include <memory>
#include <vector>
#include <string>
#include <json/json.h>

namespace icao::relay::repositories {

/**
 * @brief Repository for crl table operations (Database-agnostic)
 *
 * Handles CRL-related database operations for DB-LDAP synchronization.
 * All queries use parameterized statements for SQL injection prevention.
 * Uses Query Executor Pattern for database independence (PostgreSQL/Oracle).
 */
class CrlRepository {
public:
    /**
     * @brief Constructor with Query Executor injection
     * @param executor Query Executor (must remain valid during repository lifetime)
     * @throws std::invalid_argument if executor is nullptr
     */
    explicit CrlRepository(common::IQueryExecutor* executor);

    /**
     * @brief Destructor
     */
    ~CrlRepository() = default;

    /// @name Non-copyable and non-movable
    /// @{
    CrlRepository(const CrlRepository&) = delete;
    CrlRepository& operator=(const CrlRepository&) = delete;
    CrlRepository(CrlRepository&&) = delete;
    CrlRepository& operator=(CrlRepository&&) = delete;
    /// @}

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
     * @brief Convert database result row (JSON) to Crl domain object
     * @param row Database result row as JSON
     * @return Crl domain object
     */
    domain::Crl jsonToCrl(const Json::Value& row);

    common::IQueryExecutor* queryExecutor_;  // Not owned - do not free
};

} // namespace icao::relay::repositories
