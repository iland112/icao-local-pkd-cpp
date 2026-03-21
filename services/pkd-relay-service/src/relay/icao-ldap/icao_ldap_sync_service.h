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

// Forward declarations (shared libs use 'common' namespace)
namespace common { class IQueryExecutor; }
namespace common { class LdapConnectionPool; }

namespace icao {
namespace relay {

class IcaoLdapSyncService {
public:
    IcaoLdapSyncService(const Config& config,
                        common::IQueryExecutor* queryExecutor,
                        common::LdapConnectionPool* localLdapPool);
    ~IcaoLdapSyncService();

    /// Trigger a full sync (blocking). Returns result.
    IcaoLdapSyncResult performFullSync(const std::string& triggeredBy = "MANUAL");

    /// Check if sync is currently running
    bool isSyncRunning() const { return syncRunning_.load(); }

    /// Get last sync result
    IcaoLdapSyncResult getLastSyncResult() const;

    /// Get current configuration
    IcaoLdapSyncConfig getConfig() const;

    /// Update runtime configuration
    void updateConfig(const IcaoLdapSyncConfig& newConfig);

    /// Get sync history from DB
    std::vector<IcaoLdapSyncResult> getSyncHistory(int limit = 20) const;

private:
    /// Process a single certificate entry from ICAO LDAP
    /// Returns true if new (saved), false if existing (skipped)
    bool processEntry(const IcaoLdapCertEntry& entry);

    /// Compute SHA-256 fingerprint from DER data
    std::string computeFingerprint(const std::vector<uint8_t>& derData) const;

    /// Check if fingerprint already exists in local DB
    bool fingerprintExists(const std::string& fingerprint) const;

    /// Save certificate to local DB
    bool saveCertificateToDb(const IcaoLdapCertEntry& entry, const std::string& fingerprint);

    /// Save certificate to local LDAP
    bool saveCertificateToLocalLdap(const IcaoLdapCertEntry& entry, const std::string& fingerprint);

    /// Save CRL to local DB
    bool saveCrlToDb(const IcaoLdapCertEntry& entry, const std::string& fingerprint);

    /// Save CRL to local LDAP
    bool saveCrlToLocalLdap(const IcaoLdapCertEntry& entry, const std::string& fingerprint);

    /// Save sync result to DB
    void saveSyncLog(const IcaoLdapSyncResult& result);

    Config config_;
    common::IQueryExecutor* queryExecutor_;
    common::LdapConnectionPool* localLdapPool_;

    std::atomic<bool> syncRunning_{false};
    mutable std::mutex resultMutex_;
    IcaoLdapSyncResult lastResult_;
};

} // namespace relay
} // namespace icao
