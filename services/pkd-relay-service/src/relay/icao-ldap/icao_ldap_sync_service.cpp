/**
 * @file icao_ldap_sync_service.cpp
 * @brief ICAO PKD LDAP synchronization implementation
 */
#include "icao_ldap_sync_service.h"

#include <spdlog/spdlog.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/cms.h>
#include <icao/validation/icao_compliance.h>
#include <iomanip>
#include <sstream>
#include <chrono>
#include <ldap.h>

namespace {

/// Read X.509 PEM file and extract subject, issuer, expiry
struct CertInfo {
    std::string subject;
    std::string issuer;
    std::string expiry;
};

CertInfo readCertInfo(const std::string& pemPath) {
    CertInfo info;
    FILE* fp = fopen(pemPath.c_str(), "r");
    if (!fp) return info;

    X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
    fclose(fp);
    if (!cert) return info;

    // Subject
    char* subj = X509_NAME_oneline(X509_get_subject_name(cert), nullptr, 0);
    if (subj) { info.subject = subj; OPENSSL_free(subj); }

    // Issuer
    char* iss = X509_NAME_oneline(X509_get_issuer_name(cert), nullptr, 0);
    if (iss) { info.issuer = iss; OPENSSL_free(iss); }

    // Expiry (NotAfter)
    const ASN1_TIME* notAfter = X509_get0_notAfter(cert);
    if (notAfter) {
        struct tm tm = {};
        if (ASN1_TIME_to_tm(notAfter, &tm)) {
            char buf[32];
            strftime(buf, sizeof(buf), "%Y-%m-%d", &tm);
            info.expiry = buf;
        }
    }

    X509_free(cert);
    return info;
}

} // anonymous namespace

#include "i_query_executor.h"
#include "query_helpers.h"
#include <ldap_connection_pool.h>
#include "../../common/notification_manager.h"

// Validation library (shared lib - resolved by CMake include paths)
#include <icao/validation/trust_chain_builder.h>
#include <icao/validation/crl_checker.h>
#include <icao/validation/cert_ops.h>
#include <icao/validation/types.h>
// Adapters & repositories (relative to src/)
#include "../../adapters/relay_csca_provider.h"
#include "../../adapters/relay_crl_provider.h"
#include "../../repositories/certificate_repository.h"
#include "../../repositories/crl_repository.h"
#include "../../repositories/validation_repository.h"

namespace icao {
namespace relay {

IcaoLdapSyncService::IcaoLdapSyncService(const Config& config,
                                         common::IQueryExecutor* queryExecutor,
                                         common::LdapConnectionPool* localLdapPool,
                                         icao::relay::repositories::CertificateRepository* certRepo,
                                         icao::relay::repositories::CrlRepository* crlRepo,
                                         icao::relay::repositories::ValidationRepository* validationRepo)
    : config_(config), queryExecutor_(queryExecutor), localLdapPool_(localLdapPool),
      certRepo_(certRepo), crlRepo_(crlRepo), validationRepo_(validationRepo)
{
    spdlog::info("[IcaoLdapSync] Initialized (enabled={}, host={}:{})",
                config_.icaoLdapSyncEnabled, config_.icaoLdapHost, config_.icaoLdapPort);
}

void IcaoLdapSyncService::initValidation() {
    if (trustChainBuilder_) return;  // Already initialized

    if (!certRepo_ || !crlRepo_) {
        spdlog::warn("[IcaoLdapSync] Cannot init validation: certRepo or crlRepo is null");
        return;
    }

    cscaProvider_ = std::make_unique<adapters::RelayCscaProvider>(certRepo_);
    crlProvider_ = std::make_unique<adapters::RelayCrlProvider>(crlRepo_);
    trustChainBuilder_ = std::make_unique<icao::validation::TrustChainBuilder>(cscaProvider_.get());
    crlChecker_ = std::make_unique<icao::validation::CrlChecker>(crlProvider_.get());

    spdlog::info("[IcaoLdapSync] Validation components initialized (TrustChainBuilder + CrlChecker)");
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
        // Create client based on auth mode
        std::unique_ptr<IcaoLdapClient> clientPtr;
        if (config_.icaoLdapUseTls) {
            IcaoLdapTlsConfig tlsCfg;
            tlsCfg.enabled = true;
            tlsCfg.certFile = config_.icaoLdapTlsCertFile;
            tlsCfg.keyFile = config_.icaoLdapTlsKeyFile;
            tlsCfg.caCertFile = config_.icaoLdapTlsCaCertFile;
            // Use Simple Bind over TLS if bindDn is configured
            tlsCfg.bindDn = config_.icaoLdapBindDn;
            tlsCfg.bindPassword = config_.icaoLdapBindPassword;
            clientPtr = std::make_unique<IcaoLdapClient>(
                config_.icaoLdapHost, config_.icaoLdapPort,
                config_.icaoLdapBaseDn, tlsCfg);
        } else {
            clientPtr = std::make_unique<IcaoLdapClient>(
                config_.icaoLdapHost, config_.icaoLdapPort,
                config_.icaoLdapBindDn, config_.icaoLdapBindPassword,
                config_.icaoLdapBaseDn);
        }
        auto& client = *clientPtr;

        // Notify: sync started (shown in notification bell)
        notification::NotificationManager::getInstance().broadcast(
            "ICAO_LDAP_SYNC_STARTED",
            "ICAO PKD 동기화 시작",
            config_.icaoLdapHost + ":" + std::to_string(config_.icaoLdapPort) + " 연결 중...");

        if (!client.connect()) {
            result.status = "FAILED";
            result.errorMessage = "Failed to connect to ICAO PKD LDAP at " +
                                 config_.icaoLdapHost + ":" + std::to_string(config_.icaoLdapPort);
            saveSyncLog(result);
            syncRunning_ = false;
            return result;
        }

        // Broadcast: connected
        currentProgress_ = {};
        currentProgress_.phase = "CONNECTING";
        currentProgress_.message = "ICAO PKD LDAP 연결됨, 인증서 수 확인 중...";
        broadcastProgress(currentProgress_);

        result.totalRemoteCount = client.getTotalEntryCount();
        currentProgress_.totalRemoteCount = result.totalRemoteCount;
        spdlog::info("[IcaoLdapSync] Total entries in ICAO PKD: {}", result.totalRemoteCount);

        // P1: Load fingerprint cache for O(1) duplicate check
        loadFingerprintCache();

        // Sync helper lambda with per-entry progress broadcast
        int typeIndex = 0;
        bool syncAborted = false;

        auto syncEntries = [&](const std::string& typeName,
                               std::vector<IcaoLdapCertEntry> (IcaoLdapClient::*searchFn)(int)) {
            if (syncAborted) return;

            // Reconnect before each type (LDAP idle timeout prevention during long processing)
            client.disconnect();
            if (!client.connect()) {
                spdlog::error("[IcaoLdapSync] Reconnect failed for {} search", typeName);
                syncAborted = true;
                result.status = "FAILED";
                result.errorMessage = "LDAP reconnect failed before " + typeName + " search";
                return;
            }

            // Phase: searching
            currentProgress_.phase = "SEARCHING";
            currentProgress_.currentType = typeName;
            currentProgress_.message = typeName + " 인증서 검색 중...";
            broadcastProgress(currentProgress_);

            auto entries = (client.*searchFn)(0);

            // Phase: processing
            currentProgress_.phase = "PROCESSING";
            currentProgress_.currentTypeTotal = static_cast<int>(entries.size());
            currentProgress_.currentTypeProcessed = 0;
            currentProgress_.currentTypeNew = 0;
            currentProgress_.currentTypeSkipped = 0;
            currentProgress_.message = typeName + " " + std::to_string(entries.size()) + "건 처리 중...";
            broadcastProgress(currentProgress_);

            spdlog::info("[IcaoLdapSync] Processing {} {} entries...", entries.size(), typeName);

            int consecutiveFailures = 0;
            constexpr int MAX_CONSECUTIVE_FAILURES = 50;

            for (const auto& entry : entries) {
                try {
                    auto er = processEntry(entry);
                    if (er == EntryResult::NEW) {
                        result.newCertificates++;
                        currentProgress_.currentTypeNew++;
                        currentProgress_.totalNew++;
                        consecutiveFailures = 0;
                    } else if (er == EntryResult::SKIPPED) {
                        result.existingSkipped++;
                        currentProgress_.currentTypeSkipped++;
                        currentProgress_.totalSkipped++;
                        consecutiveFailures = 0;
                    } else {
                        result.failedCount++;
                        currentProgress_.totalFailed++;
                        consecutiveFailures++;
                    }
                } catch (const std::exception& e) {
                    result.failedCount++;
                    currentProgress_.totalFailed++;
                    consecutiveFailures++;
                    spdlog::warn("[IcaoLdapSync] Failed to process {}: {}", entry.dn, e.what());
                }

                // Abort if too many consecutive failures (likely systemic error)
                if (consecutiveFailures >= MAX_CONSECUTIVE_FAILURES) {
                    std::string abortMsg = typeName + " 연속 " + std::to_string(MAX_CONSECUTIVE_FAILURES) +
                        "건 실패 — 동기화 중단. 재시작하여 이어서 진행 가능합니다.";
                    spdlog::error("[IcaoLdapSync] {}", abortMsg);
                    result.errorMessage = abortMsg;
                    result.status = "FAILED";
                    currentProgress_.phase = "FAILED";
                    currentProgress_.message = abortMsg;
                    broadcastProgress(currentProgress_);
                    syncAborted = true;
                    return;
                }

                currentProgress_.currentTypeProcessed++;

                // Broadcast every 200 entries or on last entry
                if (currentProgress_.currentTypeProcessed % 200 == 0 ||
                    currentProgress_.currentTypeProcessed == currentProgress_.currentTypeTotal) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now() - result.startedAt).count();
                    currentProgress_.elapsedMs = static_cast<int>(elapsed);
                    currentProgress_.message = typeName + " " +
                        std::to_string(currentProgress_.currentTypeProcessed) + "/" +
                        std::to_string(currentProgress_.currentTypeTotal) + " 처리 완료 (신규: " +
                        std::to_string(currentProgress_.currentTypeNew) +
                        (currentProgress_.totalFailed > 0
                            ? ", 실패: " + std::to_string(currentProgress_.totalFailed)
                            : "") + ")";
                    broadcastProgress(currentProgress_);
                }
            }

            // Collect per-type stats
            IcaoLdapTypeStat typeStat;
            typeStat.type = typeName;
            typeStat.total = currentProgress_.currentTypeTotal;
            typeStat.newCount = currentProgress_.currentTypeNew;
            typeStat.skipped = currentProgress_.currentTypeSkipped;
            typeStat.failed = currentProgress_.currentTypeTotal -
                             currentProgress_.currentTypeNew - currentProgress_.currentTypeSkipped;
            result.typeStats.push_back(typeStat);

            typeIndex++;
            currentProgress_.completedTypes = typeIndex;
        };

        // CSCA: extracted from Master Lists (ICAO PKD has no o=csca branch)
        {
            currentProgress_.phase = "SEARCHING";
            currentProgress_.currentType = "ML→CSCA";
            currentProgress_.message = "Master List 검색 중 (CSCA 추출)...";
            broadcastProgress(currentProgress_);

            auto mlEntries = client.searchMasterLists(0);
            spdlog::info("[IcaoLdapSync] Found {} Master List entries, extracting CSCAs...", mlEntries.size());

            currentProgress_.phase = "PROCESSING";
            currentProgress_.currentTypeTotal = static_cast<int>(mlEntries.size());
            currentProgress_.currentTypeProcessed = 0;
            currentProgress_.currentTypeNew = 0;
            currentProgress_.currentTypeSkipped = 0;
            currentProgress_.message = "ML→CSCA " + std::to_string(mlEntries.size()) + "건 처리 중...";
            broadcastProgress(currentProgress_);

            for (const auto& mlEntry : mlEntries) {
                try {
                    int newCscas = processMasterListEntry(mlEntry);
                    result.newCertificates += newCscas;
                    currentProgress_.currentTypeNew += newCscas;
                    currentProgress_.totalNew += newCscas;
                    if (newCscas == 0) {
                        result.existingSkipped++;
                        currentProgress_.currentTypeSkipped++;
                        currentProgress_.totalSkipped++;
                    }
                } catch (const std::exception& e) {
                    result.failedCount++;
                    currentProgress_.totalFailed++;
                    spdlog::warn("[IcaoLdapSync] Failed to process ML {}: {}", mlEntry.dn, e.what());
                }
                currentProgress_.currentTypeProcessed++;
                if (currentProgress_.currentTypeProcessed % 10 == 0 ||
                    currentProgress_.currentTypeProcessed == currentProgress_.currentTypeTotal) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now() - result.startedAt).count();
                    currentProgress_.elapsedMs = static_cast<int>(elapsed);
                    currentProgress_.message = "ML→CSCA " +
                        std::to_string(currentProgress_.currentTypeProcessed) + "/" +
                        std::to_string(currentProgress_.currentTypeTotal) + " (신규 CSCA: " +
                        std::to_string(currentProgress_.currentTypeNew) + ")";
                    broadcastProgress(currentProgress_);
                }
            }

            // Collect ML→CSCA type stats
            IcaoLdapTypeStat mlStat;
            mlStat.type = "ML→CSCA";
            mlStat.total = currentProgress_.currentTypeTotal;
            mlStat.newCount = currentProgress_.currentTypeNew;
            mlStat.skipped = currentProgress_.currentTypeSkipped;
            result.typeStats.push_back(mlStat);

            typeIndex++;
            currentProgress_.completedTypes = typeIndex;
        }

        // Order: CSCA(above) → CRL → DSC → DSC_NC
        // CRL before DSC so Trust Chain validation can check revocation
        syncEntries("CRL", &IcaoLdapClient::searchCrls);
        syncEntries("DSC", &IcaoLdapClient::searchDscCertificates);
        syncEntries("DSC_NC", &IcaoLdapClient::searchNcDscCertificates);

        // Flush remaining batches
        flushDuplicateBatch();
        flushValidationBatch();
        fingerprintCache_.clear(); // Free memory

        client.disconnect();

        if (syncAborted) {
            // Already set result.status = "FAILED" in lambda
            result.completedAt = std::chrono::system_clock::now();
            result.durationMs = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    result.completedAt - result.startedAt).count());
            saveSyncLog(result);
            syncRunning_ = false;
            return result;
        }

        result.status = "COMPLETED";
        result.completedAt = std::chrono::system_clock::now();
        result.durationMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                result.completedAt - result.startedAt).count());

        spdlog::info("[IcaoLdapSync] Sync completed: new={}, skipped={}, failed={}, duration={}ms",
                    result.newCertificates, result.existingSkipped, result.failedCount, result.durationMs);

        // Broadcast final progress COMPLETED (must come BEFORE the notification bell event)
        currentProgress_.phase = "COMPLETED";
        currentProgress_.elapsedMs = result.durationMs;
        currentProgress_.message = "동기화 완료 — 신규 " + std::to_string(result.newCertificates) +
            "건, " + std::to_string(result.durationMs / 1000) + "초 소요";
        broadcastProgress(currentProgress_);

        // Notify: sync completed (shown in notification bell)
        notification::NotificationManager::getInstance().broadcast(
            "ICAO_LDAP_SYNC_COMPLETED",
            "ICAO PKD 동기화 완료",
            "신규 " + std::to_string(result.newCertificates) + "건, " +
            std::to_string(result.durationMs / 1000) + "초 소요");

    } catch (const std::exception& e) {
        result.status = "FAILED";
        result.errorMessage = e.what();
        result.completedAt = std::chrono::system_clock::now();
        result.durationMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                result.completedAt - result.startedAt).count());
        spdlog::error("[IcaoLdapSync] Sync failed: {}", e.what());

        // Notify: sync failed (shown in notification bell)
        notification::NotificationManager::getInstance().broadcast(
            "ICAO_LDAP_SYNC_FAILED",
            "ICAO PKD 동기화 실패",
            result.errorMessage);
    }

    saveSyncLog(result);

    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        lastResult_ = result;
    }

    syncRunning_ = false;
    return result;
}

IcaoLdapSyncService::EntryResult IcaoLdapSyncService::processEntry(const IcaoLdapCertEntry& entry) {
    if (entry.binaryData.empty()) return EntryResult::FAILED;

    auto fingerprint = computeFingerprint(entry.binaryData);
    if (fingerprint.empty()) return EntryResult::FAILED;

    if (fingerprintExists(fingerprint)) {
        // P0: Batch duplicate recording (flush every 500)
        // Look up certificate ID for duplicate tracking
        try {
            std::string idQuery = (entry.certType == "CRL")
                ? "SELECT id FROM crl WHERE fingerprint_sha256 = $1"
                : "SELECT id FROM certificate WHERE fingerprint_sha256 = $1";
            auto idResult = queryExecutor_->executeQuery(idQuery, {fingerprint});
            if (!idResult.empty()) {
                std::string certId = idResult[0].get("id", "").asString();
                if (!certId.empty()) {
                    duplicateBatch_.push_back({certId, "ICAO_PKD_SYNC", "ICAO_PKD_SYNC", entry.countryCode});
                    if (duplicateBatch_.size() >= 500) flushDuplicateBatch();
                }
            }
        } catch (...) {}
        return EntryResult::SKIPPED;
    }

    bool saved = false;
    if (entry.certType == "CRL") {
        saved = saveCrlToDb(entry, fingerprint);
        if (saved) {
            saveCrlToLocalLdap(entry, fingerprint);
            fingerprintCache_.insert(fingerprint); // Update cache
        }
    } else {
        saved = saveCertificateToDb(entry, fingerprint);
        if (saved) {
            saveCertificateToLocalLdap(entry, fingerprint);
            fingerprintCache_.insert(fingerprint); // Update cache

            // P3: Queue validation for batch processing (DSC/DSC_NC only)
            if (entry.certType == "DSC" || entry.certType == "DSC_NC") {
                validationBatch_.push_back({fingerprint, entry, entry.binaryData});
                if (validationBatch_.size() >= 100) flushValidationBatch();
            }
        }
    }

    return saved ? EntryResult::NEW : EntryResult::FAILED;
}

void IcaoLdapSyncService::validateAndSaveResult(const IcaoLdapCertEntry& entry,
                                                const std::string& fingerprint,
                                                X509* cert) {
    if (!validationRepo_ || !cert) {
        if (!validationRepo_) spdlog::warn("[IcaoLdapSync] validateAndSaveResult skipped: validationRepo is null (first call only)");
        return;
    }

    try {
        // Lazy-init validation components
        initValidation();
        if (!trustChainBuilder_ || !crlChecker_) {
            spdlog::warn("[IcaoLdapSync] validateAndSaveResult skipped: validation components not initialized");
            return;
        }

        std::string dbType = queryExecutor_->getDatabaseType();

        // Step 1: Build trust chain (DSC → CSCA)
        std::string validationStatus = "PENDING";
        std::string validationMessage = "Trust chain not verified";
        bool cscaFound = false;
        std::string trustChainPath;

        try {
            auto chainResult = trustChainBuilder_->build(cert);
            if (chainResult.valid) {
                validationStatus = "VALID";
                validationMessage = "Trust chain verified";
                cscaFound = true;
                trustChainPath = chainResult.path;
            } else {
                validationStatus = !chainResult.cscaSubjectDn.empty() ? "INVALID" : "PENDING";
                validationMessage = chainResult.message;
                cscaFound = !chainResult.cscaSubjectDn.empty();
            }
        } catch (const std::exception& e) {
            validationMessage = std::string("Trust chain error: ") + e.what();
        }

        // Step 2: CRL check (only if trust chain is valid)
        if (validationStatus == "VALID") {
            try {
                auto crlResult = crlChecker_->check(cert, entry.countryCode);
                if (crlResult.status == icao::validation::CrlCheckStatus::REVOKED) {
                    validationStatus = "INVALID";
                    validationMessage = "Certificate revoked (CRL): " + crlResult.revocationReason;
                }
            } catch (...) {
                /* CRL check failure is non-fatal */
            }
        }

        // Step 3: Check expiration
        if (validationStatus == "VALID") {
            // Check notAfter
            const ASN1_TIME* notAfter = X509_get0_notAfter(cert);
            if (notAfter && X509_cmp_current_time(notAfter) < 0) {
                validationStatus = "EXPIRED_VALID";
                validationMessage = "Certificate expired but trust chain valid";
            }
        }

        // Step 4: ICAO Doc 9303 compliance check
        auto icao = icao::validation::checkIcaoCompliance(cert, entry.certType);

        // Step 5: Check validity period (not_after vs now)
        bool validityPeriodValid = true;
        {
            const ASN1_TIME* na = X509_get0_notAfter(cert);
            if (na && X509_cmp_current_time(na) < 0) {
                validityPeriodValid = false;
            }
        }

        // Step 6: Check signature validity (self-verify for self-signed, otherwise skip)
        bool signatureValid = (validationStatus == "VALID" || validationStatus == "EXPIRED_VALID");

        // Extract serial number
        std::string serialNumber;
        {
            ASN1_INTEGER* serial = X509_get_serialNumber(cert);
            if (serial) {
                BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
                if (bn) {
                    char* hex = BN_bn2hex(bn);
                    if (hex) { serialNumber = hex; OPENSSL_free(hex); }
                    BN_free(bn);
                }
            }
        }

        // Extract sig algorithm
        std::string sigAlgName;
        {
            const X509_ALGOR* algor = nullptr;
            X509_get0_signature(nullptr, &algor, cert);
            if (algor) {
                int nid = OBJ_obj2nid(algor->algorithm);
                sigAlgName = (nid != NID_undef) ? OBJ_nid2sn(nid) : "unknown";
            }
        }

        // Expiry dates
        std::string notBeforeStr, notAfterStr;
        {
            auto asn1ToIso = [](const ASN1_TIME* t) -> std::string {
                if (!t) return "";
                struct tm tm = {};
                if (ASN1_TIME_to_tm(t, &tm)) {
                    char buf[32];
                    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
                    return buf;
                }
                return "";
            };
            notBeforeStr = asn1ToIso(X509_get0_notBefore(cert));
            notAfterStr = asn1ToIso(X509_get0_notAfter(cert));
        }

        // CSCA subject DN (for trust chain)
        std::string cscaSubjectDn, cscaSerial, cscaCountry;
        if (cscaFound) {
            try {
                auto chainResult2 = trustChainBuilder_->build(cert);
                cscaSubjectDn = chainResult2.cscaSubjectDn;
            } catch (...) {}
        }

        // Extract subject/issuer from cert
        bool trustChainValid2 = (validationStatus == "VALID" || validationStatus == "EXPIRED_VALID");
        char vrSubject[512] = {0}, vrIssuer[512] = {0};
        X509_NAME_oneline(X509_get_subject_name(cert), vrSubject, sizeof(vrSubject));
        X509_NAME_oneline(X509_get_issuer_name(cert), vrIssuer, sizeof(vrIssuer));

        // Revocation status
        std::string revocationStatus = "NOT_CHECKED";
        if (validationStatus == "VALID" || validationStatus == "EXPIRED_VALID") {
            revocationStatus = "NOT_REVOKED";
        }
        // Check CRL result for revocation
        bool crlChecked = false;
        try {
            auto crlRes = crlChecker_->check(cert, entry.countryCode);
            crlChecked = true;
            if (crlRes.status == icao::validation::CrlCheckStatus::REVOKED) {
                revocationStatus = "REVOKED";
            }
        } catch (...) {}

        // Violations string
        std::string violationsStr = icao.violationsString();

        // Save to validation_result — full schema
        std::string sql;
        if (dbType == "oracle") {
            sql = "INSERT INTO validation_result (id, certificate_id, upload_id, "
                  "certificate_type, country_code, subject_dn, issuer_dn, serial_number, "
                  "validation_status, trust_chain_valid, trust_chain_message, csca_found, "
                  "csca_subject_dn, "
                  "signature_valid, signature_algorithm, "
                  "validity_period_valid, not_before, not_after, "
                  "revocation_status, crl_checked, "
                  "icao_compliant, icao_compliance_level, icao_violations, "
                  "icao_key_usage_compliant, icao_algorithm_compliant, "
                  "icao_key_size_compliant, icao_validity_period_compliant, "
                  "icao_extensions_compliant, "
                  "validation_timestamp) "
                  "VALUES (SYS_GUID(), $1, $2, $3, $4, $5, $6, $7, $8, "
                  + common::db::boolLiteral(dbType, trustChainValid2) + ", $9, "
                  + common::db::boolLiteral(dbType, cscaFound) + ", $10, "
                  + common::db::boolLiteral(dbType, signatureValid) + ", $11, "
                  + common::db::boolLiteral(dbType, validityPeriodValid) + ", $12, $13, "
                  "$14, " + common::db::boolLiteral(dbType, crlChecked) + ", "
                  + common::db::boolLiteral(dbType, icao.isCompliant) + ", $15, $16, "
                  + common::db::boolLiteral(dbType, icao.keyUsageCompliant) + ", "
                  + common::db::boolLiteral(dbType, icao.algorithmCompliant) + ", "
                  + common::db::boolLiteral(dbType, icao.keySizeCompliant) + ", "
                  + common::db::boolLiteral(dbType, icao.validityPeriodCompliant) + ", "
                  + common::db::boolLiteral(dbType, icao.extensionsCompliant) + ", "
                  + common::db::currentTimestamp(dbType) + ")";
        } else {
            sql = "INSERT INTO validation_result (id, certificate_id, upload_id, "
                  "certificate_type, country_code, subject_dn, issuer_dn, serial_number, "
                  "validation_status, trust_chain_valid, trust_chain_message, csca_found, "
                  "csca_subject_dn, "
                  "signature_valid, signature_algorithm, "
                  "validity_period_valid, not_before, not_after, "
                  "revocation_status, crl_checked, "
                  "icao_compliant, icao_compliance_level, icao_violations, "
                  "icao_key_usage_compliant, icao_algorithm_compliant, "
                  "icao_key_size_compliant, icao_validity_period_compliant, "
                  "icao_extensions_compliant, "
                  "validation_timestamp) "
                  "VALUES (gen_random_uuid(), $1, $2, $3, $4, $5, $6, $7, $8, "
                  "$9, $10, $11, $12, $13, $14, $15, $16, $17, $18, $19, "
                  "$20, $21, $22, $23, $24, $25, $26, $27, NOW()) "
                  "ON CONFLICT (certificate_id, upload_id) DO NOTHING";
        }

        std::vector<std::string> params = {
            fingerprint,                              // $1 certificate_id
            std::string("ICAO_PKD_SYNC"),            // $2 upload_id
            entry.certType,                           // $3 certificate_type
            entry.countryCode,                        // $4 country_code
            std::string(vrSubject),                   // $5 subject_dn
            std::string(vrIssuer),                    // $6 issuer_dn
            serialNumber,                             // $7 serial_number
            validationStatus,                         // $8 validation_status
        };

        if (dbType == "oracle") {
            // Oracle: booleans are boolLiterals in SQL, only string params here
            params.push_back(validationMessage);      // $9 trust_chain_message
            params.push_back(cscaSubjectDn);          // $10 csca_subject_dn
            params.push_back(sigAlgName);             // $11 signature_algorithm
            params.push_back(notBeforeStr);           // $12 not_before
            params.push_back(notAfterStr);            // $13 not_after
            params.push_back(revocationStatus);       // $14 revocation_status
            params.push_back(icao.complianceLevel);   // $15 icao_compliance_level
            params.push_back(violationsStr);          // $16 icao_violations
        } else {
            // PostgreSQL: all params including booleans
            params.push_back(trustChainValid2 ? "true" : "false"); // $9
            params.push_back(validationMessage);      // $10
            params.push_back(cscaFound ? "true" : "false"); // $11
            params.push_back(cscaSubjectDn);          // $12
            params.push_back(signatureValid ? "true" : "false"); // $13
            params.push_back(sigAlgName);             // $14
            params.push_back(validityPeriodValid ? "true" : "false"); // $15
            params.push_back(notBeforeStr);           // $16
            params.push_back(notAfterStr);            // $17
            params.push_back(revocationStatus);       // $18
            params.push_back(crlChecked ? "true" : "false"); // $19
            params.push_back(icao.isCompliant ? "true" : "false"); // $20
            params.push_back(icao.complianceLevel);   // $21
            params.push_back(violationsStr);          // $22
            params.push_back(icao.keyUsageCompliant ? "true" : "false"); // $23
            params.push_back(icao.algorithmCompliant ? "true" : "false"); // $24
            params.push_back(icao.keySizeCompliant ? "true" : "false"); // $25
            params.push_back(icao.validityPeriodCompliant ? "true" : "false"); // $26
            params.push_back(icao.extensionsCompliant ? "true" : "false"); // $27
        }
        queryExecutor_->executeQuery(sql, params);

        // Update certificate validation_status
        queryExecutor_->executeQuery(
            "UPDATE certificate SET validation_status = $1 WHERE fingerprint_sha256 = $2",
            {validationStatus, fingerprint});

    } catch (const std::exception& e) {
        spdlog::warn("[IcaoLdapSync] validateAndSaveResult failed for {}: {}", fingerprint, e.what());
    }
}

void IcaoLdapSyncService::saveDuplicateRecord(const IcaoLdapCertEntry& entry,
                                               const std::string& fingerprint) {
    if (!queryExecutor_) return;

    try {
        std::string dbType = queryExecutor_->getDatabaseType();

        // Look up certificate.id from fingerprint (certificate_duplicates uses certificate.id as FK)
        std::string certIdQuery = (entry.certType == "CRL")
            ? "SELECT id FROM crl WHERE fingerprint_sha256 = $1"
            : "SELECT id FROM certificate WHERE fingerprint_sha256 = $1";

        auto idResult = queryExecutor_->executeQuery(certIdQuery, {fingerprint});
        if (idResult.empty()) return;
        std::string certId = idResult[0].get("id", "").asString();
        if (certId.empty()) return;

        // Insert into certificate_duplicates (same table as upload issues API)
        std::string sql;
        if (dbType == "oracle") {
            sql = "INSERT INTO certificate_duplicates "
                  "(id, certificate_id, upload_id, source_type, source_country, detected_at) "
                  "VALUES (SEQ_CERT_DUPLICATES.NEXTVAL, $1, $2, $3, $4, " + common::db::currentTimestamp(dbType) + ")";
        } else {
            sql = "INSERT INTO certificate_duplicates "
                  "(certificate_id, upload_id, source_type, source_country, detected_at) "
                  "VALUES ($1, $2, $3, $4, NOW()) "
                  "ON CONFLICT DO NOTHING";
        }

        queryExecutor_->executeQuery(sql, {
            certId,
            std::string("ICAO_PKD_SYNC"),
            std::string("ICAO_PKD_SYNC"),
            entry.countryCode
        });
    } catch (const std::exception& e) {
        // Non-fatal but log for debugging
        static int dupErrorCount = 0;
        if (dupErrorCount++ < 3) {
            spdlog::warn("[IcaoLdapSync] saveDuplicateRecord failed: {}", e.what());
        }
    }
}

// =============================================================================
// P1: Fingerprint cache — load all fingerprints at sync start
// =============================================================================

void IcaoLdapSyncService::loadFingerprintCache() {
    fingerprintCache_.clear();
    if (!queryExecutor_) return;

    try {
        auto certResult = queryExecutor_->executeQuery(
            "SELECT fingerprint_sha256 FROM certificate");
        for (const auto& row : certResult) {
            fingerprintCache_.insert(row.get("fingerprint_sha256", "").asString());
        }

        auto crlResult = queryExecutor_->executeQuery(
            "SELECT fingerprint_sha256 FROM crl");
        for (const auto& row : crlResult) {
            fingerprintCache_.insert(row.get("fingerprint_sha256", "").asString());
        }

        spdlog::info("[IcaoLdapSync] Fingerprint cache loaded: {} entries", fingerprintCache_.size());
    } catch (const std::exception& e) {
        spdlog::error("[IcaoLdapSync] Failed to load fingerprint cache: {}", e.what());
    }
}

// =============================================================================
// P0: Batch duplicate INSERT
// =============================================================================

void IcaoLdapSyncService::flushDuplicateBatch() {
    if (duplicateBatch_.empty() || !queryExecutor_) return;

    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        for (const auto& dup : duplicateBatch_) {
            std::string sql;
            if (dbType == "oracle") {
                sql = "INSERT INTO certificate_duplicates "
                      "(id, certificate_id, upload_id, source_type, source_country, detected_at) "
                      "VALUES (SEQ_CERT_DUPLICATES.NEXTVAL, $1, $2, $3, $4, "
                      + common::db::currentTimestamp(dbType) + ")";
            } else {
                sql = "INSERT INTO certificate_duplicates "
                      "(certificate_id, upload_id, source_type, source_country, detected_at) "
                      "VALUES ($1, $2, $3, $4, NOW()) "
                      "ON CONFLICT DO NOTHING";
            }
            queryExecutor_->executeQuery(sql, {
                dup.certId, dup.uploadId, dup.sourceType, dup.countryCode
            });
        }
    } catch (const std::exception& e) {
        spdlog::warn("[IcaoLdapSync] flushDuplicateBatch failed: {}", e.what());
    }
    duplicateBatch_.clear();
}

// =============================================================================
// P3: Batch validation result flush
// =============================================================================

void IcaoLdapSyncService::flushValidationBatch() {
    if (validationBatch_.empty()) return;

    for (auto& ve : validationBatch_) {
        const uint8_t* p = ve.derData.data();
        X509* cert = d2i_X509(nullptr, &p, static_cast<long>(ve.derData.size()));
        if (cert) {
            validateAndSaveResult(ve.entry, ve.fingerprint, cert);
            X509_free(cert);
        }
    }
    validationBatch_.clear();
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
    // P1: Use in-memory cache (loaded at sync start) — O(1) lookup, 0 DB queries
    if (!fingerprintCache_.empty()) {
        return fingerprintCache_.count(fingerprint) > 0;
    }

    // Fallback: DB query (only if cache not loaded)
    if (!queryExecutor_) return false;
    try {
        auto result = queryExecutor_->executeQuery(
            "SELECT COUNT(*) AS cnt FROM certificate WHERE fingerprint_sha256 = $1",
            {fingerprint});
        if (!result.empty() && common::db::scalarToInt(result[0]["cnt"]) > 0) return true;

        auto crlResult = queryExecutor_->executeQuery(
            "SELECT COUNT(*) AS cnt FROM crl WHERE fingerprint_sha256 = $1",
            {fingerprint});
        if (!crlResult.empty() && common::db::scalarToInt(crlResult[0]["cnt"]) > 0) return true;
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

        // --- Extract 22 X.509 metadata fields ---
        char subjectBuf[512] = {0}, issuerBuf[512] = {0};
        X509_NAME_oneline(X509_get_subject_name(cert), subjectBuf, sizeof(subjectBuf));
        X509_NAME_oneline(X509_get_issuer_name(cert), issuerBuf, sizeof(issuerBuf));

        // Version
        int version = X509_get_version(cert);

        // Serial number
        std::string serialNumber;
        {
            ASN1_INTEGER* serial = X509_get_serialNumber(cert);
            if (serial) {
                BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
                if (bn) {
                    char* hex = BN_bn2hex(bn);
                    if (hex) { serialNumber = hex; OPENSSL_free(hex); }
                    BN_free(bn);
                }
            }
        }

        // Signature algorithm
        std::string sigAlg;
        {
            const X509_ALGOR* algor = nullptr;
            X509_get0_signature(nullptr, &algor, cert);
            if (algor) {
                int nid = OBJ_obj2nid(algor->algorithm);
                sigAlg = (nid != NID_undef) ? OBJ_nid2sn(nid) : "unknown";
            }
        }

        // Public key algorithm + size
        std::string pubKeyAlg;
        int pubKeySize = 0;
        {
            EVP_PKEY* pkey = X509_get0_pubkey(cert);
            if (pkey) {
                int pkType = EVP_PKEY_base_id(pkey);
                if (pkType == EVP_PKEY_RSA) pubKeyAlg = "RSA";
                else if (pkType == EVP_PKEY_EC) pubKeyAlg = "ECDSA";
                else if (pkType == EVP_PKEY_DSA) pubKeyAlg = "DSA";
                else pubKeyAlg = OBJ_nid2sn(pkType);
                pubKeySize = EVP_PKEY_bits(pkey);
            }
        }

        // Validity dates (ISO 8601 format for Oracle TIMESTAMP compatibility)
        std::string notBefore, notAfter;
        {
            auto asn1ToIso = [](const ASN1_TIME* t) -> std::string {
                if (!t) return "";
                struct tm tm = {};
                if (ASN1_TIME_to_tm(t, &tm)) {
                    char buf[32];
                    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
                    return buf;
                }
                return "";
            };
            notBefore = asn1ToIso(X509_get0_notBefore(cert));
            notAfter = asn1ToIso(X509_get0_notAfter(cert));
        }

        // Self-signed check
        bool isSelfSigned = (X509_name_cmp(X509_get_subject_name(cert), X509_get_issuer_name(cert)) == 0);

        // DER hex for storage
        std::ostringstream hexStream;
        for (auto b : entry.binaryData) {
            hexStream << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
        }

        std::string dbType = queryExecutor_->getDatabaseType();
        std::string certType = entry.certType;

        // --- Insert with full metadata ---
        std::string sql;
        if (dbType == "oracle") {
            sql = "INSERT INTO certificate (id, fingerprint_sha256, certificate_type, "
                  "country_code, subject_dn, issuer_dn, certificate_data, source_type, "
                  "version, serial_number, signature_algorithm, "
                  "public_key_algorithm, public_key_size, "
                  "not_before, not_after, is_self_signed, "
                  "stored_in_ldap, created_at) "
                  "VALUES (SYS_GUID(), $1, $2, $3, $4, $5, $6, $7, "
                  "$8, $9, $10, $11, $12, $13, $14, "
                  + common::db::boolLiteral(dbType, isSelfSigned) + ", "
                  + common::db::boolLiteral(dbType, false) + ", "
                  + common::db::currentTimestamp(dbType) + ")";
        } else {
            sql = "INSERT INTO certificate (id, fingerprint_sha256, certificate_type, "
                  "country_code, subject_dn, issuer_dn, certificate_data, source_type, "
                  "version, serial_number, signature_algorithm, "
                  "public_key_algorithm, public_key_size, "
                  "not_before, not_after, is_self_signed, "
                  "stored_in_ldap, created_at) "
                  "VALUES (gen_random_uuid(), $1, $2, $3, $4, $5, decode($6, 'hex'), $7, "
                  "$8, $9, $10, $11, $12, $13, $14, $15, "
                  "FALSE, NOW()) "
                  "ON CONFLICT (fingerprint_sha256) DO NOTHING";
        }

        std::vector<std::string> params = {
            fingerprint, certType, entry.countryCode,
            std::string(subjectBuf), std::string(issuerBuf),
            hexStream.str(), std::string("ICAO_PKD_SYNC"),
            std::to_string(version), serialNumber, sigAlg,
            pubKeyAlg, std::to_string(pubKeySize),
            notBefore, notAfter
        };
        // PostgreSQL: $15 = is_self_signed (Oracle uses boolLiteral in SQL)
        if (dbType != "oracle") {
            params.push_back(isSelfSigned ? "true" : "false");
        }
        queryExecutor_->executeQuery(sql, params);

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
    if (entry.binaryData.empty()) return false;  // Skip empty CRL data (ORA-24333 prevention)

    try {
        // Parse CRL to extract metadata
        std::string issuerDn, thisUpdate, nextUpdate, crlNumber;
        {
            const uint8_t* p = entry.binaryData.data();
            X509_CRL* crl = d2i_X509_CRL(nullptr, &p, static_cast<long>(entry.binaryData.size()));
            if (crl) {
                char* iss = X509_NAME_oneline(X509_CRL_get_issuer(crl), nullptr, 0);
                if (iss) { issuerDn = iss; OPENSSL_free(iss); }

                auto asn1ToIso = [](const ASN1_TIME* t) -> std::string {
                    if (!t) return "";
                    struct tm tm = {};
                    if (ASN1_TIME_to_tm(t, &tm)) {
                        char buf[32];
                        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
                        return buf;
                    }
                    return "";
                };
                thisUpdate = asn1ToIso(X509_CRL_get0_lastUpdate(crl));
                nextUpdate = asn1ToIso(X509_CRL_get0_nextUpdate(crl));

                ASN1_INTEGER* crlNum = static_cast<ASN1_INTEGER*>(
                    X509_CRL_get_ext_d2i(crl, NID_crl_number, nullptr, nullptr));
                if (crlNum) {
                    long num = ASN1_INTEGER_get(crlNum);
                    crlNumber = std::to_string(num);
                    ASN1_INTEGER_free(crlNum);
                }

                X509_CRL_free(crl);
            }
        }
        if (issuerDn.empty()) issuerDn = "unknown";
        if (thisUpdate.empty()) thisUpdate = "1970-01-01 00:00:00";

        std::ostringstream hexStream;
        for (auto b : entry.binaryData) {
            hexStream << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
        }
        std::string hexData = hexStream.str();
        if (hexData.empty()) return false;

        std::string dbType = queryExecutor_->getDatabaseType();

        std::string sql;
        if (dbType == "oracle") {
            sql = "INSERT INTO crl (id, fingerprint_sha256, country_code, issuer_dn, "
                  "this_update, next_update, crl_number, crl_binary, "
                  "upload_id, stored_in_ldap, created_at) "
                  "VALUES (SYS_GUID(), $1, $2, $3, $4, $5, $6, $7, $8, "
                  + common::db::boolLiteral(dbType, false) + ", "
                  + common::db::currentTimestamp(dbType) + ")";
        } else {
            sql = "INSERT INTO crl (id, fingerprint_sha256, country_code, issuer_dn, "
                  "this_update, next_update, crl_number, crl_binary, "
                  "upload_id, stored_in_ldap, created_at) "
                  "VALUES (gen_random_uuid(), $1, $2, $3, $4, $5, $6, decode($7, 'hex'), $8, "
                  "FALSE, NOW()) "
                  "ON CONFLICT (fingerprint_sha256) DO NOTHING";
        }

        queryExecutor_->executeQuery(sql, {
            fingerprint, entry.countryCode, issuerDn,
            thisUpdate, nextUpdate, crlNumber, hexData,
            std::string("ICAO_PKD_SYNC")
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

        // Build error_message with type stats JSON (for detail view)
        std::string errorMsg = result.errorMessage;
        if (!result.typeStats.empty()) {
            Json::Value statsJson(Json::arrayValue);
            for (const auto& ts : result.typeStats) {
                Json::Value item;
                item["type"] = ts.type;
                item["total"] = ts.total;
                item["new"] = ts.newCount;
                item["skipped"] = ts.skipped;
                item["failed"] = ts.failed;
                statsJson.append(item);
            }
            Json::StreamWriterBuilder writer;
            writer["indentation"] = "";
            std::string statsStr = Json::writeString(writer, statsJson);
            // Prepend type stats to error_message (separated by |)
            errorMsg = "STATS:" + statsStr + (errorMsg.empty() ? "" : "|" + errorMsg);
        }
        if (errorMsg.empty()) errorMsg = " ";  // Oracle empty string = NULL

        queryExecutor_->executeQuery(sql, {
            result.syncType, result.status, result.triggeredBy,
            std::to_string(result.totalRemoteCount),
            std::to_string(result.newCertificates),
            std::to_string(result.existingSkipped),
            std::to_string(result.failedCount),
            std::to_string(result.durationMs),
            errorMsg
        });
    } catch (const std::exception& e) {
        spdlog::warn("[IcaoLdapSync] Failed to save sync log: {}", e.what());
    }
}

IcaoLdapSyncResult IcaoLdapSyncService::getLastSyncResult() const {
    {
        std::lock_guard<std::mutex> lock(resultMutex_);
        if (!lastResult_.status.empty()) return lastResult_;
    }

    // Fallback: load from DB (after service restart, in-memory lastResult_ is empty)
    if (!queryExecutor_) return {};

    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string sql = (dbType == "oracle")
            ? "SELECT * FROM (SELECT sync_type, status, triggered_by, total_remote_count, "
              "new_certificates, updated_certificates, failed_count, duration_ms, error_message "
              "FROM icao_ldap_sync_log ORDER BY created_at DESC) WHERE ROWNUM = 1"
            : "SELECT sync_type, status, triggered_by, total_remote_count, "
              "new_certificates, updated_certificates, failed_count, duration_ms, error_message "
              "FROM icao_ldap_sync_log ORDER BY created_at DESC LIMIT 1";

        auto rows = queryExecutor_->executeQuery(sql);
        if (!rows.empty()) {
            IcaoLdapSyncResult result;
            result.syncType = rows[0].get("sync_type", "").asString();
            result.status = rows[0].get("status", "").asString();
            result.triggeredBy = rows[0].get("triggered_by", "").asString();
            result.totalRemoteCount = common::db::getInt(rows[0], "total_remote_count", 0);
            result.newCertificates = common::db::getInt(rows[0], "new_certificates", 0);
            result.existingSkipped = common::db::getInt(rows[0], "updated_certificates", 0);
            result.failedCount = common::db::getInt(rows[0], "failed_count", 0);
            result.durationMs = common::db::getInt(rows[0], "duration_ms", 0);
            result.errorMessage = rows[0].get("error_message", "").asString();
            return result;
        }
    } catch (const std::exception& e) {
        spdlog::warn("[IcaoLdapSync] Failed to load last sync from DB: {}", e.what());
    }
    return {};
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

int IcaoLdapSyncService::getSyncHistoryCount(const std::string& statusFilter) const {
    if (!queryExecutor_) return 0;
    try {
        std::string sql = "SELECT COUNT(*) AS cnt FROM icao_ldap_sync_log";
        std::vector<std::string> params;
        if (!statusFilter.empty()) {
            sql += " WHERE status = $1";
            params.push_back(statusFilter);
        }
        auto result = queryExecutor_->executeQuery(sql, params);
        if (!result.empty()) return common::db::scalarToInt(result[0]["cnt"]);
    } catch (...) {}
    return 0;
}

std::vector<IcaoLdapSyncResult> IcaoLdapSyncService::getSyncHistory(
    int limit, int offset, const std::string& statusFilter) const {
    std::vector<IcaoLdapSyncResult> history;
    if (!queryExecutor_) return history;

    try {
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string sql = "SELECT sync_type, status, triggered_by, total_remote_count, "
            "new_certificates, updated_certificates, failed_count, duration_ms, "
            "error_message, " +
            (dbType == "oracle"
                ? std::string("TO_CHAR(created_at, 'YYYY-MM-DD HH24:MI:SS') as created_at")
                : std::string("TO_CHAR(created_at, 'YYYY-MM-DD HH24:MI:SS') as created_at")) +
            " FROM icao_ldap_sync_log";

        std::vector<std::string> params;
        if (!statusFilter.empty()) {
            sql += " WHERE status = $1";
            params.push_back(statusFilter);
        }
        sql += " ORDER BY created_at DESC";
        sql += " " + common::db::paginationClause(dbType, limit, offset);

        auto rows = queryExecutor_->executeQuery(sql, params);

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
            std::string rawMsg = row.get("error_message", "").asString();
            // Parse type stats from error_message (format: "STATS:[{...}]|actual error")
            if (rawMsg.substr(0, 6) == "STATS:") {
                auto pipePos = rawMsg.find('|', 6);
                std::string statsStr = (pipePos != std::string::npos)
                    ? rawMsg.substr(6, pipePos - 6) : rawMsg.substr(6);
                r.errorMessage = (pipePos != std::string::npos)
                    ? rawMsg.substr(pipePos + 1) : "";

                Json::CharReaderBuilder reader;
                std::istringstream ss(statsStr);
                Json::Value statsJson;
                if (Json::parseFromStream(reader, ss, &statsJson, nullptr) && statsJson.isArray()) {
                    for (const auto& item : statsJson) {
                        IcaoLdapTypeStat ts;
                        ts.type = item.get("type", "").asString();
                        ts.total = item.get("total", 0).asInt();
                        ts.newCount = item.get("new", 0).asInt();
                        ts.skipped = item.get("skipped", 0).asInt();
                        ts.failed = item.get("failed", 0).asInt();
                        r.typeStats.push_back(ts);
                    }
                }
            } else {
                r.errorMessage = rawMsg;
            }
            r.createdAt = row.get("created_at", "").asString();
            history.push_back(std::move(r));
        }
    } catch (const std::exception& e) {
        spdlog::warn("[IcaoLdapSync] getSyncHistory failed: {}", e.what());
    }

    return history;
}

int IcaoLdapSyncService::processMasterListEntry(const IcaoLdapCertEntry& mlEntry) {
    if (mlEntry.binaryData.empty()) return 0;

    // Parse CMS SignedData from Master List binary (pkdMasterListContent)
    BIO* bio = BIO_new_mem_buf(mlEntry.binaryData.data(), static_cast<int>(mlEntry.binaryData.size()));
    if (!bio) return 0;

    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);
    if (!cms) {
        spdlog::warn("[IcaoLdapSync] Failed to parse ML CMS for {}", mlEntry.dn);
        return 0;
    }

    int newCscaCount = 0;

    // Extract embedded content (pkiData containing CSCA certificates)
    ASN1_OCTET_STRING** pContent = CMS_get0_content(cms);
    if (pContent && *pContent && (*pContent)->data && (*pContent)->length > 0) {
        // Parse the encapContentInfo as a SET OF Certificate
        const unsigned char* p = (*pContent)->data;
        long len = (*pContent)->length;

        // Try parsing as ASN.1 SEQUENCE OF Certificate
        STACK_OF(X509)* cscas = nullptr;
        // The content is typically a MasterList ::= SEQUENCE { version, certList SET OF Certificate }
        // Skip version field and parse certificates
        // Use d2i_X509 in a loop to extract individual certificates
        const unsigned char* pos = p;
        const unsigned char* end = p + len;

        // Skip outer SEQUENCE tag + length
        if (pos < end && *pos == 0x30) {
            long outerLen;
            int tag, xclass;
            ASN1_get_object(&pos, &outerLen, &tag, &xclass, end - pos);

            // Skip version INTEGER if present
            if (pos < end && *pos == 0x02) {
                long verLen;
                ASN1_get_object(&pos, &verLen, &tag, &xclass, end - pos);
                pos += verLen;
            }

            // Now parse SET OF Certificate
            if (pos < end && (*pos == 0x31 || *pos == 0x30)) {
                long setLen;
                ASN1_get_object(&pos, &setLen, &tag, &xclass, end - pos);
                const unsigned char* setEnd = pos + setLen;

                while (pos < setEnd) {
                    const unsigned char* certStart = pos;
                    X509* cert = d2i_X509(nullptr, &pos, setEnd - certStart);
                    if (!cert) break;

                    // Compute fingerprint
                    int derLen = i2d_X509(cert, nullptr);
                    std::vector<uint8_t> derData(derLen);
                    unsigned char* dPtr = derData.data();
                    i2d_X509(cert, &dPtr);

                    std::string fingerprint = computeFingerprint(derData);

                    if (!fingerprint.empty() && !fingerprintExists(fingerprint)) {
                        // Determine type: self-signed = CSCA, cross-signed = Link Certificate
                        bool isSelfSigned = (X509_NAME_cmp(
                            X509_get_subject_name(cert), X509_get_issuer_name(cert)) == 0);
                        std::string certType = isSelfSigned ? "CSCA" : "CSCA";  // Both stored as CSCA

                        // Extract country from ML entry
                        IcaoLdapCertEntry cscaEntry;
                        cscaEntry.dn = mlEntry.dn;
                        cscaEntry.countryCode = mlEntry.countryCode;
                        cscaEntry.certType = certType;
                        cscaEntry.binaryData = derData;

                        if (saveCertificateToDb(cscaEntry, fingerprint)) {
                            saveCertificateToLocalLdap(cscaEntry, fingerprint);
                            newCscaCount++;
                        }
                    }

                    X509_free(cert);
                }
            }
        }
    }

    // Also extract certificates from CMS certificates field (MLSC + additional CSCAs)
    STACK_OF(X509)* cmsCerts = CMS_get1_certs(cms);
    if (cmsCerts) {
        int numCerts = sk_X509_num(cmsCerts);
        for (int i = 0; i < numCerts; i++) {
            X509* cert = sk_X509_value(cmsCerts, i);
            if (!cert) continue;

            int derLen = i2d_X509(cert, nullptr);
            std::vector<uint8_t> derData(derLen);
            unsigned char* dPtr = derData.data();
            i2d_X509(cert, &dPtr);

            std::string fingerprint = computeFingerprint(derData);
            if (!fingerprint.empty() && !fingerprintExists(fingerprint)) {
                // CMS certificates field typically contains MLSC
                bool isSelfSigned = (X509_NAME_cmp(
                    X509_get_subject_name(cert), X509_get_issuer_name(cert)) == 0);

                IcaoLdapCertEntry certEntry;
                certEntry.dn = mlEntry.dn;
                certEntry.countryCode = mlEntry.countryCode;
                certEntry.certType = isSelfSigned ? "CSCA" : "MLSC";
                certEntry.binaryData = derData;

                if (saveCertificateToDb(certEntry, fingerprint)) {
                    saveCertificateToLocalLdap(certEntry, fingerprint);
                    newCscaCount++;
                }
            }
        }
        // Free the stack (CMS_get1_certs returns a new stack)
        for (int i = 0; i < sk_X509_num(cmsCerts); i++) {
            X509_free(sk_X509_value(cmsCerts, i));
        }
        sk_X509_free(cmsCerts);
    }

    CMS_ContentInfo_free(cms);

    if (newCscaCount > 0) {
        spdlog::info("[IcaoLdapSync] ML {} → {} new CSCAs extracted", mlEntry.countryCode, newCscaCount);
    }

    return newCscaCount;
}

void IcaoLdapSyncService::broadcastProgress(const IcaoLdapSyncProgress& progress) {
    Json::Value data;
    data["phase"] = progress.phase;
    data["currentType"] = progress.currentType;
    data["totalTypes"] = progress.totalTypes;
    data["completedTypes"] = progress.completedTypes;
    data["currentTypeTotal"] = progress.currentTypeTotal;
    data["currentTypeProcessed"] = progress.currentTypeProcessed;
    data["currentTypeNew"] = progress.currentTypeNew;
    data["currentTypeSkipped"] = progress.currentTypeSkipped;
    data["totalNew"] = progress.totalNew;
    data["totalSkipped"] = progress.totalSkipped;
    data["totalFailed"] = progress.totalFailed;
    data["totalRemoteCount"] = progress.totalRemoteCount;
    data["message"] = progress.message;
    data["elapsedMs"] = progress.elapsedMs;

    notification::NotificationManager::getInstance().broadcast(
        "ICAO_LDAP_SYNC_PROGRESS",
        "ICAO PKD 동기화",
        progress.message,
        data);
}

IcaoLdapConnectionTestResult IcaoLdapSyncService::testConnection() {
    IcaoLdapConnectionTestResult result;

    auto start = std::chrono::steady_clock::now();

    try {
        std::unique_ptr<IcaoLdapClient> clientPtr;
        if (config_.icaoLdapUseTls) {
            IcaoLdapTlsConfig tlsCfg;
            tlsCfg.enabled = true;
            tlsCfg.certFile = config_.icaoLdapTlsCertFile;
            tlsCfg.keyFile = config_.icaoLdapTlsKeyFile;
            tlsCfg.caCertFile = config_.icaoLdapTlsCaCertFile;
            tlsCfg.bindDn = config_.icaoLdapBindDn;
            tlsCfg.bindPassword = config_.icaoLdapBindPassword;
            clientPtr = std::make_unique<IcaoLdapClient>(
                config_.icaoLdapHost, config_.icaoLdapPort,
                config_.icaoLdapBaseDn, tlsCfg);
            result.tlsMode = tlsCfg.bindDn.empty() ? "TLS / SASL EXTERNAL" : "TLS / Simple Bind";
        } else {
            clientPtr = std::make_unique<IcaoLdapClient>(
                config_.icaoLdapHost, config_.icaoLdapPort,
                config_.icaoLdapBindDn, config_.icaoLdapBindPassword,
                config_.icaoLdapBaseDn);
            result.tlsMode = "Simple Bind";
        }

        if (!clientPtr->connect()) {
            result.success = false;
            result.errorMessage = clientPtr->lastError().empty()
                ? ("Connection failed to " + config_.icaoLdapHost + ":" + std::to_string(config_.icaoLdapPort))
                : clientPtr->lastError();
            auto elapsed = std::chrono::steady_clock::now() - start;
            result.latencyMs = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
            return result;
        }

        auto elapsed = std::chrono::steady_clock::now() - start;
        result.latencyMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());

        result.entryCount = clientPtr->getTotalEntryCount();
        result.serverInfo = config_.icaoLdapHost + ":" + std::to_string(config_.icaoLdapPort);
        result.success = true;

        // Read TLS certificate info from files
        if (config_.icaoLdapUseTls) {
            auto clientCert = readCertInfo(config_.icaoLdapTlsCertFile);
            result.clientCertSubject = clientCert.subject;
            result.clientCertIssuer = clientCert.issuer;
            result.clientCertExpiry = clientCert.expiry;

            auto caCert = readCertInfo(config_.icaoLdapTlsCaCertFile);
            result.serverCertSubject = caCert.subject;
            result.serverCertIssuer = caCert.issuer;
            result.serverCertExpiry = caCert.expiry;
        }

        clientPtr->disconnect();

    } catch (const std::exception& e) {
        result.success = false;
        result.errorMessage = e.what();
        auto elapsed = std::chrono::steady_clock::now() - start;
        result.latencyMs = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
    }

    return result;
}

} // namespace relay
} // namespace icao
