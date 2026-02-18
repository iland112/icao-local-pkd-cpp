/**
 * @file upload_handler.cpp
 * @brief UploadHandler implementation
 *
 * Extracted from main.cpp - upload-related handler endpoints.
 *
 * @date 2026-02-17
 */

#include "upload_handler.h"

#include <drogon/drogon.h>
#include <trantor/utils/Date.h>
#include <spdlog/spdlog.h>
#include <json/json.h>

#include <fstream>
#include <thread>
#include <future>
#include <set>
#include <algorithm>
#include <sstream>
#include <iomanip>

// OpenLDAP header
#include <ldap.h>

// OpenSSL for CMS signature verification and certificate parsing
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/asn1.h>
#include <openssl/bn.h>
#include <openssl/bio.h>
#include <openssl/pkcs7.h>
#include <openssl/cms.h>

// Infrastructure
#include "../infrastructure/service_container.h"

// ICAO Validation Library (shared)
#include <icao/validation/cert_ops.h>

// Repositories
#include "../repositories/upload_repository.h"
#include "../repositories/certificate_repository.h"
#include "../repositories/crl_repository.h"
#include "../repositories/validation_repository.h"

// Services
#include "../services/upload_service.h"
#include "../services/validation_service.h"
#include "../services/ldif_structure_service.h"
#include "../services/ldap_storage_service.h"

// Common utilities
#include "../common.h"
#include "../common/main_utils.h"
#include "../common/progress_manager.h"
#include "../common/certificate_utils.h"
#include "../common/x509_metadata_extractor.h"
#include "../processing_strategy.h"
#include "../ldif_processor.h"

// Audit logging (shared library)
#include <icao/audit/audit_log.h>

// Bring in audit types for cleaner code
using icao::audit::AuditLogEntry;
using icao::audit::OperationType;
using icao::audit::logOperation;
using icao::audit::createAuditEntryFromRequest;
using icao::audit::extractUserFromRequest;
using icao::audit::extractIpAddress;

// Progress manager types
using common::ProcessingStage;
using common::ProcessingProgress;
using common::ProgressManager;

// Global service container (defined in main.cpp)
extern infrastructure::ServiceContainer* g_services;

namespace {

/**
 * @brief Load UN_CSCA trust anchor certificate
 * @param trustAnchorPath Path to PEM file
 */
X509* loadTrustAnchor(const std::string& trustAnchorPath) {
    FILE* fp = fopen(trustAnchorPath.c_str(), "r");
    if (!fp) {
        spdlog::error("Failed to open trust anchor file: {}", trustAnchorPath);
        return nullptr;
    }

    X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
    fclose(fp);

    if (!cert) {
        spdlog::error("Failed to parse trust anchor certificate");
        return nullptr;
    }

    spdlog::info("Trust anchor loaded: {}", trustAnchorPath);
    return cert;
}

/**
 * @brief Verify CMS signature of Master List against UN_CSCA trust anchor
 */
bool verifyCmsSignature(CMS_ContentInfo* cms, X509* trustAnchor) {
    if (!cms || !trustAnchor) return false;

    // Create certificate store with trust anchor
    X509_STORE* store = X509_STORE_new();
    if (!store) {
        spdlog::error("Failed to create X509 store");
        return false;
    }

    X509_STORE_add_cert(store, trustAnchor);

    // Get signer certificates from CMS
    STACK_OF(X509)* signerCerts = CMS_get1_certs(cms);

    // Verify CMS signature
    BIO* contentBio = BIO_new(BIO_s_mem());
    if (!contentBio) {
        if (signerCerts) sk_X509_pop_free(signerCerts, X509_free);
        X509_STORE_free(store);
        spdlog::error("Failed to create content BIO for CMS verification");
        return false;
    }
    int result = CMS_verify(cms, signerCerts, store, nullptr, contentBio, CMS_NO_SIGNER_CERT_VERIFY);

    BIO_free(contentBio);
    if (signerCerts) sk_X509_pop_free(signerCerts, X509_free);
    X509_STORE_free(store);

    if (result != 1) {
        unsigned long err = ERR_get_error();
        char errBuf[256];
        ERR_error_string_n(err, errBuf, sizeof(errBuf));
        spdlog::warn("CMS signature verification failed: {}", errBuf);
        return false;
    }

    spdlog::info("CMS signature verification succeeded");
    return true;
}

} // anonymous namespace

namespace handlers {

// Static member definitions
std::mutex UploadHandler::s_processingMutex;
std::set<std::string> UploadHandler::s_processingUploads;

// =============================================================================
// Constructor
// =============================================================================

UploadHandler::UploadHandler(
    services::UploadService* uploadService,
    services::ValidationService* validationService,
    services::LdifStructureService* ldifStructureService,
    repositories::UploadRepository* uploadRepository,
    repositories::CertificateRepository* certificateRepository,
    repositories::CrlRepository* crlRepository,
    repositories::ValidationRepository* validationRepository,
    common::IQueryExecutor* queryExecutor,
    const LdapConfig& ldapConfig)
    : uploadService_(uploadService),
      validationService_(validationService),
      ldifStructureService_(ldifStructureService),
      uploadRepository_(uploadRepository),
      certificateRepository_(certificateRepository),
      crlRepository_(crlRepository),
      validationRepository_(validationRepository),
      queryExecutor_(queryExecutor),
      ldapConfig_(ldapConfig)
{
    if (!uploadService_ || !validationService_ || !ldifStructureService_) {
        throw std::invalid_argument("UploadHandler: services cannot be nullptr");
    }
    if (!uploadRepository_ || !certificateRepository_ || !crlRepository_ || !validationRepository_) {
        throw std::invalid_argument("UploadHandler: repositories cannot be nullptr");
    }
    if (!queryExecutor_) {
        throw std::invalid_argument("UploadHandler: queryExecutor cannot be nullptr");
    }

    spdlog::info("[UploadHandler] Initialized with Repository Pattern (LDAP write: {}:{})",
                 ldapConfig_.writeHost, ldapConfig_.writePort);
}

// =============================================================================
// Route Registration
// =============================================================================

void UploadHandler::registerRoutes(drogon::HttpAppFramework& app) {
    // POST /api/upload/{uploadId}/parse
    app.registerHandler(
        "/api/upload/{uploadId}/parse",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& uploadId) {
            handleParse(req, std::move(callback), uploadId);
        },
        {drogon::Post}
    );

    // POST /api/upload/{uploadId}/validate
    app.registerHandler(
        "/api/upload/{uploadId}/validate",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& uploadId) {
            handleValidate(req, std::move(callback), uploadId);
        },
        {drogon::Post}
    );

    // GET /api/upload/{uploadId}/validations
    app.registerHandler(
        "/api/upload/{uploadId}/validations",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& uploadId) {
            handleGetValidations(req, std::move(callback), uploadId);
        },
        {drogon::Get}
    );

    // GET /api/upload/{uploadId}/validation-statistics
    app.registerHandler(
        "/api/upload/{uploadId}/validation-statistics",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& uploadId) {
            handleGetValidationStatistics(req, std::move(callback), uploadId);
        },
        {drogon::Get}
    );

    // GET /api/upload/{uploadId}/ldif-structure
    app.registerHandler(
        "/api/upload/{uploadId}/ldif-structure",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& uploadId) {
            handleGetLdifStructure(req, std::move(callback), uploadId);
        },
        {drogon::Get}
    );

    // DELETE /api/upload/{uploadId}
    app.registerHandler(
        "/api/upload/{uploadId}",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& uploadId) {
            handleDelete(req, std::move(callback), uploadId);
        },
        {drogon::Delete}
    );

    // POST /api/upload/ldif
    app.registerHandler(
        "/api/upload/ldif",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleUploadLdif(req, std::move(callback));
        },
        {drogon::Post}
    );

    // POST /api/upload/masterlist
    app.registerHandler(
        "/api/upload/masterlist",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleUploadMasterList(req, std::move(callback));
        },
        {drogon::Post}
    );

    // POST /api/upload/certificate
    app.registerHandler(
        "/api/upload/certificate",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleUploadCertificate(req, std::move(callback));
        },
        {drogon::Post}
    );

    // POST /api/upload/certificate/preview
    app.registerHandler(
        "/api/upload/certificate/preview",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handlePreviewCertificate(req, std::move(callback));
        },
        {drogon::Post}
    );

    spdlog::info("[UploadHandler] Registered 10 upload routes");
}

// =============================================================================
// Helper: getLdapWriteConnection
// =============================================================================

LDAP* UploadHandler::getLdapWriteConnection() {
    LDAP* ld = nullptr;
    std::string uri = "ldap://" + ldapConfig_.writeHost + ":" + std::to_string(ldapConfig_.writePort);

    int rc = ldap_initialize(&ld, uri.c_str());
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP write connection initialize failed: {}", ldap_err2string(rc));
        return nullptr;
    }

    int version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

    // Direct connection, no referral chasing needed
    ldap_set_option(ld, LDAP_OPT_REFERRALS, LDAP_OPT_OFF);

    struct berval cred;
    cred.bv_val = const_cast<char*>(ldapConfig_.bindPassword.c_str());
    cred.bv_len = ldapConfig_.bindPassword.length();

    rc = ldap_sasl_bind_s(ld, ldapConfig_.bindDn.c_str(), LDAP_SASL_SIMPLE, &cred, nullptr, nullptr, nullptr);
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP write connection bind failed: {}", ldap_err2string(rc));
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        return nullptr;
    }

    spdlog::debug("LDAP write: Connected successfully to {}:{}", ldapConfig_.writeHost, ldapConfig_.writePort);
    return ld;
}

// =============================================================================
// Helper: processLdifFileAsync
// =============================================================================

void UploadHandler::processLdifFileAsync(const std::string& uploadId, const std::vector<uint8_t>& content) {
    // Check and register this upload for processing (prevent duplicate threads)
    {
        std::lock_guard<std::mutex> lock(s_processingMutex);
        if (s_processingUploads.count(uploadId) > 0) {
            spdlog::warn("[processLdifFileAsync] Upload {} already being processed - skipping duplicate", uploadId);
            return;
        }
        s_processingUploads.insert(uploadId);
    }

    std::thread([uploadId, content]() {
        // Ensure cleanup on thread exit
        auto cleanupGuard = [&uploadId]() {
            std::lock_guard<std::mutex> lock(UploadHandler::s_processingMutex);
            UploadHandler::s_processingUploads.erase(uploadId);
        };

        spdlog::info("Starting async LDIF processing for upload: {}", uploadId);

        // Get processing_mode from upload record using Repository
        auto uploadOpt = g_services->uploadRepository()->findById(uploadId);
        if (!uploadOpt.has_value()) {
            spdlog::error("Upload record not found: {}", uploadId);
            cleanupGuard();
            return;
        }

        std::string processingMode = uploadOpt->processingMode.value_or("AUTO");
        spdlog::info("Processing mode for LDIF upload {}: {}", uploadId, processingMode);

        // Connect to LDAP only if AUTO mode (for MANUAL, LDAP connection happens during triggerLdapUpload)
        LDAP* ld = nullptr;
        if (processingMode == "AUTO") {
            ld = g_services->ldapStorageService()->getLdapWriteConnection();
            if (!ld) {
                spdlog::error("CRITICAL: LDAP write connection failed in AUTO mode for LDIF upload {}", uploadId);
                spdlog::error("Cannot proceed - data consistency requires both DB and LDAP storage");

                // Update upload status to FAILED using Repository
                g_services->uploadRepository()->updateStatus(uploadId, "FAILED",
                    "LDAP connection failure - cannot ensure data consistency");

                // Send failure progress
                ProgressManager::getInstance().sendProgress(
                    ProcessingProgress::create(uploadId, ProcessingStage::FAILED,
                        0, 0, "LDAP 연결 실패", "데이터 일관성을 보장할 수 없어 처리를 중단했습니다."));

                if (ld) ldap_unbind_ext_s(ld, nullptr, nullptr);
                cleanupGuard();
                return;
            }
            spdlog::info("LDAP write connection established successfully for AUTO mode LDIF upload {}", uploadId);
        }

        try {
            std::string contentStr(content.begin(), content.end());

            // Send parsing started progress
            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::PARSING_IN_PROGRESS,
                    0, 100, "LDIF 파일 파싱 중..."));

            // Parse LDIF content using LdifProcessor
            std::vector<LdifEntry> entries = LdifProcessor::parseLdifContent(contentStr);
            int totalEntries = static_cast<int>(entries.size());

            spdlog::info("Parsed {} LDIF entries for upload {}", totalEntries, uploadId);

            // Update DB record: status = PROCESSING, total_entries populated
            if (g_services && g_services->uploadRepository()) {
                g_services->uploadRepository()->updateStatus(uploadId, "PROCESSING", "");
                g_services->uploadRepository()->updateProgress(uploadId, totalEntries, 0);
                spdlog::info("Upload {} status updated to PROCESSING (total_entries={})", uploadId, totalEntries);
            }

            // Use Strategy Pattern to handle AUTO vs MANUAL modes
            auto strategy = ProcessingStrategyFactory::create(processingMode);
            strategy->processLdifEntries(uploadId, entries, ld);

            // Send parsing completed progress AFTER strategy completes (for MANUAL mode)
            // This ensures temp file is saved and DB status is updated before frontend receives event
            if (processingMode == "MANUAL") {
                ProgressManager::getInstance().sendProgress(
                    ProcessingProgress::create(uploadId, ProcessingStage::PARSING_COMPLETED,
                        totalEntries, totalEntries, "LDIF 파싱 완료: " + std::to_string(totalEntries) + "개 엔트리"));
            } else {
                // For AUTO mode, send progress immediately since processing continues
                ProgressManager::getInstance().sendProgress(
                    ProcessingProgress::create(uploadId, ProcessingStage::PARSING_COMPLETED,
                        totalEntries, totalEntries, "LDIF 파싱 완료: " + std::to_string(totalEntries) + "개 엔트리"));
            }

            // For MANUAL mode, stop here (strategy already saved to temp file)
            if (processingMode == "MANUAL") {
                return;
            }

            // For AUTO mode, strategy has already processed everything
            // No additional work needed here
            spdlog::info("AUTO mode: Processing completed by Strategy Pattern");

        } catch (const std::exception& e) {
            spdlog::error("LDIF processing failed for upload {}: {}", uploadId, e.what());
            common::updateUploadStatistics(uploadId, "FAILED", 0, 0, 0, 0, 0, 0, e.what());
        }

        // Cleanup LDAP connection
        if (ld) {
            ldap_unbind_ext_s(ld, nullptr, nullptr);
        }
        // Note: No PGconn cleanup needed - Strategy Pattern uses Repository with connection pool

        // Remove from processing set
        cleanupGuard();
    }).detach();
}

// =============================================================================
// Helper: processMasterListFileAsync
// =============================================================================

void UploadHandler::processMasterListFileAsync(const std::string& uploadId, const std::vector<uint8_t>& content) {
    // Check and register this upload for processing (prevent duplicate threads)
    {
        std::lock_guard<std::mutex> lock(s_processingMutex);
        if (s_processingUploads.count(uploadId) > 0) {
            spdlog::warn("[processMasterListFileAsync] Upload {} already being processed - skipping duplicate", uploadId);
            return;
        }
        s_processingUploads.insert(uploadId);
    }

    // Capture trustAnchorPath for use in detached thread
    std::string trustAnchorPath = ldapConfig_.trustAnchorPath;

    std::thread([uploadId, content, trustAnchorPath]() {
        // Ensure cleanup on thread exit
        auto cleanupGuard = [&uploadId]() {
            std::lock_guard<std::mutex> lock(UploadHandler::s_processingMutex);
            UploadHandler::s_processingUploads.erase(uploadId);
        };

        spdlog::info("Starting async Master List processing for upload: {}", uploadId);

        // Get processing_mode from upload record using Repository
        auto uploadOpt = g_services->uploadRepository()->findById(uploadId);
        if (!uploadOpt.has_value()) {
            spdlog::error("Upload record not found: {}", uploadId);
            cleanupGuard();
            return;
        }

        std::string processingMode = uploadOpt->processingMode.value_or("AUTO");
        spdlog::info("Processing mode for Master List upload {}: {}", uploadId, processingMode);

        // Connect to LDAP only if AUTO mode
        LDAP* ld = nullptr;
        if (processingMode == "AUTO") {
            ld = g_services->ldapStorageService()->getLdapWriteConnection();
            if (!ld) {
                spdlog::error("CRITICAL: LDAP write connection failed in AUTO mode for Master List upload {}", uploadId);
                spdlog::error("Cannot proceed - data consistency requires both DB and LDAP storage");

                // Update upload status to FAILED using Repository
                g_services->uploadRepository()->updateStatus(uploadId, "FAILED",
                    "LDAP connection failure - cannot ensure data consistency");

                // Send failure progress
                ProgressManager::getInstance().sendProgress(
                    ProcessingProgress::create(uploadId, ProcessingStage::FAILED,
                        0, 0, "LDAP 연결 실패", "데이터 일관성을 보장할 수 없어 처리를 중단했습니다."));

                cleanupGuard();
                return;
            }
            spdlog::info("LDAP write connection established successfully for AUTO mode Master List upload {}", uploadId);
        }

        try {
            int cscaCount = 0;
            int dscCount = 0;
            int ldapStoredCount = 0;
            int skippedDuplicates = 0;
            int totalCerts = 0;
            int totalCertsInML = 0;  // Pre-counted total for progress percentage

            // Validation statistics counters
            int validCount = 0;
            int invalidCount = 0;
            int validPeriodCount = 0;
            int expiredCount = 0;
            int icaoCompliantCount = 0;
            int icaoNonCompliantCount = 0;
            int icaoWarningCount = 0;

            // Send initial progress
            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::PARSING_STARTED, 0, 0, "CMS 파싱 시작"));

            // Validate CMS format: first byte must be 0x30 (SEQUENCE tag)
            if (content.empty() || content[0] != 0x30) {
                spdlog::error("Invalid Master List: not a valid CMS structure (missing SEQUENCE tag)");
                ProgressManager::getInstance().sendProgress(
                    ProcessingProgress::create(uploadId, ProcessingStage::FAILED, 0, 0, "Invalid CMS format", "CMS 형식 오류"));
                g_services->uploadRepository()->updateStatus(uploadId, "FAILED", "Invalid CMS format");
                g_services->uploadRepository()->updateStatistics(uploadId, 0, 0, 0, 0, 0, 0);
                if (ld) ldap_unbind_ext_s(ld, nullptr, nullptr);
                cleanupGuard();
                return;
            }

            // Parse as CMS SignedData using OpenSSL CMS API
            BIO* bio = BIO_new_mem_buf(content.data(), static_cast<int>(content.size()));
            CMS_ContentInfo* cms = nullptr;
            if (bio) {
                cms = d2i_CMS_bio(bio, nullptr);
                BIO_free(bio);
            }

            // Verify CMS signature with UN_CSCA trust anchor
            if (cms) {
                X509* trustAnchor = loadTrustAnchor(trustAnchorPath);
                if (trustAnchor) {
                    bool signatureValid = verifyCmsSignature(cms, trustAnchor);
                    X509_free(trustAnchor);

                    if (!signatureValid) {
                        spdlog::warn("Master List CMS signature verification failed - continuing with parsing");
                        // Note: We continue processing even if signature fails (for testing)
                        // In production, you may want to reject the file
                    }
                } else {
                    spdlog::warn("Trust anchor not available - skipping CMS signature verification");
                }
            }

            // Update DB record: status = PROCESSING before certificate extraction
            if (g_services && g_services->uploadRepository()) {
                g_services->uploadRepository()->updateStatus(uploadId, "PROCESSING", "");
                spdlog::info("Upload {} status updated to PROCESSING (Master List)", uploadId);
            }

            if (!cms) {
                // Fallback: try PKCS7 API for older formats
                spdlog::debug("CMS parsing failed, trying PKCS7 fallback...");
                const uint8_t* p = content.data();
                PKCS7* p7 = d2i_PKCS7(nullptr, &p, static_cast<long>(content.size()));

                if (p7) {
                    STACK_OF(X509)* certs = nullptr;
                    if (PKCS7_type_is_signed(p7)) {
                        certs = p7->d.sign->cert;
                    }

                    if (certs) {
                        int numCerts = sk_X509_num(certs);
                        totalCertsInML = numCerts;
                        spdlog::info("Found {} certificates in Master List (PKCS7 fallback path)", numCerts);

                        for (int i = 0; i < numCerts; i++) {
                            X509* cert = sk_X509_value(certs, i);
                            if (!cert) continue;

                            int derLen = i2d_X509(cert, nullptr);
                            if (derLen <= 0) continue;

                            std::vector<uint8_t> derBytes(derLen);
                            uint8_t* derPtr = derBytes.data();
                            i2d_X509(cert, &derPtr);

                            std::string subjectDn = x509NameToString(X509_get_subject_name(cert));
                            std::string issuerDn = x509NameToString(X509_get_issuer_name(cert));
                            std::string serialNumber = asn1IntegerToHex(X509_get_serialNumber(cert));
                            std::string notBefore = asn1TimeToIso8601(X509_get0_notBefore(cert));
                            std::string notAfter = asn1TimeToIso8601(X509_get0_notAfter(cert));
                            std::string fingerprint = computeFileHash(derBytes);
                            std::string countryCode = extractCountryCode(subjectDn);

                            // Extract comprehensive certificate metadata for progress tracking
                            common::CertificateMetadata certMetadata = common::extractCertificateMetadataForProgress(cert, false);

                            // Master List contains ONLY CSCA certificates (per ICAO Doc 9303)
                            std::string certType = "CSCA";
                            std::string validationStatus = "VALID";
                            std::string validationMessage = "";

                            if (subjectDn == issuerDn) {
                                bool sigValid = icao::validation::verifyCertificateSignature(cert, cert);
                                if (sigValid) {
                                    validationStatus = "VALID";
                                } else {
                                    validationStatus = "INVALID";
                                    validationMessage = "Self-signature verification failed";
                                }
                            }

                            // Track validation counts
                            if (validationStatus == "VALID") validCount++;
                            else if (validationStatus == "INVALID") invalidCount++;

                            // Check ICAO 9303 compliance
                            common::IcaoComplianceStatus icaoCompliance = common::checkIcaoCompliance(cert, certType);
                            if (icaoCompliance.isCompliant) {
                                icaoCompliantCount++;
                            } else {
                                icaoNonCompliantCount++;
                            }

                            // Check validity period
                            {
                                const ASN1_TIME* naTime = X509_get0_notAfter(cert);
                                const ASN1_TIME* nbTime = X509_get0_notBefore(cert);
                                if (naTime && X509_cmp_current_time(naTime) < 0) {
                                    expiredCount++;
                                } else if (nbTime && X509_cmp_current_time(nbTime) > 0) {
                                    // not yet valid - skip
                                } else {
                                    validPeriodCount++;
                                }
                            }

                            totalCerts++;

                            // Send progress update every 10 certs (PKCS7 fallback path)
                            if (totalCerts % 10 == 0) {
                                int savedCount = cscaCount + dscCount;
                                int pct = 30 + (70 * totalCerts / std::max(1, totalCertsInML));
                                auto progress = ProcessingProgress::create(uploadId, ProcessingStage::DB_SAVING_IN_PROGRESS,
                                        savedCount, totalCertsInML,
                                        "인증서 처리 중: " + std::to_string(totalCerts) + "/" + std::to_string(totalCertsInML) + "개");
                                progress.percentage = std::min(pct, 99);
                                ProgressManager::getInstance().sendProgress(progress);

                                if (totalCerts % 50 == 0) {
                                    g_services->uploadRepository()->updateProgress(uploadId, totalCertsInML, savedCount);
                                }
                            }

                            // Save certificate using Repository Pattern
                            auto [certId, isDuplicate] = g_services->certificateRepository()->saveCertificateWithDuplicateCheck(
                                uploadId, certType, countryCode, subjectDn, issuerDn, serialNumber,
                                fingerprint, notBefore, notAfter, derBytes, validationStatus, validationMessage);

                            if (!certId.empty()) {
                                if (isDuplicate) {
                                    skippedDuplicates++;
                                    spdlog::debug("Skipping duplicate CSCA: fingerprint={}", fingerprint.substr(0, 16));
                                } else {
                                    cscaCount++;
                                    spdlog::debug("Saved CSCA to DB: fingerprint={}", fingerprint.substr(0, 16));

                                    if (ld) {
                                        std::string ldapDn = g_services->ldapStorageService()->saveCertificateToLdap(ld, certType, countryCode,
                                                                                    subjectDn, issuerDn, serialNumber,
                                                                                    fingerprint, derBytes);
                                        if (!ldapDn.empty()) {
                                            g_services->certificateRepository()->updateCertificateLdapStatus(certId, ldapDn);
                                            ldapStoredCount++;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    PKCS7_free(p7);
                } else {
                    spdlog::error("Failed to parse Master List: neither CMS nor PKCS7 parsing succeeded");
                    spdlog::error("OpenSSL error: {}", ERR_error_string(ERR_get_error(), nullptr));
                    g_services->uploadRepository()->updateStatus(uploadId, "FAILED", "CMS/PKCS7 parsing failed");
                    g_services->uploadRepository()->updateStatistics(uploadId, 0, 0, 0, 0, 0, 0);
                    if (ld) ldap_unbind_ext_s(ld, nullptr, nullptr);
                    return;
                }
            } else {
                // CMS parsing succeeded - extract certificates from encapsulated content
                spdlog::info("CMS SignedData parsed successfully, extracting encapsulated content...");

                ProgressManager::getInstance().sendProgress(
                    ProcessingProgress::create(uploadId, ProcessingStage::PARSING_IN_PROGRESS, 0, 0, "CMS 파싱 완료, 인증서 추출 중"));

                // Get the encapsulated content (signed data)
                ASN1_OCTET_STRING** contentPtr = CMS_get0_content(cms);
                if (contentPtr && *contentPtr) {
                    const unsigned char* contentData = ASN1_STRING_get0_data(*contentPtr);
                    int contentLen = ASN1_STRING_length(*contentPtr);

                    spdlog::debug("Encapsulated content length: {} bytes", contentLen);

                    // Parse the Master List ASN.1 structure
                    // MasterList ::= SEQUENCE { version INTEGER OPTIONAL, certList SET OF Certificate }
                    const unsigned char* p = contentData;
                    long remaining = contentLen;

                    // Parse outer SEQUENCE
                    int tag, xclass;
                    long seqLen;
                    int ret = ASN1_get_object(&p, &seqLen, &tag, &xclass, remaining);

                    if (ret == 0x80 || tag != V_ASN1_SEQUENCE) {
                        spdlog::error("Invalid Master List structure: expected SEQUENCE");
                    } else {
                        const unsigned char* seqEnd = p + seqLen;

                        // Check first element: could be version (INTEGER) or certList (SET)
                        const unsigned char* elemStart = p;
                        long elemLen;
                        ret = ASN1_get_object(&p, &elemLen, &tag, &xclass, seqEnd - p);

                        const unsigned char* certSetStart = nullptr;
                        long certSetLen = 0;

                        if (tag == V_ASN1_INTEGER) {
                            // Has version, skip it and read next element (certList)
                            p += elemLen;
                            if (p < seqEnd) {
                                ret = ASN1_get_object(&p, &elemLen, &tag, &xclass, seqEnd - p);
                                if (tag == V_ASN1_SET) {
                                    certSetStart = p;
                                    certSetLen = elemLen;
                                }
                            }
                        } else if (tag == V_ASN1_SET) {
                            // No version, this is the certList
                            certSetStart = p;
                            certSetLen = elemLen;
                        }

                        if (certSetStart && certSetLen > 0) {
                            // Pre-count certificates for progress percentage
                            {
                                const unsigned char* scanPtr = certSetStart;
                                const unsigned char* scanEnd = certSetStart + certSetLen;
                                while (scanPtr < scanEnd) {
                                    const unsigned char* scanStart = scanPtr;
                                    X509* scanCert = d2i_X509(nullptr, &scanPtr, scanEnd - scanStart);
                                    if (scanCert) {
                                        totalCertsInML++;
                                        X509_free(scanCert);
                                    } else {
                                        break;
                                    }
                                }
                                spdlog::info("Pre-counted {} certificates in Master List", totalCertsInML);
                            }

                            // Parse certificates from SET
                            const unsigned char* certPtr = certSetStart;
                            const unsigned char* certSetEnd = certSetStart + certSetLen;

                            while (certPtr < certSetEnd) {
                                // Parse each certificate
                                const unsigned char* certStart = certPtr;
                                X509* cert = d2i_X509(nullptr, &certPtr, certSetEnd - certStart);

                                if (cert) {
                                    int derLen = i2d_X509(cert, nullptr);
                                    if (derLen > 0) {
                                        std::vector<uint8_t> derBytes(derLen);
                                        uint8_t* derPtr = derBytes.data();
                                        i2d_X509(cert, &derPtr);

                                        std::string subjectDn = x509NameToString(X509_get_subject_name(cert));
                                        std::string issuerDn = x509NameToString(X509_get_issuer_name(cert));
                                        std::string serialNumber = asn1IntegerToHex(X509_get_serialNumber(cert));
                                        std::string notBefore = asn1TimeToIso8601(X509_get0_notBefore(cert));
                                        std::string notAfter = asn1TimeToIso8601(X509_get0_notAfter(cert));
                                        std::string fingerprint = computeFileHash(derBytes);
                                        std::string countryCode = extractCountryCode(subjectDn);

                                        // Extract comprehensive certificate metadata for progress tracking
                                        common::CertificateMetadata certMetadata = common::extractCertificateMetadataForProgress(cert, false);

                                        // Master List contains ONLY CSCA certificates (per ICAO Doc 9303)
                                        std::string certType = "CSCA";
                                        std::string validationStatus = "VALID";
                                        std::string validationMessage = "";

                                        if (subjectDn == issuerDn) {
                                            // Self-signed CSCA - verify self-signature using icao::validation
                                            bool sigValid = icao::validation::verifyCertificateSignature(cert, cert);
                                            if (sigValid) {
                                                validationStatus = "VALID";
                                                spdlog::debug("CSCA self-signature verified: {}", subjectDn.substr(0, 50));
                                            } else {
                                                validationStatus = "INVALID";
                                                validationMessage = "Self-signature verification failed";
                                                spdlog::error("CSCA self-signature FAILED: {}", subjectDn.substr(0, 50));
                                            }
                                        } else {
                                            // Cross-signed/Link CSCA - mark as valid (signed by another CSCA)
                                            spdlog::debug("Cross-signed CSCA: subject={}, issuer={}",
                                                         subjectDn.substr(0, 50), issuerDn.substr(0, 50));
                                        }

                                        // Track validation counts
                                        if (validationStatus == "VALID") validCount++;
                                        else if (validationStatus == "INVALID") invalidCount++;

                                        // Check ICAO 9303 compliance
                                        common::IcaoComplianceStatus icaoCompliance = common::checkIcaoCompliance(cert, certType);
                                        if (icaoCompliance.isCompliant) {
                                            icaoCompliantCount++;
                                        } else {
                                            icaoNonCompliantCount++;
                                        }

                                        // Check validity period
                                        {
                                            const ASN1_TIME* naTime = X509_get0_notAfter(cert);
                                            const ASN1_TIME* nbTime = X509_get0_notBefore(cert);
                                            if (naTime && X509_cmp_current_time(naTime) < 0) {
                                                expiredCount++;
                                            } else if (nbTime && X509_cmp_current_time(nbTime) > 0) {
                                                // not yet valid - skip
                                            } else {
                                                validPeriodCount++;
                                            }
                                        }

                                        totalCerts++;

                                        // Send progress update every 10 certs
                                        if (totalCerts % 10 == 0) {
                                            int savedCount = cscaCount + dscCount;
                                            int pct = 30 + (70 * totalCerts / std::max(1, totalCertsInML));
                                            auto progress = ProcessingProgress::create(uploadId, ProcessingStage::DB_SAVING_IN_PROGRESS,
                                                    savedCount, totalCertsInML,
                                                    "인증서 처리 중: " + std::to_string(totalCerts) + "/" + std::to_string(totalCertsInML) + "개");
                                            progress.percentage = std::min(pct, 99);
                                            ProgressManager::getInstance().sendProgress(progress);

                                            if (totalCerts % 50 == 0) {
                                                g_services->uploadRepository()->updateProgress(uploadId, totalCertsInML, savedCount);
                                            }
                                        }

                                        // Save to DB with validation status using Repository Pattern
                                        auto [certId, isDuplicate] = g_services->certificateRepository()->saveCertificateWithDuplicateCheck(
                                            uploadId, certType, countryCode, subjectDn, issuerDn, serialNumber,
                                            fingerprint, notBefore, notAfter, derBytes, validationStatus, validationMessage);

                                        if (!certId.empty()) {
                                            if (isDuplicate) {
                                                skippedDuplicates++;
                                                spdlog::debug("Skipping duplicate CSCA from Master List: fingerprint={}", fingerprint.substr(0, 16));

                                                // Track duplicate in certificate_duplicates table
                                                g_services->certificateRepository()->trackCertificateDuplicate(certId, uploadId, "ML_FILE", countryCode, "", "");
                                            } else {
                                                cscaCount++;
                                                spdlog::debug("Saved CSCA from Master List to DB: country={}, fingerprint={}",
                                                             countryCode, fingerprint.substr(0, 16));

                                                // Save to LDAP
                                                if (ld) {
                                                    std::string ldapDn = g_services->ldapStorageService()->saveCertificateToLdap(ld, certType, countryCode,
                                                                                                subjectDn, issuerDn, serialNumber,
                                                                                                fingerprint, derBytes);
                                                    if (!ldapDn.empty()) {
                                                        g_services->certificateRepository()->updateCertificateLdapStatus(certId, ldapDn);
                                                        ldapStoredCount++;
                                                        spdlog::debug("Saved {} from Master List to LDAP: {}", certType, ldapDn);
                                                    }
                                                }
                                            }
                                        }
                                    }
                                    X509_free(cert);
                                } else {
                                    // Failed to parse certificate, skip to end
                                    spdlog::warn("Failed to parse certificate in Master List SET");
                                    break;
                                }
                            }

                            spdlog::info("Extracted {} certificates from Master List encapsulated content", cscaCount + dscCount);
                        } else {
                            spdlog::warn("No certificate SET found in Master List structure");
                        }
                    }
                } else {
                    // No encapsulated content, try getting signer certificates from CMS store
                    spdlog::debug("No encapsulated content, trying CMS certificate store...");
                    STACK_OF(X509)* certs = CMS_get1_certs(cms);

                    if (certs) {
                        int numCerts = sk_X509_num(certs);
                        totalCertsInML = numCerts;
                        spdlog::info("Found {} certificates in CMS certificate store", numCerts);

                        for (int i = 0; i < numCerts; i++) {
                            X509* cert = sk_X509_value(certs, i);
                            if (!cert) continue;

                            int derLen = i2d_X509(cert, nullptr);
                            if (derLen <= 0) continue;

                            std::vector<uint8_t> derBytes(derLen);
                            uint8_t* derPtr = derBytes.data();
                            i2d_X509(cert, &derPtr);

                            std::string subjectDn = x509NameToString(X509_get_subject_name(cert));
                            std::string issuerDn = x509NameToString(X509_get_issuer_name(cert));
                            std::string serialNumber = asn1IntegerToHex(X509_get_serialNumber(cert));
                            std::string notBefore = asn1TimeToIso8601(X509_get0_notBefore(cert));
                            std::string notAfter = asn1TimeToIso8601(X509_get0_notAfter(cert));
                            std::string fingerprint = computeFileHash(derBytes);
                            std::string countryCode = extractCountryCode(subjectDn);

                            // Extract comprehensive certificate metadata for progress tracking
                            common::CertificateMetadata certMetadata = common::extractCertificateMetadataForProgress(cert, false);

                            // Master List contains ONLY CSCA certificates (per ICAO Doc 9303)
                            std::string certType = "CSCA";
                            std::string validationStatus = "VALID";
                            std::string validationMessage = "";

                            if (subjectDn == issuerDn) {
                                bool sigValid = icao::validation::verifyCertificateSignature(cert, cert);
                                if (sigValid) {
                                    validationStatus = "VALID";
                                } else {
                                    validationStatus = "INVALID";
                                    validationMessage = "Self-signature verification failed";
                                }
                            }

                            // Track validation counts
                            if (validationStatus == "VALID") validCount++;
                            else if (validationStatus == "INVALID") invalidCount++;

                            // Check ICAO 9303 compliance
                            common::IcaoComplianceStatus icaoCompliance = common::checkIcaoCompliance(cert, certType);
                            if (icaoCompliance.isCompliant) {
                                icaoCompliantCount++;
                            } else {
                                icaoNonCompliantCount++;
                            }

                            // Check validity period
                            {
                                const ASN1_TIME* naTime = X509_get0_notAfter(cert);
                                const ASN1_TIME* nbTime = X509_get0_notBefore(cert);
                                if (naTime && X509_cmp_current_time(naTime) < 0) {
                                    expiredCount++;
                                } else if (nbTime && X509_cmp_current_time(nbTime) > 0) {
                                    // not yet valid - skip
                                } else {
                                    validPeriodCount++;
                                }
                            }

                            totalCerts++;

                            // Send progress update every 10 certs
                            if (totalCerts % 10 == 0) {
                                int savedCount = cscaCount + dscCount;
                                int pct = 30 + (70 * totalCerts / std::max(1, totalCertsInML));
                                auto progress = ProcessingProgress::create(uploadId, ProcessingStage::DB_SAVING_IN_PROGRESS,
                                        savedCount, totalCertsInML,
                                        "인증서 처리 중: " + std::to_string(totalCerts) + "/" + std::to_string(totalCertsInML) + "개");
                                progress.percentage = std::min(pct, 99);
                                ProgressManager::getInstance().sendProgress(progress);

                                if (totalCerts % 50 == 0) {
                                    g_services->uploadRepository()->updateProgress(uploadId, totalCertsInML, savedCount);
                                }
                            }

                            // Save certificate using Repository Pattern
                            auto [certId, isDuplicate] = g_services->certificateRepository()->saveCertificateWithDuplicateCheck(
                                uploadId, certType, countryCode, subjectDn, issuerDn, serialNumber,
                                fingerprint, notBefore, notAfter, derBytes, validationStatus, validationMessage);

                            if (!certId.empty()) {
                                if (isDuplicate) {
                                    skippedDuplicates++;
                                    spdlog::debug("Skipping duplicate CSCA: fingerprint={}", fingerprint.substr(0, 16));
                                } else {
                                    cscaCount++;
                                    spdlog::debug("Saved CSCA to DB: fingerprint={}", fingerprint.substr(0, 16));

                                    if (ld) {
                                        std::string ldapDn = g_services->ldapStorageService()->saveCertificateToLdap(ld, certType, countryCode,
                                                                                    subjectDn, issuerDn, serialNumber,
                                                                                    fingerprint, derBytes);
                                        if (!ldapDn.empty()) {
                                            g_services->certificateRepository()->updateCertificateLdapStatus(certId, ldapDn);
                                            ldapStoredCount++;
                                        }
                                    }
                                }
                            }
                        }

                        sk_X509_pop_free(certs, X509_free);
                    }
                }

                CMS_ContentInfo_free(cms);
            }

            // Update statistics
            g_services->uploadRepository()->updateStatus(uploadId, "COMPLETED", "");
            g_services->uploadRepository()->updateStatistics(uploadId, cscaCount, dscCount, 0, 0, 1, 1);
            int finalTotal = totalCertsInML > 0 ? totalCertsInML : totalCerts;
            g_services->uploadRepository()->updateProgress(uploadId, finalTotal, cscaCount + dscCount);

            // Save validation statistics to DB
            if (g_services->validationRepository()) {
                domain::models::ValidationStatistics valStats;
                valStats.validCount = validCount;
                valStats.invalidCount = invalidCount;
                valStats.validPeriodCount = validPeriodCount;
                valStats.expiredCount = expiredCount;
                valStats.icaoCompliantCount = icaoCompliantCount;
                valStats.icaoNonCompliantCount = icaoNonCompliantCount;
                valStats.icaoWarningCount = icaoWarningCount;
                g_services->validationRepository()->updateStatistics(uploadId, valStats);
                spdlog::info("Validation statistics saved for ML upload {}: valid={}, invalid={}, validPeriod={}, expired={}, icaoCompliant={}, icaoNonCompliant={}",
                            uploadId, validCount, invalidCount, validPeriodCount, expiredCount, icaoCompliantCount, icaoNonCompliantCount);
            }

            // Enhanced completion message with LDAP status
            std::string completionMsg = "처리 완료: ";
            std::vector<std::string> completionParts;
            if (cscaCount > 0) completionParts.push_back("CSCA " + std::to_string(cscaCount));
            if (dscCount > 0) completionParts.push_back("DSC " + std::to_string(dscCount));

            for (size_t i = 0; i < completionParts.size(); ++i) {
                if (i > 0) completionMsg += ", ";
                completionMsg += completionParts[i];
            }

            if (skippedDuplicates > 0) {
                completionMsg += " (중복 " + std::to_string(skippedDuplicates) + "개 건너뜀)";
            }

            if (ld) {
                completionMsg += ", LDAP 저장 " + std::to_string(ldapStoredCount) + "/" + std::to_string(cscaCount + dscCount);
            }

            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::COMPLETED,
                    cscaCount + dscCount, totalCerts, completionMsg));

            spdlog::info("Master List processing completed for upload {}: {} CSCA, {} DSC certificates (LDAP: {}, duplicates skipped: {})",
                        uploadId, cscaCount, dscCount, ldapStoredCount, skippedDuplicates);

        } catch (const std::exception& e) {
            spdlog::error("Master List processing failed for upload {}: {}", uploadId, e.what());
            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::FAILED, 0, 0, "처리 실패", e.what()));
            g_services->uploadRepository()->updateStatus(uploadId, "FAILED", e.what());
            g_services->uploadRepository()->updateStatistics(uploadId, 0, 0, 0, 0, 0, 0);
        }

        // Cleanup LDAP connection
        if (ld) {
            ldap_unbind_ext_s(ld, nullptr, nullptr);
        }

        // Remove from processing set
        cleanupGuard();
    }).detach();
}

// =============================================================================
// POST /api/upload/{uploadId}/parse
// =============================================================================

void UploadHandler::handleParse(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& uploadId)
{
    spdlog::info("POST /api/upload/{}/parse - Trigger parsing", uploadId);

    // Use QueryExecutor for Oracle support
    if (!queryExecutor_) {
        Json::Value error;
        error["success"] = false;
        error["message"] = "Query executor not initialized";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
        return;
    }

    try {
        // Check if upload exists and get file path (parameterized query)
        std::string query = "SELECT id, file_path, file_format FROM uploaded_file WHERE id = $1";
        auto rows = queryExecutor_->executeQuery(query, {uploadId});

        if (rows.empty()) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "Upload not found";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k404NotFound);
            callback(resp);
            return;
        }

        // Get file path and format
        std::string filePathStr = rows[0].get("file_path", "").asString();
        std::string fileFormatStr = rows[0].get("file_format", "").asString();

        if (filePathStr.empty()) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "File path not found. File may not have been saved.";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k404NotFound);
            callback(resp);
            return;
        }

        // Read file from disk
        std::ifstream inFile(filePathStr, std::ios::binary | std::ios::ate);
        if (!inFile.is_open()) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "Failed to open file: " + filePathStr;
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k500InternalServerError);
            callback(resp);
            return;
        }

        std::streamsize fileSize = inFile.tellg();
        inFile.seekg(0, std::ios::beg);
        std::vector<uint8_t> contentBytes(fileSize);
        if (!inFile.read(reinterpret_cast<char*>(contentBytes.data()), fileSize)) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "Failed to read file";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k500InternalServerError);
            callback(resp);
            return;
        }
        inFile.close();

        // Trigger async processing based on file format
        if (fileFormatStr == "LDIF") {
            processLdifFileAsync(uploadId, contentBytes);
        } else if (fileFormatStr == "ML") {
            // Use Strategy Pattern for Master List processing
            // Capture member pointers for use in detached thread
            auto* uploadRepo = uploadRepository_;
            std::thread([this, uploadId, contentBytes, uploadRepo]() {
                spdlog::info("Starting async Master List processing via Strategy for upload: {}", uploadId);

                // Get processing mode from upload record using Repository
                auto uploadOpt = uploadRepo->findById(uploadId);
                if (!uploadOpt.has_value()) {
                    spdlog::error("Upload record not found: {}", uploadId);
                    return;
                }

                std::string processingMode = uploadOpt->processingMode.value_or("AUTO");
                spdlog::info("Processing mode for Master List upload {}: {}", uploadId, processingMode);

                // Connect to LDAP only if AUTO mode
                LDAP* ld = nullptr;
                if (processingMode == "AUTO") {
                    ld = this->getLdapWriteConnection();
                    if (!ld) {
                        spdlog::error("CRITICAL: LDAP write connection failed in AUTO mode for Master List upload {}", uploadId);
                        spdlog::error("Cannot proceed - data consistency requires both DB and LDAP storage");

                        // Update upload status to FAILED using Repository
                        uploadRepo->updateStatus(uploadId, "FAILED",
                            "LDAP connection failure - cannot ensure data consistency");

                        // Send failure progress
                        ProgressManager::getInstance().sendProgress(
                            ProcessingProgress::create(uploadId, ProcessingStage::FAILED,
                                0, 0, "LDAP 연결 실패", "데이터 일관성을 보장할 수 없어 처리를 중단했습니다."));

                        return;
                    }
                    spdlog::info("LDAP write connection established successfully for AUTO mode Master List upload {}", uploadId);
                }

                try {
                    // Use Strategy Pattern
                    auto strategy = ProcessingStrategyFactory::create(processingMode);
                    strategy->processMasterListContent(uploadId, contentBytes, ld);

                    // Send appropriate progress based on mode
                    if (processingMode == "MANUAL") {
                        // MANUAL mode: Only parsing completed, waiting for Stage 2
                        ProgressManager::getInstance().sendProgress(
                            ProcessingProgress::create(uploadId, ProcessingStage::PARSING_COMPLETED,
                                100, 100, "Master List 파싱 완료 - 검증 대기"));
                    } else {
                        // AUTO mode: All processing completed
                        ProgressManager::getInstance().sendProgress(
                            ProcessingProgress::create(uploadId, ProcessingStage::COMPLETED,
                                100, 100, "Master List 처리 완료"));
                    }

                } catch (const std::exception& e) {
                    spdlog::error("Master List processing via Strategy failed for upload {}: {}", uploadId, e.what());
                    ProgressManager::getInstance().sendProgress(
                        ProcessingProgress::create(uploadId, ProcessingStage::FAILED,
                            0, 0, "처리 실패", e.what()));
                }

                if (ld) ldap_unbind_ext_s(ld, nullptr, nullptr);
                // Note: No PGconn cleanup needed - using Repository Pattern with connection pool
            }).detach();
        } else {
            Json::Value error;
            error["success"] = false;
            error["message"] = "Unsupported file format: " + fileFormatStr;
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        Json::Value result;
        result["success"] = true;
        result["message"] = "Parse processing started";
        result["uploadId"] = uploadId;

        auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("POST /api/upload/{}/parse error: {}", uploadId, e.what());
        Json::Value error;
        error["success"] = false;
        error["message"] = std::string("Internal error: ") + e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
    }
}

// =============================================================================
// POST /api/upload/{uploadId}/validate
// =============================================================================

void UploadHandler::handleValidate(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& uploadId)
{
    spdlog::info("POST /api/upload/{}/validate - Trigger validation and DB save", uploadId);

    // Check if upload exists using Repository Pattern
    auto uploadOpt = uploadRepository_->findById(uploadId);
    if (!uploadOpt.has_value()) {
        Json::Value error;
        error["success"] = false;
        error["message"] = "Upload not found";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k404NotFound);
        callback(resp);
        return;
    }

    // Trigger validation and DB save in background (MANUAL mode Stage 2)
    std::thread([uploadId]() {
        spdlog::info("Starting DSC validation for upload: {}", uploadId);

        try {
            // Send validation started
            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::VALIDATION_IN_PROGRESS,
                    0, 100, "인증서 검증 중..."));

            // MANUAL mode Stage 2: Validate and save to DB
            // Note: validateAndSaveToDb() uses Repository Pattern internally (no PGconn* needed)
            auto strategy = ProcessingStrategyFactory::create("MANUAL");
            strategy->validateAndSaveToDb(uploadId);

            // Send DB save completed (Stage 2 completed)
            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::DB_SAVING_COMPLETED,
                    100, 100, "DB 저장 및 검증 완료"));

            spdlog::info("MANUAL mode Stage 2 completed for upload {}", uploadId);
        } catch (const std::exception& e) {
            spdlog::error("Validation failed for upload {}: {}", uploadId, e.what());
            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::FAILED,
                    0, 0, std::string("검증 실패: ") + e.what()));
        }
    }).detach();

    Json::Value result;
    result["success"] = true;
    result["message"] = "Validation processing started";
    result["uploadId"] = uploadId;

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

// =============================================================================
// GET /api/upload/{uploadId}/validations
// =============================================================================

void UploadHandler::handleGetValidations(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& uploadId)
{
    try {
        spdlog::info("GET /api/upload/{}/validations", uploadId);

        // Parse query parameters
        std::string limitStr = req->getOptionalParameter<std::string>("limit").value_or("50");
        std::string offsetStr = req->getOptionalParameter<std::string>("offset").value_or("0");
        std::string status = req->getOptionalParameter<std::string>("status").value_or("");
        std::string certType = req->getOptionalParameter<std::string>("certType").value_or("");

        int limit = std::stoi(limitStr);
        int offset = std::stoi(offsetStr);

        // Call ValidationService (Repository Pattern)
        Json::Value response = validationService_->getValidationsByUploadId(
            uploadId, limit, offset, status, certType
        );

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("Upload validations error: {}", e.what());
        Json::Value error;
        error["success"] = false;
        error["error"] = e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
    }
}

// =============================================================================
// GET /api/upload/{uploadId}/validation-statistics
// =============================================================================

void UploadHandler::handleGetValidationStatistics(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& uploadId)
{
    try {
        spdlog::info("GET /api/upload/{}/validation-statistics", uploadId);

        // Call ValidationService (Repository Pattern)
        Json::Value response = validationService_->getValidationStatistics(uploadId);

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("Validation statistics error: {}", e.what());
        Json::Value error;
        error["success"] = false;
        error["error"] = e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
    }
}

// =============================================================================
// GET /api/upload/{uploadId}/ldif-structure
// =============================================================================

void UploadHandler::handleGetLdifStructure(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& uploadId)
{
    try {
        spdlog::info("GET /api/upload/{}/ldif-structure", uploadId);

        // Get maxEntries from query parameter (default: 100)
        int maxEntries = 100;
        if (req->getParameter("maxEntries") != "") {
            try {
                maxEntries = std::stoi(req->getParameter("maxEntries"));
            } catch (...) {
                // Invalid maxEntries, use default
                maxEntries = 100;
            }
        }

        // Call LdifStructureService (Repository Pattern)
        Json::Value response = ldifStructureService_->getLdifStructure(uploadId, maxEntries);

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("LDIF structure error: {}", e.what());
        Json::Value error;
        error["success"] = false;
        error["error"] = e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
    }
}

// =============================================================================
// DELETE /api/upload/{uploadId}
// =============================================================================

void UploadHandler::handleDelete(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& uploadId)
{
    spdlog::info("DELETE /api/upload/{} - Delete upload", uploadId);

    try {
        bool deleted = uploadService_->deleteUpload(uploadId);

        if (!deleted) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "Upload not found or deletion failed";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k404NotFound);
            callback(resp);
            return;
        }

        Json::Value result;
        result["success"] = true;
        result["message"] = "Upload deleted successfully";
        result["uploadId"] = uploadId;

        auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
        callback(resp);

        // Audit logging - UPLOAD_DELETE success
        {
            AuditLogEntry auditEntry;
            auto [userId, username] = extractUserFromRequest(req);
            auditEntry.userId = userId;
            auditEntry.username = username;
            auditEntry.operationType = OperationType::UPLOAD_DELETE;
            auditEntry.operationSubtype = "UPLOAD";
            auditEntry.resourceId = uploadId;
            auditEntry.resourceType = "UPLOADED_FILE";
            auditEntry.ipAddress = extractIpAddress(req);
            auditEntry.userAgent = req->getHeader("User-Agent");
            auditEntry.requestMethod = "DELETE";
            auditEntry.requestPath = "/api/upload/" + uploadId;
            auditEntry.success = true;
            Json::Value metadata;
            metadata["uploadId"] = uploadId;
            auditEntry.metadata = metadata;
            logOperation(queryExecutor_, auditEntry);
        }

    } catch (const std::exception& e) {
        spdlog::error("Failed to delete upload {}: {}", uploadId, e.what());

        // Audit logging - UPLOAD_DELETE failed
        {
            AuditLogEntry auditEntry;
            auto [userId, username] = extractUserFromRequest(req);
            auditEntry.userId = userId;
            auditEntry.username = username;
            auditEntry.operationType = OperationType::UPLOAD_DELETE;
            auditEntry.operationSubtype = "UPLOAD";
            auditEntry.resourceId = uploadId;
            auditEntry.resourceType = "UPLOADED_FILE";
            auditEntry.ipAddress = extractIpAddress(req);
            auditEntry.userAgent = req->getHeader("User-Agent");
            auditEntry.requestMethod = "DELETE";
            auditEntry.requestPath = "/api/upload/" + uploadId;
            auditEntry.success = false;
            auditEntry.errorMessage = e.what();
            Json::Value metadata;
            metadata["uploadId"] = uploadId;
            auditEntry.metadata = metadata;
            logOperation(queryExecutor_, auditEntry);
        }

        Json::Value error;
        error["success"] = false;
        error["message"] = std::string("Delete failed: ") + e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
    }
}

// =============================================================================
// POST /api/upload/ldif
// =============================================================================

void UploadHandler::handleUploadLdif(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    spdlog::info("POST /api/upload/ldif - LDIF file upload");

    try {
        // Parse multipart form data
        drogon::MultiPartParser parser;
        if (parser.parse(req) != 0) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "Invalid multipart form data";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        auto& files = parser.getFiles();
        if (files.empty()) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "No file uploaded";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        auto& file = files[0];
        std::string originalFileName = file.getFileName();

        // Sanitize filename to prevent path traversal attacks
        std::string fileName;
        try {
            fileName = sanitizeFilename(originalFileName);
        } catch (const std::exception& e) {
            Json::Value error;
            error["success"] = false;
            error["message"] = std::string("Invalid filename: ") + e.what();
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        std::string content = std::string(file.fileData(), file.fileLength());
        std::vector<uint8_t> contentBytes(content.begin(), content.end());
        int64_t fileSize = static_cast<int64_t>(content.size());

        // Validate LDIF file format
        if (!isValidLdifFile(content)) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "Invalid LDIF file format. File must contain valid LDIF entries (dn: or version:).";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            spdlog::warn("Invalid LDIF file rejected: {}", originalFileName);
            return;
        }

        // Get processing mode from form data
        std::string processingMode = "AUTO";  // default
        auto& params = parser.getParameters();
        for (const auto& param : params) {
            if (param.first == "processingMode") {
                processingMode = param.second;
                break;
            }
        }

        // Get username from session
        std::string username = "anonymous";
        auto session = req->getSession();
        if (session) {
            auto [userId, sessionUsername] = extractUserFromRequest(req);
            username = sessionUsername.value_or("anonymous");
        }

        // Call UploadService to handle upload
        auto result = uploadService_->uploadLdif(fileName, contentBytes, processingMode, username);

        // Handle duplicate file
        if (result.status == "DUPLICATE") {
            // Audit logging - FILE_UPLOAD failed (duplicate)
            {
                AuditLogEntry auditEntry;
                auto [userId2, sessionUsername2] = extractUserFromRequest(req);
                auditEntry.userId = userId2;
                auditEntry.username = sessionUsername2;
                auditEntry.operationType = OperationType::FILE_UPLOAD;
                auditEntry.operationSubtype = "LDIF";
                auditEntry.resourceType = "UPLOADED_FILE";
                auditEntry.ipAddress = extractIpAddress(req);
                auditEntry.userAgent = req->getHeader("User-Agent");
                auditEntry.requestMethod = "POST";
                auditEntry.requestPath = "/api/upload/ldif";
                auditEntry.success = false;
                auditEntry.errorMessage = "Duplicate file detected";
                Json::Value metadata;
                metadata["fileName"] = fileName;
                metadata["fileSize"] = static_cast<Json::Int64>(fileSize);
                metadata["existingUploadId"] = result.uploadId;
                auditEntry.metadata = metadata;
                logOperation(queryExecutor_, auditEntry);
            }

            Json::Value error;
            error["success"] = false;
            error["message"] = result.message.empty() ? "Duplicate file detected. This file has already been uploaded." : result.message;

            Json::Value errorDetail;
            errorDetail["code"] = "DUPLICATE_FILE";
            errorDetail["detail"] = "A file with the same content (SHA-256 hash) already exists in the system.";
            error["error"] = errorDetail;

            Json::Value existingUpload;
            existingUpload["uploadId"] = result.uploadId;
            error["existingUpload"] = existingUpload;

            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k409Conflict);
            callback(resp);

            spdlog::warn("Duplicate LDIF file upload rejected: existing_upload_id={}", result.uploadId);
            return;
        }

        // Handle upload failure
        if (!result.success) {
            // Audit logging - FILE_UPLOAD failed
            {
                AuditLogEntry auditEntry;
                auto [userId3, sessionUsername3] = extractUserFromRequest(req);
                auditEntry.userId = userId3;
                auditEntry.username = sessionUsername3;
                auditEntry.operationType = OperationType::FILE_UPLOAD;
                auditEntry.operationSubtype = "LDIF";
                auditEntry.resourceType = "UPLOADED_FILE";
                auditEntry.ipAddress = extractIpAddress(req);
                auditEntry.userAgent = req->getHeader("User-Agent");
                auditEntry.requestMethod = "POST";
                auditEntry.requestPath = "/api/upload/ldif";
                auditEntry.success = false;
                auditEntry.errorMessage = result.errorMessage;
                Json::Value metadata;
                metadata["fileName"] = fileName;
                metadata["fileSize"] = static_cast<Json::Int64>(fileSize);
                auditEntry.metadata = metadata;
                logOperation(queryExecutor_, auditEntry);
            }

            Json::Value error;
            error["success"] = false;
            error["message"] = result.errorMessage.empty() ? "Upload failed" : result.errorMessage;
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k500InternalServerError);
            callback(resp);
            return;
        }

        // Success - Start async processing
        // MANUAL mode: Stage 1 (parsing) runs automatically
        // AUTO mode: All stages run automatically
        processLdifFileAsync(result.uploadId, contentBytes);

        // Return success response
        Json::Value response;
        response["success"] = true;
        if (processingMode == "MANUAL" || processingMode == "manual") {
            response["message"] = "LDIF file uploaded successfully. Use parse/validate/ldap endpoints to process manually.";
        } else {
            response["message"] = result.message.empty() ? "LDIF file uploaded successfully. Processing started." : result.message;
        }

        Json::Value data;
        data["uploadId"] = result.uploadId;
        data["fileName"] = fileName;
        data["fileSize"] = static_cast<Json::Int64>(fileSize);
        data["status"] = result.status;
        data["createdAt"] = trantor::Date::now().toFormattedString(false);

        response["data"] = data;

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(drogon::k201Created);
        callback(resp);

        // Audit logging - FILE_UPLOAD success
        {
            AuditLogEntry auditEntry;
            auto [userId4, sessionUsername4] = extractUserFromRequest(req);
            auditEntry.userId = userId4;
            auditEntry.username = sessionUsername4;
            auditEntry.operationType = OperationType::FILE_UPLOAD;
            auditEntry.operationSubtype = "LDIF";
            auditEntry.resourceId = result.uploadId;
            auditEntry.resourceType = "UPLOADED_FILE";
            auditEntry.ipAddress = extractIpAddress(req);
            auditEntry.userAgent = req->getHeader("User-Agent");
            auditEntry.requestMethod = "POST";
            auditEntry.requestPath = "/api/upload/ldif";
            auditEntry.success = true;
            Json::Value metadata;
            metadata["fileName"] = fileName;
            metadata["fileSize"] = static_cast<Json::Int64>(fileSize);
            metadata["processingMode"] = processingMode;
            auditEntry.metadata = metadata;
            logOperation(queryExecutor_, auditEntry);
        }

    } catch (const std::exception& e) {
        spdlog::error("LDIF upload failed: {}", e.what());
        Json::Value error;
        error["success"] = false;
        error["message"] = std::string("Upload failed: ") + e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
    }
}

// =============================================================================
// POST /api/upload/masterlist
// =============================================================================

void UploadHandler::handleUploadMasterList(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    spdlog::info("POST /api/upload/masterlist - Master List file upload");

    try {
        // Parse multipart form data
        drogon::MultiPartParser parser;
        if (parser.parse(req) != 0) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "Invalid multipart form data";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        auto& files = parser.getFiles();
        if (files.empty()) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "No file uploaded";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        auto& file = files[0];
        std::string originalFileName = file.getFileName();

        // Sanitize filename to prevent path traversal attacks
        std::string fileName;
        try {
            fileName = sanitizeFilename(originalFileName);
        } catch (const std::exception& e) {
            Json::Value error;
            error["success"] = false;
            error["message"] = std::string("Invalid filename: ") + e.what();
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        std::string content = std::string(file.fileData(), file.fileLength());
        std::vector<uint8_t> contentBytes(content.begin(), content.end());
        int64_t fileSize = static_cast<int64_t>(content.size());

        // Validate PKCS#7/Master List file format
        if (!isValidP7sFile(contentBytes)) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "Invalid Master List file format. File must be a valid PKCS#7/CMS structure.";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            spdlog::warn("Invalid Master List file rejected: {}", originalFileName);
            return;
        }

        // Get processing mode from form data
        std::string processingMode = "AUTO";  // default
        auto& params = parser.getParameters();
        for (const auto& param : params) {
            if (param.first == "processingMode") {
                processingMode = param.second;
                break;
            }
        }

        // Get username from session
        std::string username = "anonymous";
        auto session = req->getSession();
        if (session) {
            auto [userId, sessionUsername] = extractUserFromRequest(req);
            username = sessionUsername.value_or("anonymous");
        }

        // Call UploadService to handle upload
        auto uploadResult = uploadService_->uploadMasterList(fileName, contentBytes, processingMode, username);

        // Handle duplicate file
        if (uploadResult.status == "DUPLICATE") {
            // Audit logging - FILE_UPLOAD failed (duplicate)
            {
                AuditLogEntry auditEntry;
                auto [userId5, sessionUsername5] = extractUserFromRequest(req);
                auditEntry.userId = userId5;
                auditEntry.username = sessionUsername5;
                auditEntry.operationType = OperationType::FILE_UPLOAD;
                auditEntry.operationSubtype = "MASTER_LIST";
                auditEntry.resourceType = "UPLOADED_FILE";
                auditEntry.ipAddress = extractIpAddress(req);
                auditEntry.userAgent = req->getHeader("User-Agent");
                auditEntry.requestMethod = "POST";
                auditEntry.requestPath = "/api/upload/masterlist";
                auditEntry.success = false;
                auditEntry.errorMessage = "Duplicate file detected";
                Json::Value metadata;
                metadata["fileName"] = fileName;
                metadata["fileSize"] = static_cast<Json::Int64>(fileSize);
                metadata["existingUploadId"] = uploadResult.uploadId;
                auditEntry.metadata = metadata;
                logOperation(queryExecutor_, auditEntry);
            }

            Json::Value error;
            error["success"] = false;
            error["message"] = uploadResult.message.empty() ? "Duplicate file detected. This file has already been uploaded." : uploadResult.message;

            Json::Value errorDetail;
            errorDetail["code"] = "DUPLICATE_FILE";
            errorDetail["detail"] = "A file with the same content (SHA-256 hash) already exists in the system.";
            error["error"] = errorDetail;

            Json::Value existingUpload;
            existingUpload["uploadId"] = uploadResult.uploadId;
            error["existingUpload"] = existingUpload;

            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k409Conflict);
            callback(resp);

            spdlog::warn("Duplicate Master List file upload rejected: existing_upload_id={}", uploadResult.uploadId);
            return;
        }

        // Handle upload failure
        if (!uploadResult.success) {
            // Audit logging - FILE_UPLOAD failed
            {
                AuditLogEntry auditEntry;
                auto [userId6, sessionUsername6] = extractUserFromRequest(req);
                auditEntry.userId = userId6;
                auditEntry.username = sessionUsername6;
                auditEntry.operationType = OperationType::FILE_UPLOAD;
                auditEntry.operationSubtype = "MASTER_LIST";
                auditEntry.resourceType = "UPLOADED_FILE";
                auditEntry.ipAddress = extractIpAddress(req);
                auditEntry.userAgent = req->getHeader("User-Agent");
                auditEntry.requestMethod = "POST";
                auditEntry.requestPath = "/api/upload/masterlist";
                auditEntry.success = false;
                auditEntry.errorMessage = uploadResult.errorMessage;
                Json::Value metadata;
                metadata["fileName"] = fileName;
                metadata["fileSize"] = static_cast<Json::Int64>(fileSize);
                auditEntry.metadata = metadata;
                logOperation(queryExecutor_, auditEntry);
            }

            Json::Value error;
            error["success"] = false;
            error["message"] = uploadResult.errorMessage.empty() ? "Upload failed" : uploadResult.errorMessage;
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k500InternalServerError);
            callback(resp);
            return;
        }

        // Success - Get upload ID from Service result
        std::string uploadId = uploadResult.uploadId;

        // Start async processing for both AUTO and MANUAL modes using Strategy Pattern
        // MANUAL mode: Stage 1 (parsing) runs automatically
        // AUTO mode: All stages run automatically
        // Capture member pointers for use in detached thread
        auto* uploadRepo = uploadRepository_;
        auto* qe = queryExecutor_;
        std::thread([this, uploadId, contentBytes, uploadRepo, qe]() {
                spdlog::info("Starting async Master List processing via Strategy for upload: {}", uploadId);

                // Use QueryExecutor (Oracle/PostgreSQL agnostic) instead of PGconn
                if (!qe) {
                    spdlog::error("QueryExecutor is null for async processing");
                    return;
                }

                // Get processing mode via UploadRepository
                std::string processingMode = "AUTO";
                try {
                    auto uploadOpt = uploadRepo->findById(uploadId);
                    if (uploadOpt.has_value() && uploadOpt->processingMode.has_value()) {
                        processingMode = uploadOpt->processingMode.value();
                    }
                } catch (const std::exception& e) {
                    spdlog::warn("Failed to get processing mode, defaulting to AUTO: {}", e.what());
                }

                spdlog::info("Processing mode for Master List upload {}: {}", uploadId, processingMode);

                // Connect to LDAP only if AUTO mode
                LDAP* ld = nullptr;
                if (processingMode == "AUTO") {
                    ld = this->getLdapWriteConnection();
                    if (!ld) {
                        spdlog::error("CRITICAL: LDAP write connection failed in AUTO mode for upload {}", uploadId);
                        spdlog::error("Cannot proceed - data consistency requires both DB and LDAP storage");

                        // Update upload status to FAILED via Repository
                        if (uploadRepo) {
                            uploadRepo->updateStatus(uploadId, "FAILED",
                                "LDAP connection failure - cannot ensure data consistency");
                        }

                        // Send failure progress
                        ProgressManager::getInstance().sendProgress(
                            ProcessingProgress::create(uploadId, ProcessingStage::FAILED,
                                0, 0, "LDAP 연결 실패", "데이터 일관성을 보장할 수 없어 처리를 중단했습니다."));

                        return;
                    }
                    spdlog::info("LDAP write connection established successfully for AUTO mode");
                }

                try {
                    // Use Strategy Pattern
                    auto strategy = ProcessingStrategyFactory::create(processingMode);
                    strategy->processMasterListContent(uploadId, contentBytes, ld);

                    // Query statistics from database via UploadRepository
                    int cscaCount = 0, totalEntries = 0, processedEntries = 0, mlscCount = 0;
                    try {
                        auto uploadOpt = uploadRepo->findById(uploadId);
                        if (uploadOpt.has_value()) {
                            cscaCount = uploadOpt->cscaCount;
                            totalEntries = uploadOpt->totalEntries;
                            processedEntries = uploadOpt->processedEntries;
                            mlscCount = uploadOpt->mlscCount;
                        }
                    } catch (const std::exception& e) {
                        spdlog::warn("Failed to query stats for completion message: {}", e.what());
                    }

                    int dupCount = totalEntries - processedEntries;
                    int totalCount = processedEntries + mlscCount;

                    spdlog::info("Master List processing completed - csca_count: {}, total_entries: {}, processed_entries: {}, mlsc_count: {}, dupCount: {}",
                                cscaCount, totalEntries, processedEntries, mlscCount, dupCount);

                    // Build detailed completion message
                    std::string completionMsg;
                    if (processingMode == "MANUAL") {
                        completionMsg = "Master List 파싱 완료 - 검증 대기";
                    } else {
                        completionMsg = "처리 완료: CSCA " + std::to_string(processedEntries);
                        if (dupCount > 0) {
                            completionMsg += " (중복 " + std::to_string(dupCount) + "개 건너뜀)";
                        }
                        if (mlscCount > 0) {
                            completionMsg += ", MLSC " + std::to_string(mlscCount);
                        }
                    }

                    // Send appropriate progress based on mode
                    if (processingMode == "MANUAL") {
                        ProgressManager::getInstance().sendProgress(
                            ProcessingProgress::create(uploadId, ProcessingStage::PARSING_COMPLETED,
                                totalCount, totalCount, completionMsg));
                    } else {
                        ProgressManager::getInstance().sendProgress(
                            ProcessingProgress::create(uploadId, ProcessingStage::COMPLETED,
                                totalCount, totalCount, completionMsg));
                    }

                } catch (const std::exception& e) {
                    spdlog::error("Master List processing via Strategy failed for upload {}: {}", uploadId, e.what());
                    ProgressManager::getInstance().sendProgress(
                        ProcessingProgress::create(uploadId, ProcessingStage::FAILED,
                            0, 0, "처리 실패", e.what()));
                }

                if (ld) ldap_unbind_ext_s(ld, nullptr, nullptr);
        }).detach();

        // Return success response
        Json::Value result;
        result["success"] = true;
        if (processingMode == "MANUAL" || processingMode == "manual") {
            result["message"] = "Master List file uploaded successfully. Use parse/validate/ldap endpoints to process manually.";
        } else {
            result["message"] = "Master List file uploaded successfully. Processing started.";
        }

        Json::Value data;
        data["uploadId"] = uploadId;
        data["fileName"] = fileName;
        data["fileSize"] = static_cast<Json::Int64>(fileSize);
        data["status"] = "PROCESSING";
        data["createdAt"] = trantor::Date::now().toFormattedString(false);

        result["data"] = data;

        auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(drogon::k201Created);
        callback(resp);

        // Audit logging - FILE_UPLOAD success
        {
            AuditLogEntry auditEntry;
            auto [userId7, username7] = extractUserFromRequest(req);
            auditEntry.userId = userId7;
            auditEntry.username = username7;
            auditEntry.operationType = OperationType::FILE_UPLOAD;
            auditEntry.operationSubtype = "MASTER_LIST";
            auditEntry.resourceId = uploadId;
            auditEntry.resourceType = "UPLOADED_FILE";
            auditEntry.ipAddress = extractIpAddress(req);
            auditEntry.userAgent = req->getHeader("User-Agent");
            auditEntry.requestMethod = "POST";
            auditEntry.requestPath = "/api/upload/masterlist";
            auditEntry.success = true;
            Json::Value metadata;
            metadata["fileName"] = fileName;
            metadata["fileSize"] = static_cast<Json::Int64>(fileSize);
            metadata["processingMode"] = processingMode;
            auditEntry.metadata = metadata;
            logOperation(queryExecutor_, auditEntry);
        }

    } catch (const std::exception& e) {
        spdlog::error("Master List upload failed: {}", e.what());
        Json::Value error;
        error["success"] = false;
        error["message"] = std::string("Upload failed: ") + e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
    }
}

// =============================================================================
// POST /api/upload/certificate
// =============================================================================

void UploadHandler::handleUploadCertificate(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    spdlog::info("POST /api/upload/certificate - Individual certificate file upload");

    try {
        drogon::MultiPartParser fileParser;
        if (fileParser.parse(req) != 0) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "Invalid multipart form data";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        auto& files = fileParser.getFiles();
        if (files.empty()) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "No file uploaded";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        auto& file = files[0];
        std::string fileName = file.getFileName();
        auto fileContent = file.fileContent();
        size_t fileSize = file.fileLength();

        spdlog::info("Certificate file: name={}, size={}", fileName, fileSize);

        // Validate file size (max 10MB for individual cert files)
        if (fileSize > 10 * 1024 * 1024) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "File too large. Maximum size is 10MB for certificate files.";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        // Extract username from JWT
        std::string uploadedBy = "unknown";
        auto jwtPayload = req->getAttributes()->get<Json::Value>("jwt_payload");
        if (jwtPayload.isMember("username")) {
            uploadedBy = jwtPayload["username"].asString();
        }

        // Convert to bytes
        std::vector<uint8_t> contentBytes(fileContent.begin(), fileContent.end());

        // Call UploadService
        auto result = uploadService_->uploadCertificate(fileName, contentBytes, uploadedBy);

        Json::Value response;
        response["success"] = result.success;
        response["message"] = result.message;
        response["uploadId"] = result.uploadId;
        response["fileFormat"] = result.fileFormat;
        response["status"] = result.status;
        response["certificateCount"] = result.certificateCount;
        response["cscaCount"] = result.cscaCount;
        response["dscCount"] = result.dscCount;
        response["dscNcCount"] = result.dscNcCount;
        response["mlscCount"] = result.mlscCount;
        response["crlCount"] = result.crlCount;
        response["ldapStoredCount"] = result.ldapStoredCount;
        response["duplicateCount"] = result.duplicateCount;
        if (!result.errorMessage.empty()) {
            response["errorMessage"] = result.errorMessage;
        }

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        if (result.success) {
            resp->setStatusCode(drogon::k200OK);
        } else if (result.status == "DUPLICATE") {
            resp->setStatusCode(drogon::k409Conflict);
        } else {
            resp->setStatusCode(drogon::k400BadRequest);
        }

        // Audit log
        if (queryExecutor_) {
            AuditLogEntry auditEntry;
            auditEntry.username = uploadedBy;
            auditEntry.operationType = OperationType::FILE_UPLOAD;
            auditEntry.operationSubtype = "CERTIFICATE_" + result.fileFormat;
            auditEntry.resourceId = result.uploadId;
            auditEntry.resourceType = "UPLOADED_FILE";
            auditEntry.ipAddress = extractIpAddress(req);
            auditEntry.userAgent = req->getHeader("User-Agent");
            auditEntry.requestMethod = "POST";
            auditEntry.requestPath = "/api/upload/certificate";
            auditEntry.success = result.success;
            Json::Value metadata;
            metadata["fileName"] = fileName;
            metadata["fileSize"] = static_cast<Json::Int64>(fileSize);
            metadata["fileFormat"] = result.fileFormat;
            metadata["certificateCount"] = result.certificateCount;
            metadata["crlCount"] = result.crlCount;
            auditEntry.metadata = metadata;
            logOperation(queryExecutor_, auditEntry);
        }

        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("Certificate upload failed: {}", e.what());
        Json::Value error;
        error["success"] = false;
        error["message"] = std::string("Upload failed: ") + e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
    }
}

// =============================================================================
// POST /api/upload/certificate/preview
// =============================================================================

void UploadHandler::handlePreviewCertificate(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback)
{
    spdlog::info("POST /api/upload/certificate/preview - Certificate file preview");

    try {
        drogon::MultiPartParser fileParser;
        if (fileParser.parse(req) != 0) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "Invalid multipart form data";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        auto& files = fileParser.getFiles();
        if (files.empty()) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "No file uploaded";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        auto& file = files[0];
        std::string fileName = file.getFileName();
        auto fileContent = file.fileContent();
        size_t fileSize = file.fileLength();

        // Validate file size (max 10MB)
        if (fileSize > 10 * 1024 * 1024) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "File too large. Maximum size is 10MB for certificate files.";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        std::vector<uint8_t> contentBytes(fileContent.begin(), fileContent.end());

        auto result = uploadService_->previewCertificate(fileName, contentBytes);

        Json::Value response;
        response["success"] = result.success;
        response["fileFormat"] = result.fileFormat;
        response["isDuplicate"] = result.isDuplicate;
        if (!result.duplicateUploadId.empty()) {
            response["duplicateUploadId"] = result.duplicateUploadId;
        }
        if (!result.message.empty()) {
            response["message"] = result.message;
        }
        if (!result.errorMessage.empty()) {
            response["errorMessage"] = result.errorMessage;
        }

        // Certificates array
        Json::Value certsArray(Json::arrayValue);
        for (const auto& cert : result.certificates) {
            Json::Value certJson;
            certJson["subjectDn"] = cert.subjectDn;
            certJson["issuerDn"] = cert.issuerDn;
            certJson["serialNumber"] = cert.serialNumber;
            certJson["countryCode"] = cert.countryCode;
            certJson["certificateType"] = cert.certificateType;
            certJson["isSelfSigned"] = cert.isSelfSigned;
            certJson["isLinkCertificate"] = cert.isLinkCertificate;
            certJson["notBefore"] = cert.notBefore;
            certJson["notAfter"] = cert.notAfter;
            certJson["isExpired"] = cert.isExpired;
            certJson["signatureAlgorithm"] = cert.signatureAlgorithm;
            certJson["publicKeyAlgorithm"] = cert.publicKeyAlgorithm;
            certJson["keySize"] = cert.keySize;
            certJson["fingerprintSha256"] = cert.fingerprintSha256;
            certsArray.append(certJson);
        }
        response["certificates"] = certsArray;

        // Deviations array (DL files)
        if (!result.deviations.empty()) {
            Json::Value devsArray(Json::arrayValue);
            for (const auto& dev : result.deviations) {
                Json::Value devJson;
                devJson["certificateIssuerDn"] = dev.certificateIssuerDn;
                devJson["certificateSerialNumber"] = dev.certificateSerialNumber;
                devJson["defectDescription"] = dev.defectDescription;
                devJson["defectTypeOid"] = dev.defectTypeOid;
                devJson["defectCategory"] = dev.defectCategory;
                devsArray.append(devJson);
            }
            response["deviations"] = devsArray;
            response["dlIssuerCountry"] = result.dlIssuerCountry;
            response["dlVersion"] = result.dlVersion;
            response["dlHashAlgorithm"] = result.dlHashAlgorithm;
            response["dlSignatureValid"] = result.dlSignatureValid;
            response["dlSigningTime"] = result.dlSigningTime;
            response["dlEContentType"] = result.dlEContentType;
            response["dlCmsDigestAlgorithm"] = result.dlCmsDigestAlgorithm;
            response["dlCmsSignatureAlgorithm"] = result.dlCmsSignatureAlgorithm;
            response["dlSignerDn"] = result.dlSignerDn;
        }

        // CRL info
        if (result.hasCrlInfo) {
            Json::Value crlJson;
            crlJson["issuerDn"] = result.crlInfo.issuerDn;
            crlJson["countryCode"] = result.crlInfo.countryCode;
            crlJson["thisUpdate"] = result.crlInfo.thisUpdate;
            crlJson["nextUpdate"] = result.crlInfo.nextUpdate;
            crlJson["crlNumber"] = result.crlInfo.crlNumber;
            crlJson["revokedCount"] = result.crlInfo.revokedCount;
            response["crlInfo"] = crlJson;
        }

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        resp->setStatusCode(drogon::k200OK);
        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("Certificate preview failed: {}", e.what());
        Json::Value error;
        error["success"] = false;
        error["message"] = std::string("Preview failed: ") + e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
    }
}

} // namespace handlers
