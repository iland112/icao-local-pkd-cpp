#pragma once

#include <ldap.h>
#include <ldap_connection_pool.h>  // v2.4.3: LDAP connection pool
#include <memory>
#include "i_query_executor.h"      // Phase 6.4: Query Executor Pattern (replaces libpq-fe.h)
#include "relay/sync/common/types.h"
#include "relay/sync/common/config.h"
#include "ldap_operations.h"

namespace icao {
namespace relay {

// =============================================================================
// Reconciliation Engine - Database-LDAP Synchronization
// Phase 6.4: Migrated from PGconn* to IQueryExecutor* for Oracle support
// =============================================================================
class ReconciliationEngine {
public:
    // v2.4.3: Constructor now accepts LDAP connection pool
    // Phase 6.4: Accepts IQueryExecutor* instead of passing PGconn* to each method
    explicit ReconciliationEngine(
        const Config& config,
        common::LdapConnectionPool* ldapPool,
        common::IQueryExecutor* queryExecutor
    );
    ~ReconciliationEngine() = default;

    // Perform reconciliation between Database and LDAP
    ReconciliationResult performReconciliation(
        bool dryRun = false,
        const std::string& triggeredBy = "MANUAL",
        int syncStatusId = 0);

private:
    // Find certificates in DB that are missing in LDAP
    std::vector<CertificateInfo> findMissingInLdap(
        const std::string& certType,
        int limit) const;

    // v2.0.5: Find CRLs in DB that are missing in LDAP
    std::vector<CrlInfo> findMissingCrlsInLdap(
        int limit) const;

    // Mark certificate as stored in LDAP
    void markAsStoredInLdap(const std::string& certId) const;

    // v2.0.5: Mark CRL as stored in LDAP
    void markCrlAsStoredInLdap(const std::string& crlId) const;

    // Process certificates for a specific type
    void processCertificateType(
        LDAP* ld,
        const std::string& certType,
        bool dryRun,
        ReconciliationResult& result,
        const std::string& reconciliationId) const;

    // v2.0.5: Process CRLs
    void processCrls(
        LDAP* ld,
        bool dryRun,
        ReconciliationResult& result,
        const std::string& reconciliationId) const;

    // Database logging methods
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

    // Helper: Parse hex-encoded binary data from query result string
    static std::vector<unsigned char> parseHexBinary(const std::string& hexStr);

    // Helper: Get database-specific boolean literal for SQL
    std::string boolLiteral(bool value) const;

    const Config& config_;
    common::LdapConnectionPool* ldapPool_;  // v2.4.3: LDAP connection pool
    common::IQueryExecutor* queryExecutor_; // Phase 6.4: Database-agnostic query executor
    std::unique_ptr<LdapOperations> ldapOps_;
    std::string dbType_;                    // Cached database type ("postgres" or "oracle")
};

} // namespace relay
} // namespace icao
