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

        // Sync helper lambda with per-entry progress broadcast
        int typeIndex = 0;
        auto syncEntries = [&](const std::string& typeName,
                               std::vector<IcaoLdapCertEntry> (IcaoLdapClient::*searchFn)(int)) {
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
                    saveSyncLog(result);
                    syncRunning_ = false;
                    return result;
                }

                currentProgress_.currentTypeProcessed++;

                // Broadcast every 50 entries or on last entry
                if (currentProgress_.currentTypeProcessed % 50 == 0 ||
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
        return EntryResult::SKIPPED;
    }

    bool saved = false;
    if (entry.certType == "CRL") {
        saved = saveCrlToDb(entry, fingerprint);
        if (saved) saveCrlToLocalLdap(entry, fingerprint);
    } else {
        saved = saveCertificateToDb(entry, fingerprint);
        if (saved) {
            saveCertificateToLocalLdap(entry, fingerprint);

            // Trust Chain validation for DSC certificates
            if (entry.certType == "DSC" || entry.certType == "DSC_NC") {
                const uint8_t* p = entry.binaryData.data();
                X509* cert = d2i_X509(nullptr, &p, static_cast<long>(entry.binaryData.size()));
                if (cert) {
                    validateAndSaveResult(entry, fingerprint, cert);
                    X509_free(cert);
                }
            }
        }
    }

    return saved ? EntryResult::NEW : EntryResult::FAILED;
}

void IcaoLdapSyncService::validateAndSaveResult(const IcaoLdapCertEntry& entry,
                                                const std::string& fingerprint,
                                                X509* cert) {
    if (!validationRepo_ || !cert) return;

    try {
        // Lazy-init validation components
        initValidation();
        if (!trustChainBuilder_ || !crlChecker_) return;

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

        // Save to validation_result table
        std::string sql;
        if (dbType == "oracle") {
            sql = "INSERT INTO validation_result (id, certificate_id, upload_id, "
                  "validation_status, trust_chain_message, csca_found, "
                  "trust_chain_path, created_at) "
                  "VALUES (SYS_GUID(), $1, $2, $3, $4, "
                  + common::db::boolLiteral(dbType, cscaFound) + ", $5, "
                  + common::db::currentTimestamp(dbType) + ")";
        } else {
            sql = "INSERT INTO validation_result (id, certificate_id, upload_id, "
                  "validation_status, trust_chain_message, csca_found, "
                  "trust_chain_path, created_at) "
                  "VALUES (gen_random_uuid(), $1, $2, $3, $4, $5, $6, NOW()) "
                  "ON CONFLICT (certificate_id, upload_id) DO NOTHING";
        }

        queryExecutor_->executeQuery(sql, {
            fingerprint,                              // certificate_id
            std::string("ICAO_PKD_SYNC"),            // upload_id (marker)
            validationStatus,
            validationMessage,
            dbType == "oracle" ? "" : (cscaFound ? "true" : "false"),  // PostgreSQL bool
            trustChainPath.empty() ? validationStatus : trustChainPath
        });

        // Update certificate validation_status
        queryExecutor_->executeQuery(
            "UPDATE certificate SET validation_status = $1 WHERE fingerprint_sha256 = $2",
            {validationStatus, fingerprint});

    } catch (const std::exception& e) {
        spdlog::warn("[IcaoLdapSync] validateAndSaveResult failed for {}: {}", fingerprint, e.what());
    }
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

        // Validity dates
        std::string notBefore, notAfter;
        {
            auto asn1ToStr = [](const ASN1_TIME* t) -> std::string {
                if (!t) return "";
                BIO* bio = BIO_new(BIO_s_mem());
                if (!bio) return "";
                ASN1_TIME_print(bio, t);
                char buf[128] = {0};
                BIO_read(bio, buf, sizeof(buf) - 1);
                BIO_free(bio);
                return buf;
            };
            notBefore = asn1ToStr(X509_get0_notBefore(cert));
            notAfter = asn1ToStr(X509_get0_notAfter(cert));
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
        std::ostringstream hexStream;
        for (auto b : entry.binaryData) {
            hexStream << std::hex << std::setfill('0') << std::setw(2) << static_cast<int>(b);
        }
        std::string hexData = hexStream.str();
        if (hexData.empty()) return false;  // Double-check

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
            fingerprint, entry.countryCode, hexData, std::string("ICAO_PKD_SYNC")
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
