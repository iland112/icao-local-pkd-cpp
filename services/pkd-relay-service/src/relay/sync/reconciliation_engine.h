/**
 * @file reconciliation_engine.h
 * @brief Reconciliation engine for DB-LDAP synchronization
 */
#pragma once

#include <ldap.h>
#include <ldap_connection_pool.h>
#include <memory>
#include "i_query_executor.h"
#include "relay/sync/common/types.h"
#include "relay/sync/common/config.h"
#include "ldap_operations.h"

namespace icao {
namespace relay {

/**
 * @brief Reconciliation engine for Database-LDAP synchronization
 *
 * Finds certificates and CRLs in the database that are missing from LDAP
 * and synchronizes them. Uses Query Executor Pattern for database independence.
 */
class ReconciliationEngine {
public:
    /**
     * @brief Constructor with LDAP pool and Query Executor injection
     * @param config Service configuration
     * @param ldapPool LDAP connection pool
     * @param queryExecutor Database query executor
     */
    explicit ReconciliationEngine(
        const Config& config,
        common::LdapConnectionPool* ldapPool,
        common::IQueryExecutor* queryExecutor
    );
    ~ReconciliationEngine() = default;

    /**
     * @brief Perform reconciliation between Database and LDAP
     * @param dryRun If true, simulate without making changes
     * @param triggeredBy Who triggered the reconciliation
     * @param syncStatusId Associated sync status ID
     * @return Reconciliation result with counters and status
     */
    ReconciliationResult performReconciliation(
        bool dryRun = false,
        const std::string& triggeredBy = "MANUAL",
        int syncStatusId = 0);

private:
    /**
     * @brief Find certificates in DB that are missing in LDAP
     * @param certType Certificate type filter
     * @param limit Maximum results
     * @return Vector of certificate information
     */
    std::vector<CertificateInfo> findMissingInLdap(
        const std::string& certType,
        int limit) const;

    /**
     * @brief Find CRLs in DB that are missing in LDAP
     * @param limit Maximum results
     * @return Vector of CRL information
     */
    std::vector<CrlInfo> findMissingCrlsInLdap(
        int limit) const;

    /** @brief Mark certificate as stored in LDAP */
    void markAsStoredInLdap(const std::string& certId) const;

    /** @brief Mark CRL as stored in LDAP */
    void markCrlAsStoredInLdap(const std::string& crlId) const;

    /**
     * @brief Process certificates for a specific type
     * @param ld LDAP connection handle
     * @param certType Certificate type to process
     * @param dryRun Simulate without changes
     * @param result Reconciliation result to update
     * @param reconciliationId Associated reconciliation ID
     */
    void processCertificateType(
        LDAP* ld,
        const std::string& certType,
        bool dryRun,
        ReconciliationResult& result,
        const std::string& reconciliationId) const;

    /**
     * @brief Process CRLs for reconciliation
     * @param ld LDAP connection handle
     * @param dryRun Simulate without changes
     * @param result Reconciliation result to update
     * @param reconciliationId Associated reconciliation ID
     */
    void processCrls(
        LDAP* ld,
        bool dryRun,
        ReconciliationResult& result,
        const std::string& reconciliationId) const;

    /// @name Database logging methods
    /// @{
    std::string createReconciliationSummary(
        const std::string& triggeredBy,
        bool dryRun,
        int syncStatusId) const;

    void updateReconciliationSummary(
        const std::string& reconciliationId,
        const ReconciliationResult& result) const;

    void logReconciliationOperation(
        const std::string& reconciliationId,
        const std::string& operation,
        const std::string& certType,
        const CertificateInfo& cert,
        const std::string& status,
        const std::string& errorMsg,
        int durationMs) const;
    /// @}

    /** @brief Parse hex-encoded binary data from query result string */
    static std::vector<unsigned char> parseHexBinary(const std::string& hexStr);

    /** @brief Get database-specific boolean literal for SQL */
    std::string boolLiteral(bool value) const;

    const Config& config_;
    common::LdapConnectionPool* ldapPool_;
    common::IQueryExecutor* queryExecutor_;
    std::unique_ptr<LdapOperations> ldapOps_;
    std::string dbType_;  // Cached database type ("postgres" or "oracle")
};

} // namespace relay
} // namespace icao
