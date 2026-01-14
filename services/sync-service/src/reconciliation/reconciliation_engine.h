#pragma once

#include <libpq-fe.h>
#include <ldap.h>
#include <memory>
#include "../common/types.h"
#include "../common/config.h"
#include "ldap_operations.h"

namespace icao {
namespace sync {

// =============================================================================
// Reconciliation Engine - PostgreSQL-LDAP Synchronization
// =============================================================================
class ReconciliationEngine {
public:
    explicit ReconciliationEngine(const Config& config);
    ~ReconciliationEngine() = default;

    // Perform reconciliation between PostgreSQL and LDAP
    ReconciliationResult performReconciliation(
        PGconn* pgConn,
        bool dryRun = false,
        const std::string& triggeredBy = "MANUAL",
        int syncStatusId = 0);

private:
    // Find certificates in DB that are missing in LDAP
    std::vector<CertificateInfo> findMissingInLdap(
        PGconn* pgConn,
        const std::string& certType,
        int limit) const;

    // Mark certificate as stored in LDAP
    void markAsStoredInLdap(PGconn* pgConn, const std::string& certId) const;

    // Connect to LDAP write host
    LDAP* connectToLdapWrite(std::string& errorMsg) const;

    // Process certificates for a specific type
    void processCertificateType(
        PGconn* pgConn,
        LDAP* ld,
        const std::string& certType,
        bool dryRun,
        ReconciliationResult& result,
        int reconciliationId) const;

    // Database logging methods
    int createReconciliationSummary(
        PGconn* pgConn,
        const std::string& triggeredBy,
        bool dryRun,
        int syncStatusId) const;

    void updateReconciliationSummary(
        PGconn* pgConn,
        int reconciliationId,
        const ReconciliationResult& result) const;

    void logReconciliationOperation(
        PGconn* pgConn,
        int reconciliationId,
        const std::string& operation,
        const std::string& certType,
        const CertificateInfo& cert,
        const std::string& status,
        const std::string& errorMsg,
        int durationMs) const;

    const Config& config_;
    std::unique_ptr<LdapOperations> ldapOps_;
};

} // namespace sync
} // namespace icao
