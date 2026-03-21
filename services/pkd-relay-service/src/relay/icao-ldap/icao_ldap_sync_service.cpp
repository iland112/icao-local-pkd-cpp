/**
 * @file icao_ldap_sync_service.cpp
 * @brief ICAO PKD LDAP synchronization implementation
 */
#include "icao_ldap_sync_service.h"

#include <spdlog/spdlog.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <ldap.h>

#include "i_query_executor.h"
#include "query_helpers.h"
#include <ldap_connection_pool.h>

namespace icao {
namespace relay {

IcaoLdapSyncService::IcaoLdapSyncService(const Config& config,
                                         common::IQueryExecutor* queryExecutor,
                                         common::LdapConnectionPool* localLdapPool)
    : config_(config), queryExecutor_(queryExecutor), localLdapPool_(localLdapPool)
{
    spdlog::info("[IcaoLdapSync] Initialized (enabled={}, host={}:{})",
                config_.icaoLdapSyncEnabled, config_.icaoLdapHost, config_.icaoLdapPort);
}

IcaoLdapSyncService::~IcaoLdapSyncService() = default;

IcaoLdapSyncResult IcaoLdapSyncService::performFullSync(const std::string& triggeredBy) {
    if (syncRunning_.exchange(true)) {
        IcaoLdapSyncResult busy;
        busy.status = "BUSY";
        busy.errorMessage = "Sync already in progress";
        return busy;
    }

    IcaoLdapSyncResult result;
    result.syncType = "FULL";
    result.triggeredBy = triggeredBy;
    result.status = "RUNNING";
    result.startedAt = std::chrono::system_clock::now();

    spdlog::info("[IcaoLdapSync] Starting {} sync (triggered by: {})", result.syncType, triggeredBy);

    try {
        IcaoLdapClient client(config_.icaoLdapHost, config_.icaoLdapPort,
                              config_.icaoLdapBindDn, config_.icaoLdapBindPassword,
                              config_.icaoLdapBaseDn);

        if (!client.connect()) {
            result.status = "FAILED";
            result.errorMessage = "Failed to connect to ICAO PKD LDAP at " +
                                 config_.icaoLdapHost + ":" + std::to_string(config_.icaoLdapPort);
            saveSyncLog(result);
            syncRunning_ = false;
            return result;
        }

        result.totalRemoteCount = client.getTotalEntryCount();
        spdlog::info("[IcaoLdapSync] Total entries in ICAO PKD: {}", result.totalRemoteCount);

        // Sync helper lambda
        auto syncEntries = [&](const std::string& typeName,
                               std::vector<IcaoLdapCertEntry> (IcaoLdapClient::*searchFn)(int)) {
            auto entries = (client.*searchFn)(0);
            spdlog::info("[IcaoLdapSync] Processing {} {} entries...", entries.size(), typeName);

            for (const auto& entry : entries) {
                try {
                    if (processEntry(entry)) {
                        result.newCertificates++;
                    } else {
                        result.existingSkipped++;
                    }
                } catch (const std::exception& e) {
                    result.failedCount++;
                    spdlog::warn("[IcaoLdapSync] Failed to process {}: {}", entry.dn, e.what());
                }
            }
        };

        syncEntries("CSCA", &IcaoLdapClient::searchCscaCertificates);
        syncEntries("DSC", &IcaoLdapClient::searchDscCertificates);
        syncEntries("CRL", &IcaoLdapClient::searchCrls);
        syncEntries("DSC_NC", &IcaoLdapClient::searchNcDscCertificates);

        client.disconnect();

        result.status = "COMPLETED";
        result.completedAt = std::chrono::system_clock::now();
        result.durationMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                result.completedAt - result.startedAt).count());

        spdlog::info("[IcaoLdapSync] Sync completed: new={}, skipped={}, failed={}, duration={}ms",
                    result.newCertificates, result.existingSkipped, result.failedCount, result.durationMs);

    } catch (const std::exception& e) {
        result.status = "FAILED";
        result.errorMessage = e.what();
        result.completedAt = std::chrono::system_clock::now();
        result.durationMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                result.completedAt - result.startedAt).count());
        spdlog::error("[IcaoLdapSync] Sync failed: {}", e.what());
    }

    saveSyncLog(result);

    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        lastResult_ = result;
    }

    syncRunning_ = false;
    return result;
}

bool IcaoLdapSyncService::processEntry(const IcaoLdapCertEntry& entry) {
    if (entry.binaryData.empty()) return false;

    auto fingerprint = computeFingerprint(entry.binaryData);
    if (fingerprint.empty()) return false;

    if (fingerprintExists(fingerprint)) {
        return false;
    }

    bool saved = false;
    if (entry.certType == "CRL") {
        saved = saveCrlToDb(entry, fingerprint);
        if (saved) saveCrlToLocalLdap(entry, fingerprint);
    } else {
        saved = saveCertificateToDb(entry, fingerprint);
        if (saved) saveCertificateToLocalLdap(entry, fingerprint);
    }

    return saved;
}

std::string IcaoLdapSyncService::computeFingerprint(const std::vector<uint8_t>& derData) const {
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen = 0;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return "";

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) &&
        EVP_DigestUpdate(ctx, derData.data(), derData.size()) &&
        EVP_DigestFinal_ex(ctx, hash, &hashLen)) {
        EVP_MD_CTX_free(ctx);

        std::ostringstream oss;
        for (unsigned int i = 0; i < hashLen; ++i) {
            oss << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(hash[i]);
        }
        return oss.str();
    }

    EVP_MD_CTX_free(ctx);
    return "";
}

bool IcaoLdapSyncService::fingerprintExists(const std::string& fingerprint) const {
    if (!queryExecutor_) return false;

    try {
        auto result = queryExecutor_->executeQuery(
            "SELECT COUNT(*) AS cnt FROM certificate WHERE fingerprint_sha256 = $1",
            {fingerprint});

        if (!result.empty()) {
            int count = common::db::scalarToInt(result[0]["cnt"]);
            return count > 0;
        }
    } catch (const std::exception& e) {
        spdlog::warn("[IcaoLdapSync] fingerprintExists check failed: {}", e.what());
    }
    return false;
}

bool IcaoLdapSyncService::saveCertificateToDb(const IcaoLdapCertEntry& entry,
                                              const std::string& fingerprint) {
    if (!queryExecutor_) return false;

    try {
        const uint8_t* p = entry.binaryData.data();
        X509* cert = d2i_X509(nullptr, &p, static_cast<long>(entry.binaryData.size()));
        if (!cert) {
            spdlog::warn("[IcaoLdapSync] Failed to parse X.509 for {}", entry.dn);
            return false;
        }

        char subjectBuf[512] = {0}, issuerBuf[512] = {0};
        X509_NAME_oneline(X509_get_subject_name(cert), subjectBuf, sizeof(subjectBuf));
        X509_NAME_oneline(X509_get_issuer_name(cert), issuerBuf, sizeof(issuerBuf));

        std::ostringstream hexStream;
        for (auto b : entry.binaryData) {
            hexStream << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
        }

        std::string dbType = queryExecutor_->getDatabaseType();
        std::string certType = entry.certType;

        std::string sql;
        if (dbType == "oracle") {
            sql = "INSERT INTO certificate (id, fingerprint_sha256, certificate_type, "
                  "country_code, subject_dn, issuer_dn, certificate_data, source_type, "
                  "stored_in_ldap, created_at) "
                  "VALUES (SYS_GUID(), $1, $2, $3, $4, $5, $6, $7, "
                  + common::db::boolLiteral(dbType, false) + ", "
                  + common::db::currentTimestamp(dbType) + ")";
        } else {
            sql = "INSERT INTO certificate (id, fingerprint_sha256, certificate_type, "
                  "country_code, subject_dn, issuer_dn, certificate_data, source_type, "
                  "stored_in_ldap, created_at) "
                  "VALUES (gen_random_uuid(), $1, $2, $3, $4, $5, decode($6, 'hex'), $7, "
                  "FALSE, NOW()) "
                  "ON CONFLICT (fingerprint_sha256) DO NOTHING";
        }

        queryExecutor_->executeQuery(sql, {
            fingerprint, certType, entry.countryCode,
            std::string(subjectBuf), std::string(issuerBuf),
            hexStream.str(), std::string("ICAO_PKD_SYNC")
        });

        X509_free(cert);
        return true;

    } catch (const std::exception& e) {
        spdlog::warn("[IcaoLdapSync] saveCertificateToDb failed: {}", e.what());
        return false;
    }
}

bool IcaoLdapSyncService::saveCertificateToLocalLdap(const IcaoLdapCertEntry& entry,
                                                     const std::string& fingerprint) {
    if (!localLdapPool_) return false;

    try {
        auto conn = localLdapPool_->acquire();
        if (!conn.isValid()) return false;
        LDAP* ld = conn.get();

        std::string localType;
        std::string dataContainer;
        if (entry.certType == "CSCA") { localType = "csca"; dataContainer = "dc=data"; }
        else if (entry.certType == "DSC") { localType = "dsc"; dataContainer = "dc=data"; }
        else if (entry.certType == "DSC_NC") { localType = "dsc"; dataContainer = "dc=nc-data"; }
        else if (entry.certType == "MLSC") { localType = "mlsc"; dataContainer = "dc=data"; }
        else return false;

        std::string localBase = "dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";
        std::string dn = "cn=" + fingerprint + ",o=" + localType + ",c=" +
                         entry.countryCode + "," + dataContainer + "," + localBase;

        // Ensure parent OUs (country + type)
        auto ensureOu = [&](const std::string& ouDn, const char* objClass,
                           const char* attrName, const std::string& attrVal) {
            LDAPMessage* res = nullptr;
            int rc = ldap_search_ext_s(ld, ouDn.c_str(), LDAP_SCOPE_BASE,
                                      "(objectClass=*)", nullptr, 0, nullptr, nullptr, nullptr, 0, &res);
            if (res) ldap_msgfree(res);
            if (rc == LDAP_SUCCESS) return;

            char* ocVals[] = {const_cast<char*>("top"), const_cast<char*>(objClass), nullptr};
            char* valVals[] = {const_cast<char*>(attrVal.c_str()), nullptr};

            LDAPMod ocMod; ocMod.mod_op = LDAP_MOD_ADD; ocMod.mod_type = const_cast<char*>("objectClass"); ocMod.mod_values = ocVals;
            LDAPMod valMod; valMod.mod_op = LDAP_MOD_ADD; valMod.mod_type = const_cast<char*>(attrName); valMod.mod_values = valVals;
            LDAPMod* attrs[] = {&ocMod, &valMod, nullptr};

            ldap_add_ext_s(ld, ouDn.c_str(), attrs, nullptr, nullptr);
        };

        std::string countryDn = "c=" + entry.countryCode + "," + dataContainer + "," + localBase;
        std::string typeDn = "o=" + localType + "," + countryDn;
        ensureOu(countryDn, "country", "c", entry.countryCode);
        ensureOu(typeDn, "organization", "o", localType);

        // Add certificate entry
        struct berval certBer;
        certBer.bv_val = reinterpret_cast<char*>(const_cast<uint8_t*>(entry.binaryData.data()));
        certBer.bv_len = entry.binaryData.size();
        struct berval* certBerVals[] = {&certBer, nullptr};

        std::string snVal = fingerprint.substr(0, 16);
        char* ocVals[] = {const_cast<char*>("top"), const_cast<char*>("inetOrgPerson"),
                          const_cast<char*>("pkdDownload"), nullptr};
        char* cnVals[] = {const_cast<char*>(fingerprint.c_str()), nullptr};
        char* snVals[] = {const_cast<char*>(snVal.c_str()), nullptr};

        LDAPMod ocMod; ocMod.mod_op = LDAP_MOD_ADD; ocMod.mod_type = const_cast<char*>("objectClass"); ocMod.mod_values = ocVals;
        LDAPMod cnMod; cnMod.mod_op = LDAP_MOD_ADD; cnMod.mod_type = const_cast<char*>("cn"); cnMod.mod_values = cnVals;
        LDAPMod snMod; snMod.mod_op = LDAP_MOD_ADD; snMod.mod_type = const_cast<char*>("sn"); snMod.mod_values = snVals;
        LDAPMod certMod; certMod.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
        certMod.mod_type = const_cast<char*>("userCertificate;binary"); certMod.mod_bvalues = certBerVals;

        LDAPMod* addAttrs[] = {&ocMod, &cnMod, &snMod, &certMod, nullptr};

        int rc = ldap_add_ext_s(ld, dn.c_str(), addAttrs, nullptr, nullptr);
        if (rc == LDAP_SUCCESS || rc == LDAP_ALREADY_EXISTS) {
            std::string dbType = queryExecutor_->getDatabaseType();
            queryExecutor_->executeQuery(
                "UPDATE certificate SET stored_in_ldap = " + common::db::boolLiteral(dbType, true) +
                " WHERE fingerprint_sha256 = $1", {fingerprint});
            return true;
        }

        spdlog::warn("[IcaoLdapSync] LDAP add failed for {}: {}", dn, ldap_err2string(rc));
        return false;

    } catch (const std::exception& e) {
        spdlog::warn("[IcaoLdapSync] saveCertificateToLocalLdap failed: {}", e.what());
        return false;
    }
}

bool IcaoLdapSyncService::saveCrlToDb(const IcaoLdapCertEntry& entry,
                                      const std::string& fingerprint) {
    if (!queryExecutor_) return false;

    try {
        std::ostringstream hexStream;
        for (auto b : entry.binaryData) {
            hexStream << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
        }

        std::string dbType = queryExecutor_->getDatabaseType();

        std::string sql;
        if (dbType == "oracle") {
            sql = "INSERT INTO crl (id, fingerprint_sha256, country_code, crl_binary, "
                  "source_type, stored_in_ldap, created_at) "
                  "VALUES (SYS_GUID(), $1, $2, $3, $4, "
                  + common::db::boolLiteral(dbType, false) + ", "
                  + common::db::currentTimestamp(dbType) + ")";
        } else {
            sql = "INSERT INTO crl (id, fingerprint_sha256, country_code, crl_binary, "
                  "source_type, stored_in_ldap, created_at) "
                  "VALUES (gen_random_uuid(), $1, $2, decode($3, 'hex'), $4, "
                  "FALSE, NOW()) "
                  "ON CONFLICT (fingerprint_sha256) DO NOTHING";
        }

        queryExecutor_->executeQuery(sql, {
            fingerprint, entry.countryCode, hexStream.str(), std::string("ICAO_PKD_SYNC")
        });
        return true;

    } catch (const std::exception& e) {
        spdlog::warn("[IcaoLdapSync] saveCrlToDb failed: {}", e.what());
        return false;
    }
}

bool IcaoLdapSyncService::saveCrlToLocalLdap(const IcaoLdapCertEntry& entry,
                                             const std::string& fingerprint) {
    if (!localLdapPool_) return false;

    try {
        auto conn = localLdapPool_->acquire();
        if (!conn.isValid()) return false;
        LDAP* ld = conn.get();

        std::string localBase = "dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";
        std::string dn = "cn=" + fingerprint + ",o=crl,c=" + entry.countryCode +
                         ",dc=data," + localBase;

        struct berval crlBer;
        crlBer.bv_val = reinterpret_cast<char*>(const_cast<uint8_t*>(entry.binaryData.data()));
        crlBer.bv_len = entry.binaryData.size();
        struct berval* crlBerVals[] = {&crlBer, nullptr};

        char* ocVals[] = {const_cast<char*>("top"), const_cast<char*>("cRLDistributionPoint"), nullptr};
        char* cnVals[] = {const_cast<char*>(fingerprint.c_str()), nullptr};

        LDAPMod ocMod; ocMod.mod_op = LDAP_MOD_ADD; ocMod.mod_type = const_cast<char*>("objectClass"); ocMod.mod_values = ocVals;
        LDAPMod cnMod; cnMod.mod_op = LDAP_MOD_ADD; cnMod.mod_type = const_cast<char*>("cn"); cnMod.mod_values = cnVals;
        LDAPMod crlMod; crlMod.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
        crlMod.mod_type = const_cast<char*>("certificateRevocationList;binary"); crlMod.mod_bvalues = crlBerVals;

        LDAPMod* addAttrs[] = {&ocMod, &cnMod, &crlMod, nullptr};

        int rc = ldap_add_ext_s(ld, dn.c_str(), addAttrs, nullptr, nullptr);
        if (rc == LDAP_SUCCESS || rc == LDAP_ALREADY_EXISTS) {
            std::string dbType = queryExecutor_->getDatabaseType();
            queryExecutor_->executeQuery(
                "UPDATE crl SET stored_in_ldap = " + common::db::boolLiteral(dbType, true) +
                " WHERE fingerprint_sha256 = $1", {fingerprint});
            return true;
        }

        return false;
    } catch (...) {
        return false;
    }
}

void IcaoLdapSyncService::saveSyncLog(const IcaoLdapSyncResult& result) {
    if (!queryExecutor_) return;

    try {
        std::string dbType = queryExecutor_->getDatabaseType();

        std::string sql;
        if (dbType == "oracle") {
            sql = "INSERT INTO icao_ldap_sync_log (id, sync_type, status, triggered_by, "
                  "total_remote_count, new_certificates, updated_certificates, failed_count, "
                  "duration_ms, error_message, started_at, created_at) "
                  "VALUES (SYS_GUID(), $1, $2, $3, $4, $5, $6, $7, $8, $9, "
                  + common::db::currentTimestamp(dbType) + ", "
                  + common::db::currentTimestamp(dbType) + ")";
        } else {
            sql = "INSERT INTO icao_ldap_sync_log (id, sync_type, status, triggered_by, "
                  "total_remote_count, new_certificates, updated_certificates, failed_count, "
                  "duration_ms, error_message, started_at, created_at) "
                  "VALUES (gen_random_uuid(), $1, $2, $3, $4, $5, $6, $7, $8, $9, NOW(), NOW())";
        }

        queryExecutor_->executeQuery(sql, {
            result.syncType, result.status, result.triggeredBy,
            std::to_string(result.totalRemoteCount),
            std::to_string(result.newCertificates),
            std::to_string(result.existingSkipped),
            std::to_string(result.failedCount),
            std::to_string(result.durationMs),
            result.errorMessage
        });
    } catch (const std::exception& e) {
        spdlog::warn("[IcaoLdapSync] Failed to save sync log: {}", e.what());
    }
}

IcaoLdapSyncResult IcaoLdapSyncService::getLastSyncResult() const {
    std::lock_guard<std::mutex> lock(resultMutex_);
    return lastResult_;
}

IcaoLdapSyncConfig IcaoLdapSyncService::getConfig() const {
    IcaoLdapSyncConfig cfg;
    cfg.enabled = config_.icaoLdapSyncEnabled;
    cfg.host = config_.icaoLdapHost;
    cfg.port = config_.icaoLdapPort;
    cfg.bindDn = config_.icaoLdapBindDn;
    cfg.baseDn = config_.icaoLdapBaseDn;
    cfg.syncIntervalMinutes = config_.icaoLdapSyncIntervalMinutes;
    return cfg;
}

void IcaoLdapSyncService::updateConfig(const IcaoLdapSyncConfig& newConfig) {
    config_.icaoLdapSyncEnabled = newConfig.enabled;
    config_.icaoLdapSyncIntervalMinutes = newConfig.syncIntervalMinutes;
    spdlog::info("[IcaoLdapSync] Config updated: enabled={}, interval={}min",
                newConfig.enabled, newConfig.syncIntervalMinutes);
}

std::vector<IcaoLdapSyncResult> IcaoLdapSyncService::getSyncHistory(int limit) const {
    std::vector<IcaoLdapSyncResult> history;
    if (!queryExecutor_) return history;

    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        auto rows = queryExecutor_->executeQuery(
            "SELECT sync_type, status, triggered_by, total_remote_count, "
            "new_certificates, updated_certificates, failed_count, duration_ms, "
            "error_message, created_at FROM icao_ldap_sync_log "
            "ORDER BY created_at DESC " + common::db::limitClause(dbType, limit), {});

        for (const auto& row : rows) {
            IcaoLdapSyncResult r;
            r.syncType = row.get("sync_type", "").asString();
            r.status = row.get("status", "").asString();
            r.triggeredBy = row.get("triggered_by", "").asString();
            r.totalRemoteCount = common::db::scalarToInt(row["total_remote_count"]);
            r.newCertificates = common::db::scalarToInt(row["new_certificates"]);
            r.existingSkipped = common::db::scalarToInt(row["updated_certificates"]);
            r.failedCount = common::db::scalarToInt(row["failed_count"]);
            r.durationMs = common::db::scalarToInt(row["duration_ms"]);
            r.errorMessage = row.get("error_message", "").asString();
            history.push_back(std::move(r));
        }
    } catch (const std::exception& e) {
        spdlog::warn("[IcaoLdapSync] getSyncHistory failed: {}", e.what());
    }

    return history;
}

} // namespace relay
} // namespace icao
