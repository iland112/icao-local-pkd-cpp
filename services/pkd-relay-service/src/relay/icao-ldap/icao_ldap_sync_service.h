/**
 * @file icao_ldap_sync_service.h
 * @brief ICAO PKD LDAP synchronization orchestrator
 *
 * Connects to the ICAO PKD LDAP (simulation or production), downloads
 * certificates/CRLs, and imports them into the local database and LDAP.
 *
 * Supports two operation modes (co-existing):
 * 1. Manual upload: LDIF/ML files uploaded via PKD Management UI
 * 2. Auto sync: Periodic LDAP V3 sync from ICAO PKD (this service)
 */
#pragma once

#include "icao_ldap_types.h"
#include "icao_ldap_client.h"
#include "../sync/common/config.h"

#include <string>
#include <memory>
#include <atomic>
#include <mutex>
#include <functional>

// Forward declare OpenSSL X509 type
typedef struct x509_st X509;

// Forward declarations (shared libs use 'common' namespace)
namespace common { class IQueryExecutor; }
namespace common { class LdapConnectionPool; }

// Forward declarations - validation
namespace icao::validation {
    class TrustChainBuilder;
    class CrlChecker;
    class ICscaProvider;
    class ICrlProvider;
}
namespace icao::relay::repositories {
    class CertificateRepository;
    class CrlRepository;
    class ValidationRepository;
}

namespace icao {
namespace relay {

class IcaoLdapSyncService {
public:
    IcaoLdapSyncService(const Config& config,
                        common::IQueryExecutor* queryExecutor,
                        common::LdapConnectionPool* localLdapPool,
                        icao::relay::repositories::CertificateRepository* certRepo,
                        icao::relay::repositories::CrlRepository* crlRepo,
                        icao::relay::repositories::ValidationRepository* validationRepo);
    ~IcaoLdapSyncService();

    /// Trigger a full sync (blocking). Returns result.
    IcaoLdapSyncResult performFullSync(const std::string& triggeredBy = "MANUAL");

    /// Test connection to ICAO PKD LDAP (non-destructive)
    IcaoLdapConnectionTestResult testConnection();

    /// Check if sync is currently running
    bool isSyncRunning() const { return syncRunning_.load(); }

    /// Get last sync result
    IcaoLdapSyncResult getLastSyncResult() const;

    /// Get current configuration
    IcaoLdapSyncConfig getConfig() const;

    /// Update runtime configuration
    void updateConfig(const IcaoLdapSyncConfig& newConfig);

    /// Get sync history from DB (with pagination and filter)
    std::vector<IcaoLdapSyncResult> getSyncHistory(int limit = 20, int offset = 0,
                                                    const std::string& statusFilter = "") const;

    /// Get total sync history count (for pagination)
    int getSyncHistoryCount(const std::string& statusFilter = "") const;

private:
    /// Result of processing a single entry
    enum class EntryResult { NEW, SKIPPED, FAILED };

    /// Process a single certificate entry from ICAO LDAP
    EntryResult processEntry(const IcaoLdapCertEntry& entry);

    /// Compute SHA-256 fingerprint from DER data
    std::string computeFingerprint(const std::vector<uint8_t>& derData) const;

    /// Check if fingerprint already exists in local DB
    bool fingerprintExists(const std::string& fingerprint) const;

    /// Extract full X.509 metadata (22 fields) and save to local DB
    bool saveCertificateToDb(const IcaoLdapCertEntry& entry, const std::string& fingerprint);

    /// Perform Trust Chain validation and save result to validation_result table
    void validateAndSaveResult(const IcaoLdapCertEntry& entry, const std::string& fingerprint,
                              X509* cert);

    /// Save certificate to local LDAP
    bool saveCertificateToLocalLdap(const IcaoLdapCertEntry& entry, const std::string& fingerprint);

    /// Process Master List: extract CSCAs from CMS SignedData
    /// Returns number of new CSCAs extracted and saved
    int processMasterListEntry(const IcaoLdapCertEntry& mlEntry);

    /// Save CRL to local DB
    bool saveCrlToDb(const IcaoLdapCertEntry& entry, const std::string& fingerprint);

    /// Save CRL to local LDAP
    bool saveCrlToLocalLdap(const IcaoLdapCertEntry& entry, const std::string& fingerprint);

    /// Save sync result to DB
    void saveSyncLog(const IcaoLdapSyncResult& result);

    Config config_;
    common::IQueryExecutor* queryExecutor_;
    common::LdapConnectionPool* localLdapPool_;

    // Validation dependencies (non-owning)
    icao::relay::repositories::CertificateRepository* certRepo_;
    icao::relay::repositories::CrlRepository* crlRepo_;
    icao::relay::repositories::ValidationRepository* validationRepo_;

    // Owned validation library instances (lazy-initialized)
    std::unique_ptr<icao::validation::ICscaProvider> cscaProvider_;
    std::unique_ptr<icao::validation::ICrlProvider> crlProvider_;
    std::unique_ptr<icao::validation::TrustChainBuilder> trustChainBuilder_;
    std::unique_ptr<icao::validation::CrlChecker> crlChecker_;

    /// Initialize validation components (lazy)
    void initValidation();

    /// Broadcast current progress via SSE
    void broadcastProgress(const IcaoLdapSyncProgress& progress);

    std::atomic<bool> syncRunning_{false};
    mutable std::mutex resultMutex_;
    IcaoLdapSyncResult lastResult_;
    IcaoLdapSyncProgress currentProgress_;
};

} // namespace relay
} // namespace icao
