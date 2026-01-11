/**
 * @file main.cpp
 * @brief ICAO Local PKD Application Entry Point
 *
 * C++ REST API based ICAO Local PKD Management and
 * Passive Authentication (PA) Verification System.
 *
 * @author SmartCore Inc.
 * @date 2025-12-29
 */

#include <drogon/drogon.h>
#include <trantor/utils/Date.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <iostream>
#include <fstream>
#include <memory>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <array>
#include <thread>
#include <future>

// PostgreSQL header for direct connection test
#include <libpq-fe.h>

// OpenLDAP header
#include <ldap.h>

// OpenSSL for hash computation and certificate parsing
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
#include <iomanip>
#include <sstream>
#include <random>
#include <map>
#include <set>
#include <optional>
#include <regex>
#include <algorithm>
#include <cctype>

// Project headers
#include "common.h"
#include "processing_strategy.h"
#include "ldif_processor.h"

namespace {

/**
 * @brief Case-insensitive string search
 * @param haystack String to search in
 * @param needle String to search for
 * @return true if needle is found in haystack (case-insensitive)
 */
bool containsIgnoreCase(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;

    std::string haystackLower = haystack;
    std::string needleLower = needle;

    std::transform(haystackLower.begin(), haystackLower.end(), haystackLower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::transform(needleLower.begin(), needleLower.end(), needleLower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    return haystackLower.find(needleLower) != std::string::npos;
}

/**
 * @brief Application configuration
 */
struct AppConfig {
    std::string dbHost = "postgres";
    int dbPort = 5432;
    std::string dbName = "localpkd";
    std::string dbUser = "localpkd";
    std::string dbPassword = "localpkd123";

    // LDAP Read: HAProxy for load balancing across MMR nodes
    std::string ldapHost = "haproxy";
    int ldapPort = 389;
    // LDAP Write: Direct connection to primary master (openldap1) for write operations
    std::string ldapWriteHost = "openldap1";
    int ldapWritePort = 389;
    std::string ldapBindDn = "cn=admin,dc=ldap,dc=smartcoreinc,dc=com";
    std::string ldapBindPassword = "admin";
    std::string ldapBaseDn = "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";

    // Trust Anchor for Master List CMS signature verification
    std::string trustAnchorPath = "/app/data/cert/UN_CSCA_2.pem";

    int serverPort = 8081;
    int threadNum = 4;

    static AppConfig fromEnvironment() {
        AppConfig config;

        if (auto val = std::getenv("DB_HOST")) config.dbHost = val;
        if (auto val = std::getenv("DB_PORT")) config.dbPort = std::stoi(val);
        if (auto val = std::getenv("DB_NAME")) config.dbName = val;
        if (auto val = std::getenv("DB_USER")) config.dbUser = val;
        if (auto val = std::getenv("DB_PASSWORD")) config.dbPassword = val;

        if (auto val = std::getenv("LDAP_HOST")) config.ldapHost = val;
        if (auto val = std::getenv("LDAP_PORT")) config.ldapPort = std::stoi(val);
        if (auto val = std::getenv("LDAP_WRITE_HOST")) config.ldapWriteHost = val;
        if (auto val = std::getenv("LDAP_WRITE_PORT")) config.ldapWritePort = std::stoi(val);
        if (auto val = std::getenv("LDAP_BIND_DN")) config.ldapBindDn = val;
        if (auto val = std::getenv("LDAP_BIND_PASSWORD")) config.ldapBindPassword = val;
        if (auto val = std::getenv("LDAP_BASE_DN")) config.ldapBaseDn = val;

        if (auto val = std::getenv("SERVER_PORT")) config.serverPort = std::stoi(val);
        if (auto val = std::getenv("THREAD_NUM")) config.threadNum = std::stoi(val);
        if (auto val = std::getenv("TRUST_ANCHOR_PATH")) config.trustAnchorPath = val;

        return config;
    }
};

// Global configuration
AppConfig appConfig;

// =============================================================================
// SSE Progress Management
// =============================================================================

/**
 * @brief Processing stage enumeration
 */
enum class ProcessingStage {
    UPLOAD_COMPLETED,
    PARSING_STARTED,
    PARSING_IN_PROGRESS,
    PARSING_COMPLETED,
    VALIDATION_STARTED,
    VALIDATION_IN_PROGRESS,
    VALIDATION_COMPLETED,
    DB_SAVING_STARTED,
    DB_SAVING_IN_PROGRESS,
    DB_SAVING_COMPLETED,
    LDAP_SAVING_STARTED,
    LDAP_SAVING_IN_PROGRESS,
    LDAP_SAVING_COMPLETED,
    COMPLETED,
    FAILED
};

std::string stageToString(ProcessingStage stage) {
    switch (stage) {
        case ProcessingStage::UPLOAD_COMPLETED: return "UPLOAD_COMPLETED";
        case ProcessingStage::PARSING_STARTED: return "PARSING_STARTED";
        case ProcessingStage::PARSING_IN_PROGRESS: return "PARSING_IN_PROGRESS";
        case ProcessingStage::PARSING_COMPLETED: return "PARSING_COMPLETED";
        case ProcessingStage::VALIDATION_STARTED: return "VALIDATION_STARTED";
        case ProcessingStage::VALIDATION_IN_PROGRESS: return "VALIDATION_IN_PROGRESS";
        case ProcessingStage::VALIDATION_COMPLETED: return "VALIDATION_COMPLETED";
        case ProcessingStage::DB_SAVING_STARTED: return "DB_SAVING_STARTED";
        case ProcessingStage::DB_SAVING_IN_PROGRESS: return "DB_SAVING_IN_PROGRESS";
        case ProcessingStage::DB_SAVING_COMPLETED: return "DB_SAVING_COMPLETED";
        case ProcessingStage::LDAP_SAVING_STARTED: return "LDAP_SAVING_STARTED";
        case ProcessingStage::LDAP_SAVING_IN_PROGRESS: return "LDAP_SAVING_IN_PROGRESS";
        case ProcessingStage::LDAP_SAVING_COMPLETED: return "LDAP_SAVING_COMPLETED";
        case ProcessingStage::COMPLETED: return "COMPLETED";
        case ProcessingStage::FAILED: return "FAILED";
        default: return "UNKNOWN";
    }
}

std::string stageToKorean(ProcessingStage stage) {
    switch (stage) {
        case ProcessingStage::UPLOAD_COMPLETED: return "파일 업로드 완료";
        case ProcessingStage::PARSING_STARTED: return "파일 파싱 시작";
        case ProcessingStage::PARSING_IN_PROGRESS: return "파일 파싱 중";
        case ProcessingStage::PARSING_COMPLETED: return "파일 파싱 완료";
        case ProcessingStage::VALIDATION_STARTED: return "인증서 검증 시작";
        case ProcessingStage::VALIDATION_IN_PROGRESS: return "인증서 검증 중";
        case ProcessingStage::VALIDATION_COMPLETED: return "인증서 검증 완료";
        case ProcessingStage::DB_SAVING_STARTED: return "DB 저장 시작";
        case ProcessingStage::DB_SAVING_IN_PROGRESS: return "DB 저장 중";
        case ProcessingStage::DB_SAVING_COMPLETED: return "DB 저장 완료";
        case ProcessingStage::LDAP_SAVING_STARTED: return "LDAP 저장 시작";
        case ProcessingStage::LDAP_SAVING_IN_PROGRESS: return "LDAP 저장 중";
        case ProcessingStage::LDAP_SAVING_COMPLETED: return "LDAP 저장 완료";
        case ProcessingStage::COMPLETED: return "처리 완료";
        case ProcessingStage::FAILED: return "처리 실패";
        default: return "알 수 없음";
    }
}

int stageToBasePercentage(ProcessingStage stage) {
    switch (stage) {
        case ProcessingStage::UPLOAD_COMPLETED: return 5;
        case ProcessingStage::PARSING_STARTED: return 10;
        case ProcessingStage::PARSING_IN_PROGRESS: return 30;
        case ProcessingStage::PARSING_COMPLETED: return 50;
        case ProcessingStage::VALIDATION_STARTED: return 55;
        case ProcessingStage::VALIDATION_IN_PROGRESS: return 60;
        case ProcessingStage::VALIDATION_COMPLETED: return 70;
        case ProcessingStage::DB_SAVING_STARTED: return 72;
        case ProcessingStage::DB_SAVING_IN_PROGRESS: return 78;
        case ProcessingStage::DB_SAVING_COMPLETED: return 85;
        case ProcessingStage::LDAP_SAVING_STARTED: return 87;
        case ProcessingStage::LDAP_SAVING_IN_PROGRESS: return 93;
        case ProcessingStage::LDAP_SAVING_COMPLETED: return 100;
        case ProcessingStage::COMPLETED: return 100;
        case ProcessingStage::FAILED: return 0;
        default: return 0;
    }
}

/**
 * @brief Processing progress data
 */
struct ProcessingProgress {
    std::string uploadId;
    ProcessingStage stage;
    int percentage;
    int processedCount;
    int totalCount;
    std::string message;
    std::string errorMessage;
    std::string details;
    std::chrono::system_clock::time_point updatedAt;

    std::string toJson() const {
        Json::Value json;
        json["uploadId"] = uploadId;
        json["stage"] = stageToString(stage);
        json["stageName"] = stageToKorean(stage);
        json["percentage"] = percentage;
        json["processedCount"] = processedCount;
        json["totalCount"] = totalCount;
        json["message"] = message;
        json["errorMessage"] = errorMessage;
        json["details"] = details;

        auto time = std::chrono::system_clock::to_time_t(updatedAt);
        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%dT%H:%M:%S");
        json["updatedAt"] = ss.str();

        Json::StreamWriterBuilder writer;
        writer["indentation"] = "";  // Single-line JSON for SSE compatibility
        return Json::writeString(writer, json);
    }

    static ProcessingProgress create(
        const std::string& uploadId,
        ProcessingStage stage,
        int processedCount,
        int totalCount,
        const std::string& message,
        const std::string& errorMessage = "",
        const std::string& details = ""
    ) {
        ProcessingProgress p;
        p.uploadId = uploadId;
        p.stage = stage;
        p.processedCount = processedCount;
        p.totalCount = totalCount;
        p.message = message;
        p.errorMessage = errorMessage;
        p.details = details;
        p.updatedAt = std::chrono::system_clock::now();

        // Calculate percentage based on stage and progress
        int basePercent = stageToBasePercentage(stage);
        if (totalCount > 0 && processedCount > 0) {
            // Scale within stage range
            int nextPercent = 100;
            if (stage == ProcessingStage::PARSING_IN_PROGRESS) nextPercent = 50;
            else if (stage == ProcessingStage::DB_SAVING_IN_PROGRESS) nextPercent = 85;
            else if (stage == ProcessingStage::LDAP_SAVING_IN_PROGRESS) nextPercent = 100;

            int range = nextPercent - basePercent;
            p.percentage = basePercent + (range * processedCount / totalCount);
        } else {
            p.percentage = basePercent;
        }

        return p;
    }
};

/**
 * @brief SSE Progress Manager - Thread-safe progress tracking and SSE streaming
 */
class ProgressManager {
private:
    std::mutex mutex_;
    std::map<std::string, ProcessingProgress> progressCache_;
    std::map<std::string, std::function<void(const std::string&)>> sseCallbacks_;

public:
    static ProgressManager& getInstance() {
        static ProgressManager instance;
        return instance;
    }

    void sendProgress(const ProcessingProgress& progress) {
        std::lock_guard<std::mutex> lock(mutex_);
        progressCache_[progress.uploadId] = progress;

        // Send to SSE callback if registered
        auto it = sseCallbacks_.find(progress.uploadId);
        if (it != sseCallbacks_.end()) {
            try {
                std::string sseData = "event: progress\ndata: " + progress.toJson() + "\n\n";
                it->second(sseData);
            } catch (...) {
                sseCallbacks_.erase(it);
            }
        }

        spdlog::debug("Progress: {} - {} ({}%)", progress.uploadId, stageToString(progress.stage), progress.percentage);
    }

    void registerSseCallback(const std::string& uploadId, std::function<void(const std::string&)> callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        sseCallbacks_[uploadId] = callback;

        // Send cached progress if available
        auto it = progressCache_.find(uploadId);
        if (it != progressCache_.end()) {
            std::string sseData = "event: progress\ndata: " + it->second.toJson() + "\n\n";
            callback(sseData);
        }
    }

    void unregisterSseCallback(const std::string& uploadId) {
        std::lock_guard<std::mutex> lock(mutex_);
        sseCallbacks_.erase(uploadId);
    }

    std::optional<ProcessingProgress> getProgress(const std::string& uploadId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = progressCache_.find(uploadId);
        if (it != progressCache_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    void clearProgress(const std::string& uploadId) {
        std::lock_guard<std::mutex> lock(mutex_);
        progressCache_.erase(uploadId);
        sseCallbacks_.erase(uploadId);
    }
};

// =============================================================================
// Trust Anchor & CMS Signature Verification
// =============================================================================

/**
 * @brief Load UN_CSCA trust anchor certificate
 */
X509* loadTrustAnchor() {
    FILE* fp = fopen(appConfig.trustAnchorPath.c_str(), "r");
    if (!fp) {
        spdlog::error("Failed to open trust anchor file: {}", appConfig.trustAnchorPath);
        return nullptr;
    }

    X509* cert = PEM_read_X509(fp, nullptr, nullptr, nullptr);
    fclose(fp);

    if (!cert) {
        spdlog::error("Failed to parse trust anchor certificate");
        return nullptr;
    }

    spdlog::info("Trust anchor loaded: {}", appConfig.trustAnchorPath);
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

// =============================================================================
// CSCA Self-Signature Validation
// =============================================================================

/**
 * @brief Verify CSCA certificate is properly self-signed
 * CSCA must have:
 * 1. Subject DN == Issuer DN
 * 2. Valid self-signature (signature verifies with own public key)
 * 3. CA flag in Basic Constraints
 * 4. Key Usage: keyCertSign, cRLSign
 */
struct CscaValidationResult {
    bool isValid;
    bool isSelfSigned;
    bool signatureValid;
    bool isCa;
    bool hasKeyCertSign;
    std::string errorMessage;
};

/**
 * @brief DSC Trust Chain Validation Result
 */
struct DscValidationResult {
    bool isValid;
    bool cscaFound;
    bool signatureValid;
    bool notExpired;
    bool notRevoked;
    std::string cscaSubjectDn;
    std::string errorMessage;
};

CscaValidationResult validateCscaCertificate(X509* cert) {
    CscaValidationResult result = {false, false, false, false, false, ""};

    if (!cert) {
        result.errorMessage = "Certificate is null";
        return result;
    }

    // 1. Check if Subject DN == Issuer DN (self-signed check)
    X509_NAME* subject = X509_get_subject_name(cert);
    X509_NAME* issuer = X509_get_issuer_name(cert);

    if (X509_NAME_cmp(subject, issuer) == 0) {
        result.isSelfSigned = true;
    } else {
        result.errorMessage = "Certificate is not self-signed (Subject DN != Issuer DN)";
        return result;
    }

    // 2. Verify self-signature
    EVP_PKEY* pubKey = X509_get_pubkey(cert);
    if (!pubKey) {
        result.errorMessage = "Failed to extract public key from certificate";
        return result;
    }

    int verifyResult = X509_verify(cert, pubKey);
    EVP_PKEY_free(pubKey);

    if (verifyResult == 1) {
        result.signatureValid = true;
    } else {
        unsigned long err = ERR_get_error();
        char errBuf[256];
        ERR_error_string_n(err, errBuf, sizeof(errBuf));
        result.errorMessage = std::string("Self-signature verification failed: ") + errBuf;
        return result;
    }

    // 3. Check Basic Constraints (CA flag)
    BASIC_CONSTRAINTS* bc = static_cast<BASIC_CONSTRAINTS*>(
        X509_get_ext_d2i(cert, NID_basic_constraints, nullptr, nullptr));

    if (bc) {
        result.isCa = (bc->ca != 0);
        BASIC_CONSTRAINTS_free(bc);
    }

    // 4. Check Key Usage
    ASN1_BIT_STRING* keyUsage = static_cast<ASN1_BIT_STRING*>(
        X509_get_ext_d2i(cert, NID_key_usage, nullptr, nullptr));

    if (keyUsage) {
        // keyCertSign is bit 5, cRLSign is bit 6
        if (ASN1_BIT_STRING_get_bit(keyUsage, 5)) {
            result.hasKeyCertSign = true;
        }
        ASN1_BIT_STRING_free(keyUsage);
    }

    // Final validation: all conditions must be met for a valid CSCA
    if (result.isSelfSigned && result.signatureValid && result.isCa && result.hasKeyCertSign) {
        result.isValid = true;
    } else if (!result.isCa) {
        result.errorMessage = "Certificate does not have CA flag in Basic Constraints";
    } else if (!result.hasKeyCertSign) {
        result.errorMessage = "Certificate does not have keyCertSign in Key Usage";
    }

    return result;
}

/**
 * @brief Find CSCA certificate from DB by issuer DN
 * @return X509* pointer (caller must free) or nullptr if not found
 */
X509* findCscaByIssuerDn(PGconn* conn, const std::string& issuerDn) {
    if (!conn || issuerDn.empty()) return nullptr;

    // Query for CSCA with matching subject_dn
    std::string escapedDn = issuerDn;
    // Simple escape for SQL
    size_t pos = 0;
    while ((pos = escapedDn.find("'", pos)) != std::string::npos) {
        escapedDn.replace(pos, 1, "''");
        pos += 2;
    }

    // Use case-insensitive comparison for DN matching (RFC 4517)
    std::string query = "SELECT certificate_binary FROM certificate WHERE "
                        "certificate_type = 'CSCA' AND LOWER(subject_dn) = LOWER('" + escapedDn + "') LIMIT 1";

    spdlog::debug("CSCA lookup query: {}", query.substr(0, 200));

    PGresult* res = PQexec(conn, query.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        spdlog::error("CSCA lookup query failed: {} - Query: {}",
                     PQerrorMessage(conn), query.substr(0, 200));
        PQclear(res);
        return nullptr;
    }

    if (PQntuples(res) == 0) {
        spdlog::warn("CSCA not found for issuer DN: {}", escapedDn.substr(0, 80));
        PQclear(res);
        return nullptr;
    }

    // Get binary certificate data
    char* certData = PQgetvalue(res, 0, 0);
    int certLen = PQgetlength(res, 0, 0);

    spdlog::debug("CSCA found: certLen={}, first4chars='{}{}{}{}' (codes: {} {} {} {})",
                 certLen,
                 certLen > 0 ? certData[0] : '?',
                 certLen > 1 ? certData[1] : '?',
                 certLen > 2 ? certData[2] : '?',
                 certLen > 3 ? certData[3] : '?',
                 certLen > 0 ? (int)(unsigned char)certData[0] : -1,
                 certLen > 1 ? (int)(unsigned char)certData[1] : -1,
                 certLen > 2 ? (int)(unsigned char)certData[2] : -1,
                 certLen > 3 ? (int)(unsigned char)certData[3] : -1);

    if (!certData || certLen == 0) {
        spdlog::warn("CSCA lookup: empty certificate data");
        PQclear(res);
        return nullptr;
    }

    // Parse bytea hex format (PostgreSQL escape format: \x...)
    std::vector<uint8_t> derBytes;
    if (certLen > 2 && certData[0] == '\\' && certData[1] == 'x') {
        // Hex encoded
        spdlog::debug("CSCA lookup: parsing hex format, len={}", certLen);
        for (int i = 2; i < certLen; i += 2) {
            char hex[3] = {certData[i], certData[i+1], 0};
            derBytes.push_back(static_cast<uint8_t>(strtol(hex, nullptr, 16)));
        }
    } else {
        // Might be raw binary (if using binary mode) or escape format
        spdlog::debug("CSCA lookup: non-hex format detected, first byte code={}", (int)(unsigned char)certData[0]);
        // Try to detect if it's already DER binary (starts with 0x30 for SEQUENCE)
        if (certLen > 0 && (unsigned char)certData[0] == 0x30) {
            derBytes.assign(certData, certData + certLen);
            spdlog::debug("CSCA lookup: detected raw DER binary, len={}", derBytes.size());
        }
    }

    PQclear(res);

    if (derBytes.empty()) {
        spdlog::warn("CSCA lookup: failed to parse certificate binary data");
        return nullptr;
    }

    spdlog::debug("CSCA lookup: parsed {} DER bytes, first byte=0x{:02x}", derBytes.size(), derBytes[0]);

    const uint8_t* data = derBytes.data();
    X509* cert = d2i_X509(nullptr, &data, static_cast<long>(derBytes.size()));
    if (!cert) {
        unsigned long err = ERR_get_error();
        char errBuf[256];
        ERR_error_string_n(err, errBuf, sizeof(errBuf));
        spdlog::error("CSCA lookup: d2i_X509 failed: {}", errBuf);
        return nullptr;
    }

    spdlog::debug("CSCA lookup: successfully parsed X509 certificate");
    return cert;
}

/**
 * @brief Validate DSC certificate against its issuing CSCA
 * Checks:
 * 1. CSCA exists in DB
 * 2. DSC signature is valid (signed by CSCA)
 * 3. DSC is not expired
 */
DscValidationResult validateDscCertificate(PGconn* conn, X509* dscCert, const std::string& issuerDn) {
    DscValidationResult result = {false, false, false, false, false, "", ""};

    if (!dscCert) {
        result.errorMessage = "DSC certificate is null";
        return result;
    }

    // 1. Check expiration
    time_t now = time(nullptr);
    if (X509_cmp_time(X509_get0_notAfter(dscCert), &now) < 0) {
        result.errorMessage = "DSC certificate is expired";
        spdlog::warn("DSC validation: Certificate is EXPIRED");
        return result;
    }
    if (X509_cmp_time(X509_get0_notBefore(dscCert), &now) > 0) {
        result.errorMessage = "DSC certificate is not yet valid";
        spdlog::warn("DSC validation: Certificate is NOT YET VALID");
        return result;
    }
    result.notExpired = true;

    // 2. Find CSCA in DB
    X509* cscaCert = findCscaByIssuerDn(conn, issuerDn);
    if (!cscaCert) {
        result.errorMessage = "CSCA not found for issuer: " + issuerDn.substr(0, 80);
        spdlog::warn("DSC validation: CSCA NOT FOUND for issuer: {}", issuerDn.substr(0, 80));
        return result;
    }
    result.cscaFound = true;
    result.cscaSubjectDn = issuerDn;

    spdlog::info("DSC validation: Found CSCA for issuer: {}", issuerDn.substr(0, 80));

    // 3. Verify DSC signature with CSCA public key
    EVP_PKEY* cscaPubKey = X509_get_pubkey(cscaCert);
    if (!cscaPubKey) {
        result.errorMessage = "Failed to extract CSCA public key";
        X509_free(cscaCert);
        return result;
    }

    int verifyResult = X509_verify(dscCert, cscaPubKey);
    EVP_PKEY_free(cscaPubKey);
    X509_free(cscaCert);

    if (verifyResult == 1) {
        result.signatureValid = true;
        result.isValid = true;
        spdlog::info("DSC validation: Trust Chain VERIFIED - signature valid");
    } else {
        unsigned long err = ERR_get_error();
        char errBuf[256];
        ERR_error_string_n(err, errBuf, sizeof(errBuf));
        result.errorMessage = std::string("DSC signature verification failed: ") + errBuf;
        spdlog::error("DSC validation: Trust Chain FAILED - {}", result.errorMessage);
    }

    return result;
}

// =============================================================================
// Validation Result Storage
// =============================================================================

/**
 * @brief Structure to hold detailed validation result for storage
 */
struct ValidationResultRecord {
    std::string certificateId;
    std::string uploadId;
    std::string certificateType;
    std::string countryCode;
    std::string subjectDn;
    std::string issuerDn;
    std::string serialNumber;

    // Overall result
    std::string validationStatus;  // VALID, INVALID, PENDING, ERROR

    // Trust chain
    bool trustChainValid = false;
    std::string trustChainMessage;
    bool cscaFound = false;
    std::string cscaSubjectDn;
    std::string cscaFingerprint;
    bool signatureVerified = false;
    std::string signatureAlgorithm;

    // Validity period
    bool validityCheckPassed = false;
    bool isExpired = false;
    bool isNotYetValid = false;
    std::string notBefore;
    std::string notAfter;

    // CSCA specific
    bool isCa = false;
    bool isSelfSigned = false;
    int pathLengthConstraint = -1;

    // Key usage
    bool keyUsageValid = false;
    std::string keyUsageFlags;

    // CRL check
    std::string crlCheckStatus = "NOT_CHECKED";
    std::string crlCheckMessage;

    // Error
    std::string errorCode;
    std::string errorMessage;

    int validationDurationMs = 0;
};

/**
 * @brief Store validation result to database
 */
bool saveValidationResult(PGconn* conn, const ValidationResultRecord& record) {
    if (!conn) return false;

    // Escape strings for SQL
    auto escapeStr = [](const std::string& str) -> std::string {
        std::string result = str;
        size_t pos = 0;
        while ((pos = result.find("'", pos)) != std::string::npos) {
            result.replace(pos, 1, "''");
            pos += 2;
        }
        return result;
    };

    std::ostringstream sql;
    sql << "INSERT INTO validation_result ("
        << "certificate_id, upload_id, certificate_type, country_code, "
        << "subject_dn, issuer_dn, serial_number, "
        << "validation_status, trust_chain_valid, trust_chain_message, "
        << "csca_found, csca_subject_dn, csca_fingerprint, signature_verified, signature_algorithm, "
        << "validity_check_passed, is_expired, is_not_yet_valid, not_before, not_after, "
        << "is_ca, is_self_signed, path_length_constraint, "
        << "key_usage_valid, key_usage_flags, "
        << "crl_check_status, crl_check_message, "
        << "error_code, error_message, validation_duration_ms"
        << ") VALUES ("
        << "'" << escapeStr(record.certificateId) << "', "
        << "'" << escapeStr(record.uploadId) << "', "
        << "'" << escapeStr(record.certificateType) << "', "
        << "'" << escapeStr(record.countryCode) << "', "
        << "'" << escapeStr(record.subjectDn) << "', "
        << "'" << escapeStr(record.issuerDn) << "', "
        << "'" << escapeStr(record.serialNumber) << "', "
        << "'" << escapeStr(record.validationStatus) << "', "
        << (record.trustChainValid ? "TRUE" : "FALSE") << ", "
        << "'" << escapeStr(record.trustChainMessage) << "', "
        << (record.cscaFound ? "TRUE" : "FALSE") << ", "
        << "'" << escapeStr(record.cscaSubjectDn) << "', "
        << "'" << escapeStr(record.cscaFingerprint) << "', "
        << (record.signatureVerified ? "TRUE" : "FALSE") << ", "
        << "'" << escapeStr(record.signatureAlgorithm) << "', "
        << (record.validityCheckPassed ? "TRUE" : "FALSE") << ", "
        << (record.isExpired ? "TRUE" : "FALSE") << ", "
        << (record.isNotYetValid ? "TRUE" : "FALSE") << ", "
        << (record.notBefore.empty() ? "NULL" : ("'" + escapeStr(record.notBefore) + "'")) << ", "
        << (record.notAfter.empty() ? "NULL" : ("'" + escapeStr(record.notAfter) + "'")) << ", "
        << (record.isCa ? "TRUE" : "FALSE") << ", "
        << (record.isSelfSigned ? "TRUE" : "FALSE") << ", "
        << (record.pathLengthConstraint >= 0 ? std::to_string(record.pathLengthConstraint) : "NULL") << ", "
        << (record.keyUsageValid ? "TRUE" : "FALSE") << ", "
        << "'" << escapeStr(record.keyUsageFlags) << "', "
        << "'" << escapeStr(record.crlCheckStatus) << "', "
        << "'" << escapeStr(record.crlCheckMessage) << "', "
        << "'" << escapeStr(record.errorCode) << "', "
        << "'" << escapeStr(record.errorMessage) << "', "
        << record.validationDurationMs
        << ")";

    PGresult* res = PQexec(conn, sql.str().c_str());
    bool success = (PQresultStatus(res) == PGRES_COMMAND_OK);
    if (!success) {
        spdlog::error("Failed to save validation result: {}", PQerrorMessage(conn));
    }
    PQclear(res);

    return success;
}

} // Close anonymous namespace temporarily for extern functions

// Functions that need to be accessible from other compilation units
// (processing_strategy.cpp, ldif_processor.cpp)

/**
 * @brief Update validation statistics in uploaded_file table
 */
void updateValidationStatistics(PGconn* conn, const std::string& uploadId,
                                 int validCount, int invalidCount, int pendingCount, int errorCount,
                                 int trustChainValidCount, int trustChainInvalidCount, int cscaNotFoundCount,
                                 int expiredCount, int revokedCount) {
    std::ostringstream sql;
    sql << "UPDATE uploaded_file SET "
        << "validation_valid_count = " << validCount << ", "
        << "validation_invalid_count = " << invalidCount << ", "
        << "validation_pending_count = " << pendingCount << ", "
        << "validation_error_count = " << errorCount << ", "
        << "trust_chain_valid_count = " << trustChainValidCount << ", "
        << "trust_chain_invalid_count = " << trustChainInvalidCount << ", "
        << "csca_not_found_count = " << cscaNotFoundCount << ", "
        << "expired_count = " << expiredCount << ", "
        << "revoked_count = " << revokedCount
        << " WHERE id = '" << uploadId << "'";

    PGresult* res = PQexec(conn, sql.str().c_str());
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("Failed to update validation statistics: {}", PQerrorMessage(conn));
    }
    PQclear(res);
}

// =============================================================================
// Certificate Duplicate Check
// =============================================================================

/**
 * @brief Check if certificate with given fingerprint already exists in DB
 */
bool certificateExistsByFingerprint(PGconn* conn, const std::string& fingerprint) {
    std::string query = "SELECT 1 FROM certificate WHERE fingerprint_sha256 = '" + fingerprint + "' LIMIT 1";
    PGresult* res = PQexec(conn, query.c_str());

    bool exists = (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0);
    PQclear(res);

    return exists;
}

/**
 * @brief Check if a file with the same hash already exists
 * @param conn PostgreSQL connection
 * @param fileHash SHA-256 hash of the file
 * @return JSON object with existing upload details if duplicate found, null otherwise
 */
Json::Value checkDuplicateFile(PGconn* conn, const std::string& fileHash) {
    Json::Value result;  // null by default

    std::string query = "SELECT id, file_name, upload_timestamp, status, "
                       "processing_mode, file_format FROM uploaded_file "
                       "WHERE file_hash = '" + fileHash + "' LIMIT 1";

    PGresult* res = PQexec(conn, query.c_str());

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        spdlog::warn("Duplicate check query failed (continuing with upload): {}",
                     PQerrorMessage(conn));
        PQclear(res);
        return result;  // Fail open: allow upload if check fails
    }

    if (PQntuples(res) > 0) {
        result["uploadId"] = PQgetvalue(res, 0, 0);
        result["fileName"] = PQgetvalue(res, 0, 1);
        result["uploadTimestamp"] = PQgetvalue(res, 0, 2);
        result["status"] = PQgetvalue(res, 0, 3);
        result["processingMode"] = PQgetvalue(res, 0, 4);
        result["fileFormat"] = PQgetvalue(res, 0, 5);
    }

    PQclear(res);
    return result;
}

/**
 * @brief Initialize logging system
 */
void initializeLogging() {
    try {
        // Console sink (colored output)
        auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        console_sink->set_level(spdlog::level::debug);
        console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");

        // File sink (rotating)
        auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
            "logs/icao-local-pkd.log", 1024 * 1024 * 10, 5);
        file_sink->set_level(spdlog::level::info);
        file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] %v");

        // Multi-sink logger
        std::vector<spdlog::sink_ptr> sinks{console_sink, file_sink};
        auto logger = std::make_shared<spdlog::logger>("main", sinks.begin(), sinks.end());
        logger->set_level(spdlog::level::debug);

        spdlog::set_default_logger(logger);
        spdlog::flush_on(spdlog::level::warn);

        spdlog::info("Logging system initialized");
    } catch (const spdlog::spdlog_ex& ex) {
        std::cerr << "Log initialization failed: " << ex.what() << std::endl;
    }
}

/**
 * @brief Print application banner
 */
void printBanner() {
    std::cout << R"(
  _____ _____          ____    _                    _   ____  _  ______
 |_   _/ ____|   /\   / __ \  | |                  | | |  _ \| |/ /  _ \
   | || |       /  \ | |  | | | |     ___   ___ __ | | | |_) | ' /| | | |
   | || |      / /\ \| |  | | | |    / _ \ / __/ _` | | |  _ <|  < | | | |
  _| || |____ / ____ \ |__| | | |___| (_) | (_| (_| | | | |_) | . \| |_| |
 |_____\_____/_/    \_\____/  |______\___/ \___\__,_|_| |____/|_|\_\____/

)" << std::endl;
    std::cout << "  ICAO Local PKD Management & Passive Authentication System" << std::endl;
    std::cout << "  Version: 1.0.0" << std::endl;
    std::cout << "  (C) 2025 SmartCore Inc." << std::endl;
    std::cout << std::endl;
}

/**
 * @brief Check database connectivity using libpq directly
 */
Json::Value checkDatabase() {
    Json::Value result;
    result["name"] = "database";

    auto start = std::chrono::steady_clock::now();

    std::string conninfo = "host=" + appConfig.dbHost +
                          " port=" + std::to_string(appConfig.dbPort) +
                          " dbname=" + appConfig.dbName +
                          " user=" + appConfig.dbUser +
                          " password=" + appConfig.dbPassword +
                          " connect_timeout=5";

    PGconn* conn = PQconnectdb(conninfo.c_str());

    auto end = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    if (PQstatus(conn) == CONNECTION_OK) {
        // Execute a simple query to verify
        PGresult* res = PQexec(conn, "SELECT version()");
        if (PQresultStatus(res) == PGRES_TUPLES_OK) {
            result["status"] = "UP";
            result["responseTimeMs"] = static_cast<int>(duration.count());
            result["type"] = "PostgreSQL";
            result["version"] = PQgetvalue(res, 0, 0);
        } else {
            result["status"] = "DOWN";
            result["error"] = PQerrorMessage(conn);
        }
        PQclear(res);
    } else {
        result["status"] = "DOWN";
        result["error"] = PQerrorMessage(conn);
    }

    PQfinish(conn);
    return result;
}

/**
 * @brief Generate UUID v4
 */
std::string generateUuid() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;

    uint64_t ab = dis(gen);
    uint64_t cd = dis(gen);

    // Set version (4) and variant (RFC 4122)
    ab = (ab & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    cd = (cd & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;

    std::ostringstream ss;
    ss << std::hex << std::setfill('0');
    ss << std::setw(8) << (ab >> 32) << '-';
    ss << std::setw(4) << ((ab >> 16) & 0xFFFF) << '-';
    ss << std::setw(4) << (ab & 0xFFFF) << '-';
    ss << std::setw(4) << (cd >> 48) << '-';
    ss << std::setw(12) << (cd & 0x0000FFFFFFFFFFFFULL);

    return ss.str();
}

/**
 * @brief Compute SHA256 hash of content
 */
std::string computeFileHash(const std::vector<uint8_t>& content) {
    unsigned char hash[32];
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, content.data(), content.size());
    unsigned int len = 0;
    EVP_DigestFinal_ex(ctx, hash, &len);
    EVP_MD_CTX_free(ctx);

    std::stringstream ss;
    for (unsigned int i = 0; i < len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

/**
 * @brief Escape string for SQL
 */
std::string escapeSqlString(PGconn* conn, const std::string& str) {
    char* escaped = PQescapeLiteral(conn, str.c_str(), str.length());
    if (!escaped) return "''";
    std::string result(escaped);
    PQfreemem(escaped);
    return result;
}

/**
 * @brief Save upload record to database
 */
std::string saveUploadRecord(PGconn* conn, const std::string& fileName,
                              int64_t fileSize, const std::string& format,
                              const std::string& fileHash, const std::string& processingMode = "AUTO") {
    std::string uploadId = generateUuid();

    std::string query = "INSERT INTO uploaded_file (id, file_name, original_file_name, file_hash, "
                       "file_size, file_format, status, processing_mode, upload_timestamp) VALUES ("
                       "'" + uploadId + "', "
                       + escapeSqlString(conn, fileName) + ", "
                       + escapeSqlString(conn, fileName) + ", "
                       "'" + fileHash + "', "
                       + std::to_string(fileSize) + ", "
                       "'" + format + "', "
                       "'PROCESSING', "
                       "'" + processingMode + "', "
                       "NOW())";

    PGresult* res = PQexec(conn, query.c_str());
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::string error = PQerrorMessage(conn);
        PQclear(res);
        throw std::runtime_error("Failed to save upload record: " + error);
    }
    PQclear(res);

    return uploadId;
}

/**
 * @brief Update upload status in database
 */
void updateUploadStatus(PGconn* conn, const std::string& uploadId,
                        const std::string& status, int certCount, int crlCount,
                        const std::string& errorMessage) {
    std::string query = "UPDATE uploaded_file SET status = '" + status + "', "
                       "csca_count = " + std::to_string(certCount) + ", "
                       "crl_count = " + std::to_string(crlCount) + ", "
                       "error_message = " + escapeSqlString(conn, errorMessage) + ", "
                       "completed_timestamp = NOW() "
                       "WHERE id = '" + uploadId + "'";

    PGresult* res = PQexec(conn, query.c_str());
    PQclear(res);
}

/**
 * @brief Count LDIF entries in content
 */
int countLdifEntries(const std::string& content) {
    int count = 0;
    std::istringstream stream(content);
    std::string line;
    bool inEntry = false;

    while (std::getline(stream, line)) {
        // Remove trailing CR if present
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            if (inEntry) {
                count++;
                inEntry = false;
            }
        } else if (line.substr(0, 3) == "dn:") {
            inEntry = true;
        }
    }

    // Count last entry if exists
    if (inEntry) count++;

    return count;
}

// ============================================================================
// Certificate/CRL Parsing and DB Storage Functions
// ============================================================================

// Note: LdifEntry and ValidationStats are now in common.h

/**
 * @brief Base64 decode
 */
std::vector<uint8_t> base64Decode(const std::string& encoded) {
    static const std::string base64Chars =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    std::vector<uint8_t> result;
    std::vector<int> decodingTable(256, -1);
    for (size_t i = 0; i < base64Chars.size(); i++) {
        decodingTable[static_cast<unsigned char>(base64Chars[i])] = static_cast<int>(i);
    }

    int val = 0;
    int valb = -8;
    for (unsigned char c : encoded) {
        if (decodingTable[c] == -1) continue;
        val = (val << 6) + decodingTable[c];
        valb += 6;
        if (valb >= 0) {
            result.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return result;
}

/**
 * @brief Convert X509_NAME to string
 */
std::string x509NameToString(X509_NAME* name) {
    if (!name) return "";
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) return "";
    X509_NAME_print_ex(bio, name, 0, XN_FLAG_RFC2253);
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string result(data, len);
    BIO_free(bio);
    return result;
}

/**
 * @brief Convert ASN1_INTEGER to hex string
 */
std::string asn1IntegerToHex(const ASN1_INTEGER* asn1Int) {
    if (!asn1Int) return "";
    BIGNUM* bn = ASN1_INTEGER_to_BN(asn1Int, nullptr);
    if (!bn) return "";
    char* hex = BN_bn2hex(bn);
    std::string result(hex);
    OPENSSL_free(hex);
    BN_free(bn);
    return result;
}

/**
 * @brief Convert ASN1_TIME to ISO8601 string
 */
std::string asn1TimeToIso8601(const ASN1_TIME* asn1Time) {
    if (!asn1Time) return "";

    struct tm tm = {};
    const char* str = reinterpret_cast<const char*>(asn1Time->data);
    size_t len = asn1Time->length;

    if (asn1Time->type == V_ASN1_UTCTIME && len >= 12) {
        int year = (str[0] - '0') * 10 + (str[1] - '0');
        tm.tm_year = (year >= 50 ? 1900 : 2000) + year - 1900;
        tm.tm_mon = (str[2] - '0') * 10 + (str[3] - '0') - 1;
        tm.tm_mday = (str[4] - '0') * 10 + (str[5] - '0');
        tm.tm_hour = (str[6] - '0') * 10 + (str[7] - '0');
        tm.tm_min = (str[8] - '0') * 10 + (str[9] - '0');
        tm.tm_sec = (str[10] - '0') * 10 + (str[11] - '0');
    } else if (asn1Time->type == V_ASN1_GENERALIZEDTIME && len >= 14) {
        tm.tm_year = (str[0] - '0') * 1000 + (str[1] - '0') * 100 +
                     (str[2] - '0') * 10 + (str[3] - '0') - 1900;
        tm.tm_mon = (str[4] - '0') * 10 + (str[5] - '0') - 1;
        tm.tm_mday = (str[6] - '0') * 10 + (str[7] - '0');
        tm.tm_hour = (str[8] - '0') * 10 + (str[9] - '0');
        tm.tm_min = (str[10] - '0') * 10 + (str[11] - '0');
        tm.tm_sec = (str[12] - '0') * 10 + (str[13] - '0');
    }

    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buf) + "+00";
}

/**
 * @brief Extract country code from DN
 */
std::string extractCountryCode(const std::string& dn) {
    static const std::regex countryRegex(R"((?:^|,\s*)C=([A-Z]{2,3})(?:,|$))", std::regex::icase);
    std::smatch match;
    if (std::regex_search(dn, match, countryRegex)) {
        std::string code = match[1].str();
        for (char& c : code) {
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
        return code;
    }
    return "XX";  // Default unknown country
}

/**
 * @brief Parse LDIF content into entries
 */
std::vector<LdifEntry> parseLdifContent(const std::string& content) {
    std::vector<LdifEntry> entries;
    LdifEntry currentEntry;
    std::string currentAttrName;
    std::string currentAttrValue;
    bool inContinuation = false;

    std::istringstream stream(content);
    std::string line;

    auto finalizeAttribute = [&]() {
        if (!currentAttrName.empty()) {
            currentEntry.attributes[currentAttrName].push_back(currentAttrValue);
            currentAttrName.clear();
            currentAttrValue.clear();
        }
    };

    auto finalizeEntry = [&]() {
        finalizeAttribute();
        if (!currentEntry.dn.empty()) {
            entries.push_back(std::move(currentEntry));
            currentEntry = LdifEntry();
        }
    };

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            finalizeEntry();
            inContinuation = false;
            continue;
        }

        if (line[0] == '#') continue;

        if (line[0] == ' ') {
            // LDIF continuation line - append to current value
            if (inContinuation) {
                if (currentAttrName == "dn") {
                    // DN continuation - append to entry.dn
                    currentEntry.dn += line.substr(1);
                } else {
                    currentAttrValue += line.substr(1);
                }
            }
            continue;
        }

        finalizeAttribute();
        inContinuation = false;

        size_t colonPos = line.find(':');
        if (colonPos == std::string::npos) continue;

        currentAttrName = line.substr(0, colonPos);

        if (colonPos + 1 < line.size() && line[colonPos + 1] == ':') {
            // Base64 encoded value (double colon ::)
            // Only add ;binary suffix if not already present
            if (currentAttrName.find(";binary") == std::string::npos) {
                currentAttrName += ";binary";
            }
            size_t valueStart = colonPos + 2;
            while (valueStart < line.size() && line[valueStart] == ' ') valueStart++;
            currentAttrValue = line.substr(valueStart);
        } else {
            size_t valueStart = colonPos + 1;
            while (valueStart < line.size() && line[valueStart] == ' ') valueStart++;
            currentAttrValue = line.substr(valueStart);
        }

        if (currentAttrName == "dn") {
            currentEntry.dn = currentAttrValue;
            // Keep currentAttrName as "dn" for continuation line handling
            inContinuation = true;
        } else {
            inContinuation = true;
        }
    }

    finalizeEntry();
    return entries;
}

/**
 * @brief Escape binary data for PostgreSQL BYTEA
 */
std::string escapeBytea(PGconn* conn, const std::vector<uint8_t>& data) {
    if (data.size() >= 4) {
        spdlog::debug("escapeBytea input: size={}, first4bytes=0x{:02x}{:02x}{:02x}{:02x}",
                     data.size(), data[0], data[1], data[2], data[3]);
    }
    size_t toLen = 0;
    unsigned char* escaped = PQescapeByteaConn(conn, data.data(), data.size(), &toLen);
    if (!escaped) return "";
    std::string result(reinterpret_cast<char*>(escaped), toLen - 1);
    PQfreemem(escaped);
    spdlog::debug("escapeBytea output: len={}, first20chars={}", result.size(),
                 result.size() >= 20 ? result.substr(0, 20) : result);
    return result;
}

// =============================================================================
// LDAP Storage Functions
// =============================================================================

/**
 * @brief Get LDAP connection for write operations (direct to primary master)
 * In MMR setup, writes go to openldap1 directly to avoid replication conflicts
 */
LDAP* getLdapWriteConnection() {
    LDAP* ld = nullptr;
    std::string uri = "ldap://" + appConfig.ldapWriteHost + ":" + std::to_string(appConfig.ldapWritePort);

    int rc = ldap_initialize(&ld, uri.c_str());
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP write connection initialize failed: {}", ldap_err2string(rc));
        return nullptr;
    }

    int version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

    struct berval cred;
    cred.bv_val = const_cast<char*>(appConfig.ldapBindPassword.c_str());
    cred.bv_len = appConfig.ldapBindPassword.length();

    rc = ldap_sasl_bind_s(ld, appConfig.ldapBindDn.c_str(), LDAP_SASL_SIMPLE, &cred, nullptr, nullptr, nullptr);
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP write connection bind failed: {}", ldap_err2string(rc));
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        return nullptr;
    }

    return ld;
}

/**
 * @brief Get LDAP connection for read operations (via HAProxy load balancer)
 */
LDAP* getLdapReadConnection() {
    LDAP* ld = nullptr;
    std::string uri = "ldap://" + appConfig.ldapHost + ":" + std::to_string(appConfig.ldapPort);

    int rc = ldap_initialize(&ld, uri.c_str());
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP read connection initialize failed: {}", ldap_err2string(rc));
        return nullptr;
    }

    int version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

    struct berval cred;
    cred.bv_val = const_cast<char*>(appConfig.ldapBindPassword.c_str());
    cred.bv_len = appConfig.ldapBindPassword.length();

    rc = ldap_sasl_bind_s(ld, appConfig.ldapBindDn.c_str(), LDAP_SASL_SIMPLE, &cred, nullptr, nullptr, nullptr);
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP read connection bind failed: {}", ldap_err2string(rc));
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        return nullptr;
    }

    return ld;
}

/**
 * @brief Build LDAP DN for certificate
 * @param certType CSCA, DSC, or DSC_NC
 * @param countryCode ISO country code
 * @param fingerprint Certificate fingerprint (used as CN)
 */
std::string buildCertificateDn(const std::string& certType, const std::string& countryCode,
                                const std::string& fingerprint) {
    // ICAO PKD DIT structure:
    // dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
    //   └── dc=download
    //       └── dc=data (for CSCA, DSC)
    //           └── c={COUNTRY}
    //               ├── o=csca
    //               └── o=dsc
    //       └── dc=nc-data (for DSC_NC)
    //           └── c={COUNTRY}
    //               └── o=dsc

    std::string ou;
    std::string dataContainer;

    if (certType == "CSCA") {
        ou = "csca";
        dataContainer = "dc=data";
    } else if (certType == "DSC") {
        ou = "dsc";
        dataContainer = "dc=data";
    } else if (certType == "DSC_NC") {
        ou = "dsc";
        dataContainer = "dc=nc-data";
    } else {
        ou = "dsc";
        dataContainer = "dc=data";
    }

    return "cn=" + fingerprint.substr(0, 32) + ",o=" + ou + ",c=" + countryCode +
           "," + dataContainer + ",dc=download," + appConfig.ldapBaseDn;
}

/**
 * @brief Build LDAP DN for CRL
 */
std::string buildCrlDn(const std::string& countryCode, const std::string& fingerprint) {
    return "cn=" + fingerprint.substr(0, 32) + ",o=crl,c=" + countryCode +
           ",dc=data,dc=download," + appConfig.ldapBaseDn;
}

/**
 * @brief Ensure country organizational unit exists in LDAP
 */
bool ensureCountryOuExists(LDAP* ld, const std::string& countryCode, bool isNcData = false) {
    std::string dataContainer = isNcData ? "dc=nc-data" : "dc=data";
    std::string countryDn = "c=" + countryCode + "," + dataContainer + ",dc=download," + appConfig.ldapBaseDn;

    // Check if country entry exists
    LDAPMessage* result = nullptr;
    int rc = ldap_search_ext_s(ld, countryDn.c_str(), LDAP_SCOPE_BASE, "(objectClass=*)",
                                nullptr, 0, nullptr, nullptr, nullptr, 1, &result);

    if (result) {
        ldap_msgfree(result);
    }

    if (rc == LDAP_SUCCESS) {
        return true;  // Country already exists
    }

    if (rc != LDAP_NO_SUCH_OBJECT) {
        spdlog::warn("LDAP search for country {} failed: {}", countryCode, ldap_err2string(rc));
        return false;
    }

    // Create country entry
    LDAPMod modObjectClass;
    modObjectClass.mod_op = LDAP_MOD_ADD;
    modObjectClass.mod_type = const_cast<char*>("objectClass");
    char* ocVals[] = {const_cast<char*>("country"), const_cast<char*>("top"), nullptr};
    modObjectClass.mod_values = ocVals;

    LDAPMod modC;
    modC.mod_op = LDAP_MOD_ADD;
    modC.mod_type = const_cast<char*>("c");
    char* cVal[] = {const_cast<char*>(countryCode.c_str()), nullptr};
    modC.mod_values = cVal;

    LDAPMod* mods[] = {&modObjectClass, &modC, nullptr};

    rc = ldap_add_ext_s(ld, countryDn.c_str(), mods, nullptr, nullptr);
    if (rc != LDAP_SUCCESS && rc != LDAP_ALREADY_EXISTS) {
        spdlog::warn("Failed to create country entry {}: {}", countryDn, ldap_err2string(rc));
        return false;
    }

    // Create organizational units under country (csca, dsc, crl)
    std::vector<std::string> ous = isNcData ? std::vector<std::string>{"dsc"}
                                            : std::vector<std::string>{"csca", "dsc", "crl"};

    for (const auto& ouName : ous) {
        std::string ouDn = "o=" + ouName + "," + countryDn;

        LDAPMod ouObjClass;
        ouObjClass.mod_op = LDAP_MOD_ADD;
        ouObjClass.mod_type = const_cast<char*>("objectClass");
        char* ouOcVals[] = {const_cast<char*>("organization"), const_cast<char*>("top"), nullptr};
        ouObjClass.mod_values = ouOcVals;

        LDAPMod ouO;
        ouO.mod_op = LDAP_MOD_ADD;
        ouO.mod_type = const_cast<char*>("o");
        char* ouVal[] = {const_cast<char*>(ouName.c_str()), nullptr};
        ouO.mod_values = ouVal;

        LDAPMod* ouMods[] = {&ouObjClass, &ouO, nullptr};

        rc = ldap_add_ext_s(ld, ouDn.c_str(), ouMods, nullptr, nullptr);
        if (rc != LDAP_SUCCESS && rc != LDAP_ALREADY_EXISTS) {
            spdlog::debug("OU creation result for {}: {}", ouDn, ldap_err2string(rc));
        }
    }

    return true;
}

/**
 * @brief Save certificate to LDAP
 * @return LDAP DN or empty string on failure
 */
std::string saveCertificateToLdap(LDAP* ld, const std::string& certType,
                                   const std::string& countryCode,
                                   const std::string& subjectDn, const std::string& issuerDn,
                                   const std::string& serialNumber, const std::string& fingerprint,
                                   const std::vector<uint8_t>& certBinary) {
    bool isNcData = (certType == "DSC_NC");

    // Ensure country structure exists
    if (!ensureCountryOuExists(ld, countryCode, isNcData)) {
        spdlog::warn("Failed to ensure country OU exists for {}", countryCode);
        // Continue anyway - the OU might exist even if we couldn't create it
    }

    std::string dn = buildCertificateDn(certType, countryCode, fingerprint);

    // Build LDAP entry attributes
    // objectClass hierarchy: inetOrgPerson (structural) + pkdDownload (auxiliary, ICAO PKD custom schema)
    // inetOrgPerson <- organizationalPerson <- person <- top
    // Required attributes: cn (from person), sn (from person)
    LDAPMod modObjectClass;
    modObjectClass.mod_op = LDAP_MOD_ADD;
    modObjectClass.mod_type = const_cast<char*>("objectClass");
    char* ocVals[] = {
        const_cast<char*>("top"),
        const_cast<char*>("person"),
        const_cast<char*>("organizationalPerson"),
        const_cast<char*>("inetOrgPerson"),
        const_cast<char*>("pkdDownload"),
        nullptr
    };
    modObjectClass.mod_values = ocVals;

    // cn (subject DN - required by person)
    LDAPMod modCn;
    modCn.mod_op = LDAP_MOD_ADD;
    modCn.mod_type = const_cast<char*>("cn");
    char* cnVals[] = {const_cast<char*>(subjectDn.c_str()), nullptr};
    modCn.mod_values = cnVals;

    // sn (serial number - required by person)
    LDAPMod modSn;
    modSn.mod_op = LDAP_MOD_ADD;
    modSn.mod_type = const_cast<char*>("sn");
    char* snVals[] = {const_cast<char*>(serialNumber.c_str()), nullptr};
    modSn.mod_values = snVals;

    // userCertificate;binary (inetOrgPerson attribute for certificates)
    LDAPMod modCert;
    modCert.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
    modCert.mod_type = const_cast<char*>("userCertificate;binary");
    berval certBv;
    certBv.bv_val = reinterpret_cast<char*>(const_cast<uint8_t*>(certBinary.data()));
    certBv.bv_len = certBinary.size();
    berval* certBvVals[] = {&certBv, nullptr};
    modCert.mod_bvalues = certBvVals;

    LDAPMod* mods[] = {&modObjectClass, &modCn, &modSn, &modCert, nullptr};

    int rc = ldap_add_ext_s(ld, dn.c_str(), mods, nullptr, nullptr);

    if (rc == LDAP_ALREADY_EXISTS) {
        // Try to update the certificate
        LDAPMod modCertReplace;
        modCertReplace.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
        modCertReplace.mod_type = const_cast<char*>("userCertificate;binary");
        modCertReplace.mod_bvalues = certBvVals;

        LDAPMod* replaceMods[] = {&modCertReplace, nullptr};
        rc = ldap_modify_ext_s(ld, dn.c_str(), replaceMods, nullptr, nullptr);
    }

    if (rc != LDAP_SUCCESS) {
        spdlog::warn("Failed to save certificate to LDAP {}: {}", dn, ldap_err2string(rc));
        return "";
    }

    spdlog::debug("Saved certificate to LDAP: {}", dn);
    return dn;
}

/**
 * @brief Save CRL to LDAP
 * @return LDAP DN or empty string on failure
 */
std::string saveCrlToLdap(LDAP* ld, const std::string& countryCode,
                           const std::string& issuerDn, const std::string& fingerprint,
                           const std::vector<uint8_t>& crlBinary) {
    // Ensure country structure exists
    if (!ensureCountryOuExists(ld, countryCode, false)) {
        spdlog::warn("Failed to ensure country OU exists for CRL {}", countryCode);
    }

    std::string dn = buildCrlDn(countryCode, fingerprint);

    // Build LDAP entry attributes
    // objectClass: cRLDistributionPoint (structural) + pkdDownload (auxiliary)
    LDAPMod modObjectClass;
    modObjectClass.mod_op = LDAP_MOD_ADD;
    modObjectClass.mod_type = const_cast<char*>("objectClass");
    char* ocVals[] = {
        const_cast<char*>("top"),
        const_cast<char*>("cRLDistributionPoint"),
        const_cast<char*>("pkdDownload"),
        nullptr
    };
    modObjectClass.mod_values = ocVals;

    // cn
    LDAPMod modCn;
    modCn.mod_op = LDAP_MOD_ADD;
    modCn.mod_type = const_cast<char*>("cn");
    std::string cnValue = fingerprint.substr(0, 32);
    char* cnVals[] = {const_cast<char*>(cnValue.c_str()), nullptr};
    modCn.mod_values = cnVals;

    // certificateRevocationList;binary
    LDAPMod modCrl;
    modCrl.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
    modCrl.mod_type = const_cast<char*>("certificateRevocationList;binary");
    berval crlBv;
    crlBv.bv_val = reinterpret_cast<char*>(const_cast<uint8_t*>(crlBinary.data()));
    crlBv.bv_len = crlBinary.size();
    berval* crlBvVals[] = {&crlBv, nullptr};
    modCrl.mod_bvalues = crlBvVals;

    LDAPMod* mods[] = {&modObjectClass, &modCn, &modCrl, nullptr};

    int rc = ldap_add_ext_s(ld, dn.c_str(), mods, nullptr, nullptr);

    if (rc == LDAP_ALREADY_EXISTS) {
        // Update existing CRL
        LDAPMod modCrlReplace;
        modCrlReplace.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
        modCrlReplace.mod_type = const_cast<char*>("certificateRevocationList;binary");
        modCrlReplace.mod_bvalues = crlBvVals;

        LDAPMod* replaceMods[] = {&modCrlReplace, nullptr};
        rc = ldap_modify_ext_s(ld, dn.c_str(), replaceMods, nullptr, nullptr);
    }

    if (rc != LDAP_SUCCESS) {
        spdlog::warn("Failed to save CRL to LDAP {}: {}", dn, ldap_err2string(rc));
        return "";
    }

    spdlog::debug("Saved CRL to LDAP: {}", dn);
    return dn;
}

/**
 * @brief Update DB record with LDAP DN after successful LDAP storage
 */
void updateCertificateLdapStatus(PGconn* conn, const std::string& certId, const std::string& ldapDn) {
    if (ldapDn.empty()) return;

    std::string query = "UPDATE certificate SET "
                       "ldap_dn = " + escapeSqlString(conn, ldapDn) + ", "
                       "stored_in_ldap = TRUE, "
                       "stored_at = NOW() "
                       "WHERE id = '" + certId + "'";

    PGresult* res = PQexec(conn, query.c_str());
    PQclear(res);
}

/**
 * @brief Update CRL DB record with LDAP DN after successful LDAP storage
 */
void updateCrlLdapStatus(PGconn* conn, const std::string& crlId, const std::string& ldapDn) {
    if (ldapDn.empty()) return;

    std::string query = "UPDATE crl SET "
                       "ldap_dn = " + escapeSqlString(conn, ldapDn) + ", "
                       "stored_in_ldap = TRUE, "
                       "stored_at = NOW() "
                       "WHERE id = '" + crlId + "'";

    PGresult* res = PQexec(conn, query.c_str());
    PQclear(res);
}

/**
 * @brief Build DN for Master List entry in LDAP (o=ml node)
 * Format: cn={fingerprint},o=ml,c={country},dc=data,dc=download,dc=pkd,{baseDN}
 */
std::string buildMasterListDn(const std::string& countryCode, const std::string& fingerprint) {
    return "cn=" + fingerprint.substr(0, 32) + ",o=ml,c=" + countryCode +
           ",dc=data,dc=download," + appConfig.ldapBaseDn;
}

/**
 * @brief Ensure Master List OU (o=ml) exists under country entry
 */
bool ensureMasterListOuExists(LDAP* ld, const std::string& countryCode) {
    std::string countryDn = "c=" + countryCode + ",dc=data,dc=download," + appConfig.ldapBaseDn;

    // First ensure country exists
    LDAPMessage* result = nullptr;
    int rc = ldap_search_ext_s(ld, countryDn.c_str(), LDAP_SCOPE_BASE, "(objectClass=*)",
                                nullptr, 0, nullptr, nullptr, nullptr, 1, &result);
    if (result) ldap_msgfree(result);

    if (rc == LDAP_NO_SUCH_OBJECT) {
        // Create country entry
        LDAPMod modObjectClass;
        modObjectClass.mod_op = LDAP_MOD_ADD;
        modObjectClass.mod_type = const_cast<char*>("objectClass");
        char* ocVals[] = {const_cast<char*>("country"), const_cast<char*>("top"), nullptr};
        modObjectClass.mod_values = ocVals;

        LDAPMod modC;
        modC.mod_op = LDAP_MOD_ADD;
        modC.mod_type = const_cast<char*>("c");
        char* cVal[] = {const_cast<char*>(countryCode.c_str()), nullptr};
        modC.mod_values = cVal;

        LDAPMod* mods[] = {&modObjectClass, &modC, nullptr};
        rc = ldap_add_ext_s(ld, countryDn.c_str(), mods, nullptr, nullptr);
        if (rc != LDAP_SUCCESS && rc != LDAP_ALREADY_EXISTS) {
            spdlog::warn("Failed to create country entry for ML {}: {}", countryDn, ldap_err2string(rc));
            return false;
        }
    }

    // Create o=ml OU under country
    std::string mlOuDn = "o=ml," + countryDn;
    result = nullptr;
    rc = ldap_search_ext_s(ld, mlOuDn.c_str(), LDAP_SCOPE_BASE, "(objectClass=*)",
                            nullptr, 0, nullptr, nullptr, nullptr, 1, &result);
    if (result) ldap_msgfree(result);

    if (rc == LDAP_NO_SUCH_OBJECT) {
        LDAPMod ouObjClass;
        ouObjClass.mod_op = LDAP_MOD_ADD;
        ouObjClass.mod_type = const_cast<char*>("objectClass");
        char* ouOcVals[] = {const_cast<char*>("organization"), const_cast<char*>("top"), nullptr};
        ouObjClass.mod_values = ouOcVals;

        LDAPMod ouO;
        ouO.mod_op = LDAP_MOD_ADD;
        ouO.mod_type = const_cast<char*>("o");
        char* ouVal[] = {const_cast<char*>("ml"), nullptr};
        ouO.mod_values = ouVal;

        LDAPMod* ouMods[] = {&ouObjClass, &ouO, nullptr};
        rc = ldap_add_ext_s(ld, mlOuDn.c_str(), ouMods, nullptr, nullptr);
        if (rc != LDAP_SUCCESS && rc != LDAP_ALREADY_EXISTS) {
            spdlog::debug("ML OU creation result for {}: {}", mlOuDn, ldap_err2string(rc));
        }
    }

    return true;
}

/**
 * @brief Save Master List to LDAP (o=ml node)
 * @return LDAP DN or empty string on failure
 */
std::string saveMasterListToLdap(LDAP* ld, const std::string& countryCode,
                                  const std::string& signerDn, const std::string& fingerprint,
                                  const std::vector<uint8_t>& mlBinary) {
    // Ensure o=ml structure exists
    if (!ensureMasterListOuExists(ld, countryCode)) {
        spdlog::warn("Failed to ensure ML OU exists for {}", countryCode);
    }

    std::string dn = buildMasterListDn(countryCode, fingerprint);

    // Build LDAP entry attributes
    // objectClass: person (structural) + pkdMasterList + pkdDownload (auxiliary)
    LDAPMod modObjectClass;
    modObjectClass.mod_op = LDAP_MOD_ADD;
    modObjectClass.mod_type = const_cast<char*>("objectClass");
    char* ocVals[] = {
        const_cast<char*>("top"),
        const_cast<char*>("person"),
        const_cast<char*>("pkdMasterList"),
        const_cast<char*>("pkdDownload"),
        nullptr
    };
    modObjectClass.mod_values = ocVals;

    // cn (required for person)
    LDAPMod modCn;
    modCn.mod_op = LDAP_MOD_ADD;
    modCn.mod_type = const_cast<char*>("cn");
    std::string cnValue = fingerprint.substr(0, 32);
    char* cnVals[] = {const_cast<char*>(cnValue.c_str()), nullptr};
    modCn.mod_values = cnVals;

    // sn (required for person - use "1" as serial)
    LDAPMod modSn;
    modSn.mod_op = LDAP_MOD_ADD;
    modSn.mod_type = const_cast<char*>("sn");
    char* snVals[] = {const_cast<char*>("1"), nullptr};
    modSn.mod_values = snVals;

    // pkdMasterListContent (binary - the CMS signed Master List)
    LDAPMod modMlContent;
    modMlContent.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
    modMlContent.mod_type = const_cast<char*>("pkdMasterListContent");
    berval mlBv;
    mlBv.bv_val = reinterpret_cast<char*>(const_cast<uint8_t*>(mlBinary.data()));
    mlBv.bv_len = mlBinary.size();
    berval* mlBvVals[] = {&mlBv, nullptr};
    modMlContent.mod_bvalues = mlBvVals;

    // pkdVersion
    LDAPMod modVersion;
    modVersion.mod_op = LDAP_MOD_ADD;
    modVersion.mod_type = const_cast<char*>("pkdVersion");
    char* versionVals[] = {const_cast<char*>("70"), nullptr};
    modVersion.mod_values = versionVals;

    LDAPMod* mods[] = {&modObjectClass, &modCn, &modSn, &modMlContent, &modVersion, nullptr};

    int rc = ldap_add_ext_s(ld, dn.c_str(), mods, nullptr, nullptr);

    if (rc == LDAP_ALREADY_EXISTS) {
        // Update existing Master List
        LDAPMod modMlReplace;
        modMlReplace.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
        modMlReplace.mod_type = const_cast<char*>("pkdMasterListContent");
        modMlReplace.mod_bvalues = mlBvVals;

        LDAPMod* replaceMods[] = {&modMlReplace, nullptr};
        rc = ldap_modify_ext_s(ld, dn.c_str(), replaceMods, nullptr, nullptr);
    }

    if (rc != LDAP_SUCCESS) {
        spdlog::warn("Failed to save Master List to LDAP {}: {}", dn, ldap_err2string(rc));
        return "";
    }

    spdlog::info("Saved Master List to LDAP: {} (country: {})", dn, countryCode);
    return dn;
}

/**
 * @brief Update Master List DB record with LDAP DN after successful LDAP storage
 */
void updateMasterListLdapStatus(PGconn* conn, const std::string& mlId, const std::string& ldapDn) {
    if (ldapDn.empty()) return;

    std::string query = "UPDATE master_list SET "
                       "ldap_dn = " + escapeSqlString(conn, ldapDn) + ", "
                       "stored_in_ldap = TRUE, "
                       "stored_at = NOW() "
                       "WHERE id = '" + mlId + "'";

    PGresult* res = PQexec(conn, query.c_str());
    PQclear(res);
}

// =============================================================================
// Database Storage Functions
// =============================================================================

/**
 * @brief Save certificate to database
 * @return certificate ID or empty string on failure
 */
std::string saveCertificate(PGconn* conn, const std::string& uploadId,
                            const std::string& certType, const std::string& countryCode,
                            const std::string& subjectDn, const std::string& issuerDn,
                            const std::string& serialNumber, const std::string& fingerprint,
                            const std::string& notBefore, const std::string& notAfter,
                            const std::vector<uint8_t>& certBinary,
                            const std::string& validationStatus = "PENDING",
                            const std::string& validationMessage = "") {
    std::string certId = generateUuid();
    std::string byteaEscaped = escapeBytea(conn, certBinary);

    std::string query = "INSERT INTO certificate (id, upload_id, certificate_type, country_code, "
                       "subject_dn, issuer_dn, serial_number, fingerprint_sha256, "
                       "not_before, not_after, certificate_binary, validation_status, validation_message, created_at) VALUES ("
                       "'" + certId + "', "
                       "'" + uploadId + "', "
                       + escapeSqlString(conn, certType) + ", "
                       + escapeSqlString(conn, countryCode) + ", "
                       + escapeSqlString(conn, subjectDn) + ", "
                       + escapeSqlString(conn, issuerDn) + ", "
                       + escapeSqlString(conn, serialNumber) + ", "
                       "'" + fingerprint + "', "
                       "'" + notBefore + "', "
                       "'" + notAfter + "', "
                       "'" + byteaEscaped + "', "
                       + escapeSqlString(conn, validationStatus) + ", "
                       + escapeSqlString(conn, validationMessage) + ", NOW()) "
                       "ON CONFLICT (certificate_type, fingerprint_sha256) DO NOTHING";

    PGresult* res = PQexec(conn, query.c_str());
    bool success = (PQresultStatus(res) == PGRES_COMMAND_OK);
    PQclear(res);

    return success ? certId : "";
}

/**
 * @brief Save CRL to database
 * @return CRL ID or empty string on failure
 */
std::string saveCrl(PGconn* conn, const std::string& uploadId,
                    const std::string& countryCode, const std::string& issuerDn,
                    const std::string& thisUpdate, const std::string& nextUpdate,
                    const std::string& crlNumber, const std::string& fingerprint,
                    const std::vector<uint8_t>& crlBinary) {
    std::string crlId = generateUuid();
    std::string byteaEscaped = escapeBytea(conn, crlBinary);

    std::string query = "INSERT INTO crl (id, upload_id, country_code, issuer_dn, "
                       "this_update, next_update, crl_number, fingerprint_sha256, "
                       "crl_binary, validation_status, created_at) VALUES ("
                       "'" + crlId + "', "
                       "'" + uploadId + "', "
                       + escapeSqlString(conn, countryCode) + ", "
                       + escapeSqlString(conn, issuerDn) + ", "
                       "'" + thisUpdate + "', "
                       + (nextUpdate.empty() ? "NULL" : "'" + nextUpdate + "'") + ", "
                       + escapeSqlString(conn, crlNumber) + ", "
                       "'" + fingerprint + "', "
                       "'" + byteaEscaped + "', "
                       "'PENDING', NOW()) "
                       "ON CONFLICT DO NOTHING";

    PGresult* res = PQexec(conn, query.c_str());
    bool success = (PQresultStatus(res) == PGRES_COMMAND_OK);
    PQclear(res);

    return success ? crlId : "";
}

/**
 * @brief Save revoked certificate to database
 */
void saveRevokedCertificate(PGconn* conn, const std::string& crlId,
                            const std::string& serialNumber, const std::string& revocationDate,
                            const std::string& reason) {
    std::string query = "INSERT INTO revoked_certificate (id, crl_id, serial_number, "
                       "revocation_date, revocation_reason, created_at) VALUES ("
                       "'" + generateUuid() + "', "
                       "'" + crlId + "', "
                       + escapeSqlString(conn, serialNumber) + ", "
                       "'" + revocationDate + "', "
                       + escapeSqlString(conn, reason) + ", "
                       "NOW())";

    PGresult* res = PQexec(conn, query.c_str());
    PQclear(res);
}

/**
 * @brief Save Master List to database
 * @return Master List ID or empty string on failure
 */
std::string saveMasterList(PGconn* conn, const std::string& uploadId,
                            const std::string& countryCode, const std::string& signerDn,
                            const std::string& fingerprint, int cscaCount,
                            const std::vector<uint8_t>& mlBinary) {
    std::string mlId = generateUuid();
    std::string byteaEscaped = escapeBytea(conn, mlBinary);

    std::string query = "INSERT INTO master_list (id, upload_id, signer_country, signer_dn, "
                       "fingerprint_sha256, csca_certificate_count, ml_binary, created_at) VALUES ("
                       "'" + mlId + "', "
                       "'" + uploadId + "', "
                       + escapeSqlString(conn, countryCode) + ", "
                       + escapeSqlString(conn, signerDn) + ", "
                       "'" + fingerprint + "', "
                       + std::to_string(cscaCount) + ", "
                       "'" + byteaEscaped + "', NOW()) "
                       "ON CONFLICT DO NOTHING";

    PGresult* res = PQexec(conn, query.c_str());
    bool success = (PQresultStatus(res) == PGRES_COMMAND_OK);
    PQclear(res);

    return success ? mlId : "";
}

/**
 * @brief Parse and save certificate from LDIF entry (DB + LDAP)
 */
bool parseCertificateEntry(PGconn* conn, LDAP* ld, const std::string& uploadId,
                           const LdifEntry& entry, const std::string& attrName,
                           int& cscaCount, int& dscCount, int& dscNcCount, int& ldapStoredCount,
                           ValidationStats& validationStats) {
    std::string base64Value = entry.getFirstAttribute(attrName);
    if (base64Value.empty()) return false;

    spdlog::debug("parseCertificateEntry: base64Value len={}, first20chars={}",
                 base64Value.size(), base64Value.substr(0, 20));

    std::vector<uint8_t> derBytes = base64Decode(base64Value);
    if (derBytes.empty()) return false;

    spdlog::debug("parseCertificateEntry: derBytes size={}, first4bytes=0x{:02x}{:02x}{:02x}{:02x}",
                 derBytes.size(),
                 derBytes.size() > 0 ? derBytes[0] : 0,
                 derBytes.size() > 1 ? derBytes[1] : 0,
                 derBytes.size() > 2 ? derBytes[2] : 0,
                 derBytes.size() > 3 ? derBytes[3] : 0);

    const uint8_t* data = derBytes.data();
    X509* cert = d2i_X509(nullptr, &data, static_cast<long>(derBytes.size()));
    if (!cert) {
        spdlog::warn("Failed to parse certificate from entry: {}", entry.dn);
        return false;
    }

    std::string subjectDn = x509NameToString(X509_get_subject_name(cert));
    std::string issuerDn = x509NameToString(X509_get_issuer_name(cert));
    std::string serialNumber = asn1IntegerToHex(X509_get_serialNumber(cert));
    std::string notBefore = asn1TimeToIso8601(X509_get0_notBefore(cert));
    std::string notAfter = asn1TimeToIso8601(X509_get0_notAfter(cert));
    std::string fingerprint = computeFileHash(derBytes);
    std::string countryCode = extractCountryCode(subjectDn);
    if (countryCode == "XX") {
        countryCode = extractCountryCode(issuerDn);
    }

    // Determine certificate type and perform validation
    std::string certType;
    std::string validationStatus = "PENDING";
    std::string validationMessage = "";

    // Prepare validation result record
    ValidationResultRecord valRecord;
    valRecord.uploadId = uploadId;
    valRecord.countryCode = countryCode;
    valRecord.subjectDn = subjectDn;
    valRecord.issuerDn = issuerDn;
    valRecord.serialNumber = serialNumber;
    valRecord.notBefore = notBefore;
    valRecord.notAfter = notAfter;

    auto startTime = std::chrono::high_resolution_clock::now();

    if (subjectDn == issuerDn) {
        // CSCA - self-signed certificate
        certType = "CSCA";
        cscaCount++;
        valRecord.certificateType = "CSCA";
        valRecord.isSelfSigned = true;

        // Validate CSCA self-signature
        auto cscaValidation = validateCscaCertificate(cert);
        valRecord.isCa = cscaValidation.isCa;
        valRecord.signatureVerified = cscaValidation.signatureValid;
        valRecord.validityCheckPassed = cscaValidation.isValid;  // isValid includes validity period check
        valRecord.keyUsageValid = cscaValidation.hasKeyCertSign;
        valRecord.trustChainValid = cscaValidation.signatureValid;  // Self-signed trust chain = signature valid

        if (cscaValidation.isValid) {
            validationStatus = "VALID";
            valRecord.validationStatus = "VALID";
            valRecord.trustChainMessage = "Self-signature verified";
            validationStats.validCount++;
            validationStats.trustChainValidCount++;
            spdlog::info("CSCA validation: VERIFIED - self-signature valid for {}", countryCode);
        } else if (cscaValidation.signatureValid) {
            validationStatus = "VALID";  // Signature valid but other issues
            validationMessage = cscaValidation.errorMessage;
            valRecord.validationStatus = "VALID";
            valRecord.trustChainMessage = cscaValidation.errorMessage;
            validationStats.validCount++;
            validationStats.trustChainValidCount++;
            spdlog::warn("CSCA validation: WARNING - {} for {}", cscaValidation.errorMessage, countryCode);
        } else {
            validationStatus = "INVALID";
            validationMessage = cscaValidation.errorMessage;
            valRecord.validationStatus = "INVALID";
            valRecord.trustChainValid = false;
            valRecord.trustChainMessage = cscaValidation.errorMessage;
            valRecord.errorMessage = cscaValidation.errorMessage;
            validationStats.invalidCount++;
            validationStats.trustChainInvalidCount++;
            spdlog::error("CSCA validation: FAILED - {} for {}", cscaValidation.errorMessage, countryCode);
        }
    } else if (containsIgnoreCase(entry.dn, "dc=nc-data")) {
        // Non-Conformant DSC - detected by dc=nc-data in LDIF DN path (case-insensitive)
        certType = "DSC_NC";
        dscNcCount++;
        valRecord.certificateType = "DSC_NC";
        spdlog::info("Detected DSC_NC certificate from nc-data path: dn={}", entry.dn);

        // DSC_NC - perform trust chain validation
        auto dscValidation = validateDscCertificate(conn, cert, issuerDn);
        valRecord.cscaFound = dscValidation.cscaFound;
        valRecord.cscaSubjectDn = dscValidation.cscaSubjectDn;
        valRecord.signatureVerified = dscValidation.signatureValid;
        valRecord.validityCheckPassed = dscValidation.notExpired;
        valRecord.isExpired = !dscValidation.notExpired;

        if (dscValidation.isValid) {
            validationStatus = "VALID";
            valRecord.validationStatus = "VALID";
            valRecord.trustChainValid = true;
            valRecord.trustChainMessage = "Trust chain verified: DSC signed by CSCA";
            validationStats.validCount++;
            validationStats.trustChainValidCount++;
            spdlog::info("DSC_NC validation: Trust Chain VERIFIED for {} (issuer: {})",
                        countryCode, issuerDn.substr(0, 50));
        } else if (dscValidation.cscaFound) {
            validationStatus = "INVALID";
            validationMessage = dscValidation.errorMessage;
            valRecord.validationStatus = "INVALID";
            valRecord.trustChainValid = false;
            valRecord.trustChainMessage = dscValidation.errorMessage;
            valRecord.errorMessage = dscValidation.errorMessage;
            validationStats.invalidCount++;
            validationStats.trustChainInvalidCount++;
            if (!dscValidation.notExpired) validationStats.expiredCount++;
            spdlog::error("DSC_NC validation: Trust Chain FAILED - {} for {}",
                         dscValidation.errorMessage, countryCode);
        } else {
            validationStatus = "PENDING";
            validationMessage = dscValidation.errorMessage;
            valRecord.validationStatus = "PENDING";
            valRecord.trustChainMessage = "CSCA not found in database";
            valRecord.errorCode = "CSCA_NOT_FOUND";
            valRecord.errorMessage = dscValidation.errorMessage;
            validationStats.pendingCount++;
            validationStats.cscaNotFoundCount++;
            spdlog::warn("DSC_NC validation: CSCA not found - {} for {}",
                        dscValidation.errorMessage, countryCode);
        }
    } else {
        certType = "DSC";
        dscCount++;
        valRecord.certificateType = "DSC";

        // DSC - perform trust chain validation
        auto dscValidation = validateDscCertificate(conn, cert, issuerDn);
        valRecord.cscaFound = dscValidation.cscaFound;
        valRecord.cscaSubjectDn = dscValidation.cscaSubjectDn;
        valRecord.signatureVerified = dscValidation.signatureValid;
        valRecord.validityCheckPassed = dscValidation.notExpired;
        valRecord.isExpired = !dscValidation.notExpired;

        if (dscValidation.isValid) {
            validationStatus = "VALID";
            valRecord.validationStatus = "VALID";
            valRecord.trustChainValid = true;
            valRecord.trustChainMessage = "Trust chain verified: DSC signed by CSCA";
            validationStats.validCount++;
            validationStats.trustChainValidCount++;
            spdlog::info("DSC validation: Trust Chain VERIFIED for {} (issuer: {})",
                        countryCode, issuerDn.substr(0, 50));
        } else if (dscValidation.cscaFound) {
            validationStatus = "INVALID";
            validationMessage = dscValidation.errorMessage;
            valRecord.validationStatus = "INVALID";
            valRecord.trustChainValid = false;
            valRecord.trustChainMessage = dscValidation.errorMessage;
            valRecord.errorMessage = dscValidation.errorMessage;
            validationStats.invalidCount++;
            validationStats.trustChainInvalidCount++;
            if (!dscValidation.notExpired) validationStats.expiredCount++;
            spdlog::error("DSC validation: Trust Chain FAILED - {} for {}",
                         dscValidation.errorMessage, countryCode);
        } else {
            validationStatus = "PENDING";
            validationMessage = dscValidation.errorMessage;
            valRecord.validationStatus = "PENDING";
            valRecord.trustChainMessage = "CSCA not found in database";
            valRecord.errorCode = "CSCA_NOT_FOUND";
            valRecord.errorMessage = dscValidation.errorMessage;
            validationStats.pendingCount++;
            validationStats.cscaNotFoundCount++;
            spdlog::warn("DSC validation: CSCA not found - {} for {}",
                        dscValidation.errorMessage, countryCode);
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    valRecord.validationDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    X509_free(cert);

    // 1. Save to DB with validation status
    std::string certId = saveCertificate(conn, uploadId, certType, countryCode,
                                         subjectDn, issuerDn, serialNumber, fingerprint,
                                         notBefore, notAfter, derBytes,
                                         validationStatus, validationMessage);

    if (!certId.empty()) {
        spdlog::debug("Saved certificate to DB: type={}, country={}, fingerprint={}",
                     certType, countryCode, fingerprint.substr(0, 16));

        // 3. Save validation result
        valRecord.certificateId = certId;
        saveValidationResult(conn, valRecord);

        // 4. Save to LDAP
        if (ld) {
            std::string ldapDn = saveCertificateToLdap(ld, certType, countryCode,
                                                        subjectDn, issuerDn, serialNumber,
                                                        fingerprint, derBytes);
            if (!ldapDn.empty()) {
                updateCertificateLdapStatus(conn, certId, ldapDn);
                ldapStoredCount++;
                spdlog::debug("Saved certificate to LDAP: {}", ldapDn);
            }
        }
    }

    return !certId.empty();
}

/**
 * @brief Parse and save CRL from LDIF entry (DB + LDAP)
 */
bool parseCrlEntry(PGconn* conn, LDAP* ld, const std::string& uploadId,
                   const LdifEntry& entry, int& crlCount, int& ldapCrlStoredCount) {
    std::string base64Value = entry.getFirstAttribute("certificateRevocationList;binary");
    if (base64Value.empty()) return false;

    std::vector<uint8_t> derBytes = base64Decode(base64Value);
    if (derBytes.empty()) return false;

    const uint8_t* data = derBytes.data();
    X509_CRL* crl = d2i_X509_CRL(nullptr, &data, static_cast<long>(derBytes.size()));
    if (!crl) {
        spdlog::warn("Failed to parse CRL from entry: {}", entry.dn);
        return false;
    }

    std::string issuerDn = x509NameToString(X509_CRL_get_issuer(crl));
    std::string thisUpdate = asn1TimeToIso8601(X509_CRL_get0_lastUpdate(crl));
    std::string nextUpdate;
    if (X509_CRL_get0_nextUpdate(crl)) {
        nextUpdate = asn1TimeToIso8601(X509_CRL_get0_nextUpdate(crl));
    }

    std::string crlNumber;
    ASN1_INTEGER* crlNumAsn1 = static_cast<ASN1_INTEGER*>(
        X509_CRL_get_ext_d2i(crl, NID_crl_number, nullptr, nullptr));
    if (crlNumAsn1) {
        crlNumber = asn1IntegerToHex(crlNumAsn1);
        ASN1_INTEGER_free(crlNumAsn1);
    }

    std::string fingerprint = computeFileHash(derBytes);
    std::string countryCode = extractCountryCode(issuerDn);

    // 1. Save to DB
    std::string crlId = saveCrl(conn, uploadId, countryCode, issuerDn,
                                thisUpdate, nextUpdate, crlNumber, fingerprint, derBytes);

    if (!crlId.empty()) {
        crlCount++;

        // Save revoked certificates to DB
        STACK_OF(X509_REVOKED)* revokedStack = X509_CRL_get_REVOKED(crl);
        if (revokedStack) {
            int revokedCount = sk_X509_REVOKED_num(revokedStack);
            for (int i = 0; i < revokedCount; i++) {
                X509_REVOKED* revoked = sk_X509_REVOKED_value(revokedStack, i);
                if (revoked) {
                    std::string serialNum = asn1IntegerToHex(X509_REVOKED_get0_serialNumber(revoked));
                    std::string revDate = asn1TimeToIso8601(X509_REVOKED_get0_revocationDate(revoked));
                    std::string reason = "unspecified";

                    ASN1_ENUMERATED* reasonEnum = static_cast<ASN1_ENUMERATED*>(
                        X509_REVOKED_get_ext_d2i(revoked, NID_crl_reason, nullptr, nullptr));
                    if (reasonEnum) {
                        long reasonCode = ASN1_ENUMERATED_get(reasonEnum);
                        switch (reasonCode) {
                            case 1: reason = "keyCompromise"; break;
                            case 2: reason = "cACompromise"; break;
                            case 3: reason = "affiliationChanged"; break;
                            case 4: reason = "superseded"; break;
                            case 5: reason = "cessationOfOperation"; break;
                            case 6: reason = "certificateHold"; break;
                        }
                        ASN1_ENUMERATED_free(reasonEnum);
                    }

                    saveRevokedCertificate(conn, crlId, serialNum, revDate, reason);
                }
            }
            spdlog::debug("Saved CRL to DB with {} revoked certificates, issuer={}",
                         revokedCount, issuerDn.substr(0, 50));
        }

        // 2. Save to LDAP
        if (ld) {
            std::string ldapDn = saveCrlToLdap(ld, countryCode, issuerDn, fingerprint, derBytes);
            if (!ldapDn.empty()) {
                updateCrlLdapStatus(conn, crlId, ldapDn);
                ldapCrlStoredCount++;
                spdlog::debug("Saved CRL to LDAP: {}", ldapDn);
            }
        }
    }

    X509_CRL_free(crl);
    return !crlId.empty();
}

/**
 * @brief Extract country code from LDIF entry DN
 * Example: "cn=...,o=ml,c=FR,dc=data,..." -> "FR"
 * Example: "cn=...,o=ml,C=CA,dc=data,..." -> "CA" (case-insensitive)
 */
std::string extractCountryCodeFromDn(const std::string& dn) {
    // Look for ",c=" or ",C=" pattern in DN (case-insensitive)
    std::regex countryPattern(",([cC])=([A-Za-z]{2,3}),", std::regex::icase);
    std::smatch match;
    if (std::regex_search(dn, match, countryPattern)) {
        std::string country = match[2].str();
        std::transform(country.begin(), country.end(), country.begin(), ::toupper);
        return country;
    }
    return "XX";  // Default country code if not found
}

/**
 * @brief Parse and save Master List from LDIF entry (DB + LDAP)
 * This handles entries with pkdMasterListContent attribute
 */
bool parseMasterListEntry(PGconn* conn, LDAP* ld, const std::string& uploadId,
                          const LdifEntry& entry, int& mlCount, int& ldapMlStoredCount) {
    // Check for pkdMasterListContent;binary (LDIF parser adds ;binary suffix for base64 values)
    std::string base64Value = entry.getFirstAttribute("pkdMasterListContent;binary");
    if (base64Value.empty()) {
        // Fallback: try without ;binary suffix
        base64Value = entry.getFirstAttribute("pkdMasterListContent");
    }
    if (base64Value.empty()) return false;

    std::vector<uint8_t> mlBytes = base64Decode(base64Value);
    if (mlBytes.empty()) return false;

    spdlog::info("Parsing Master List entry: dn={}, size={} bytes", entry.dn, mlBytes.size());

    // Extract country code from DN
    std::string countryCode = extractCountryCodeFromDn(entry.dn);

    // Calculate fingerprint
    std::string fingerprint = computeFileHash(mlBytes);

    // Try to extract signer DN from the CMS structure
    std::string signerDn;
    int cscaCount = 0;

    // Parse CMS to get certificate count and signer info
    BIO* bio = BIO_new_mem_buf(mlBytes.data(), static_cast<int>(mlBytes.size()));
    if (bio) {
        CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
        if (cms) {
            // Get certificates from CMS
            STACK_OF(X509)* certs = CMS_get1_certs(cms);
            if (certs) {
                cscaCount = sk_X509_num(certs);

                // Get first certificate's subject DN as signer reference
                if (cscaCount > 0) {
                    X509* firstCert = sk_X509_value(certs, 0);
                    if (firstCert) {
                        char subjectBuf[512];
                        X509_NAME_oneline(X509_get_subject_name(firstCert), subjectBuf, sizeof(subjectBuf));
                        signerDn = subjectBuf;
                    }
                }
                sk_X509_pop_free(certs, X509_free);
            }
            CMS_ContentInfo_free(cms);
        } else {
            // Fallback: Try PKCS7
            BIO_reset(bio);
            PKCS7* p7 = d2i_PKCS7_bio(bio, nullptr);
            if (p7) {
                STACK_OF(X509)* certs = nullptr;
                if (PKCS7_type_is_signed(p7)) {
                    certs = p7->d.sign->cert;
                }
                if (certs) {
                    cscaCount = sk_X509_num(certs);
                    if (cscaCount > 0) {
                        X509* firstCert = sk_X509_value(certs, 0);
                        if (firstCert) {
                            char subjectBuf[512];
                            X509_NAME_oneline(X509_get_subject_name(firstCert), subjectBuf, sizeof(subjectBuf));
                            signerDn = subjectBuf;
                        }
                    }
                }
                PKCS7_free(p7);
            }
        }
        BIO_free(bio);
    }

    // Use cn from entry as fallback signer DN
    if (signerDn.empty()) {
        signerDn = entry.getFirstAttribute("cn");
        if (signerDn.empty()) {
            signerDn = "Unknown";
        }
    }

    spdlog::info("Master List parsed: country={}, cscaCount={}, fingerprint={}",
                 countryCode, cscaCount, fingerprint.substr(0, 16));

    // 1. Save to DB
    std::string mlId = saveMasterList(conn, uploadId, countryCode, signerDn, fingerprint, cscaCount, mlBytes);

    if (!mlId.empty()) {
        mlCount++;
        spdlog::info("Saved Master List to DB: id={}, country={}", mlId, countryCode);

        // 2. Save to LDAP (o=ml node)
        if (ld) {
            std::string ldapDn = saveMasterListToLdap(ld, countryCode, signerDn, fingerprint, mlBytes);
            if (!ldapDn.empty()) {
                updateMasterListLdapStatus(conn, mlId, ldapDn);
                ldapMlStoredCount++;
                spdlog::info("Saved Master List to LDAP: {}", ldapDn);
            }
        }
    }

    return !mlId.empty();
}

/**
 * @brief Update uploaded_file with parsing statistics
 */
void updateUploadStatistics(PGconn* conn, const std::string& uploadId,
                           const std::string& status, int cscaCount, int dscCount,
                           int dscNcCount, int crlCount, int totalEntries, int processedEntries,
                           const std::string& errorMessage) {
    std::string query = "UPDATE uploaded_file SET "
                       "status = '" + status + "', "
                       "csca_count = " + std::to_string(cscaCount) + ", "
                       "dsc_count = " + std::to_string(dscCount) + ", "
                       "dsc_nc_count = " + std::to_string(dscNcCount) + ", "
                       "crl_count = " + std::to_string(crlCount) + ", "
                       "total_entries = " + std::to_string(totalEntries) + ", "
                       "processed_entries = " + std::to_string(processedEntries) + ", "
                       "error_message = " + escapeSqlString(conn, errorMessage) + ", "
                       "completed_timestamp = NOW() "
                       "WHERE id = '" + uploadId + "'";

    PGresult* res = PQexec(conn, query.c_str());
    PQclear(res);
}

namespace {  // Resume anonymous namespace

/**
 * @brief Process LDIF file asynchronously with full parsing (DB + LDAP)
 */
void processLdifFileAsync(const std::string& uploadId, const std::vector<uint8_t>& content) {
    std::thread([uploadId, content]() {
        spdlog::info("Starting async LDIF processing for upload: {}", uploadId);

        // Connect to PostgreSQL
        std::string conninfo = "host=" + appConfig.dbHost +
                              " port=" + std::to_string(appConfig.dbPort) +
                              " dbname=" + appConfig.dbName +
                              " user=" + appConfig.dbUser +
                              " password=" + appConfig.dbPassword;

        PGconn* conn = PQconnectdb(conninfo.c_str());
        if (PQstatus(conn) != CONNECTION_OK) {
            spdlog::error("Database connection failed for async processing: {}", PQerrorMessage(conn));
            PQfinish(conn);
            return;
        }

        // Check processing_mode
        std::string modeQuery = "SELECT processing_mode FROM uploaded_file WHERE id = '" + uploadId + "'";
        PGresult* modeRes = PQexec(conn, modeQuery.c_str());
        std::string processingMode = "AUTO";  // Default to AUTO
        if (PQresultStatus(modeRes) == PGRES_TUPLES_OK && PQntuples(modeRes) > 0) {
            processingMode = PQgetvalue(modeRes, 0, 0);
        }
        PQclear(modeRes);

        spdlog::info("Processing mode for upload {}: {}", uploadId, processingMode);

        // Connect to LDAP only if AUTO mode (for MANUAL, LDAP connection happens during triggerLdapUpload)
        LDAP* ld = nullptr;
        if (processingMode == "AUTO") {
            ld = getLdapWriteConnection();
            if (!ld) {
                spdlog::warn("LDAP write connection failed - will only save to DB");
            } else {
                spdlog::info("LDAP write connection established for upload {}", uploadId);
            }
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

            // Send parsing completed progress
            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::PARSING_COMPLETED,
                    totalEntries, totalEntries, "LDIF 파싱 완료: " + std::to_string(totalEntries) + "개 엔트리"));

            // Use Strategy Pattern to handle AUTO vs MANUAL modes
            auto strategy = ProcessingStrategyFactory::create(processingMode);
            strategy->processLdifEntries(uploadId, entries, conn, ld);

            // For MANUAL mode, stop here (strategy already saved to temp file)
            if (processingMode == "MANUAL") {
                PQfinish(conn);
                return;
            }

            // For AUTO mode, strategy has already processed everything
            // No additional work needed here

            /* OLD CODE - Replaced by Strategy Pattern
            if (processingMode == "MANUAL_OLD") {
                spdlog::info("MANUAL mode OLD: Saving parsed entries to temp file");

                // Serialize LDIF entries to JSON
                Json::Value jsonEntries(Json::arrayValue);
                for (const auto& entry : entries) {
                    Json::Value jsonEntry;
                    jsonEntry["dn"] = entry.dn;

                    // Serialize attributes
                    Json::Value jsonAttrs(Json::objectValue);
                    for (const auto& attr : entry.attributes) {
                        Json::Value jsonValues(Json::arrayValue);
                        for (const auto& val : attr.second) {
                            jsonValues.append(val);
                        }
                        jsonAttrs[attr.first] = jsonValues;
                    }
                    jsonEntry["attributes"] = jsonAttrs;
                    jsonEntries.append(jsonEntry);
                }

                // Save to temp file
                std::string tempDir = "/app/temp";
                std::string tempFile = tempDir + "/" + uploadId + "_ldif.json";

                // Create temp directory if not exists
                std::filesystem::create_directories(tempDir);

                std::ofstream outFile(tempFile);
                if (outFile.is_open()) {
                    Json::StreamWriterBuilder writer;
                    writer["indentation"] = "";  // Compact JSON
                    std::unique_ptr<Json::StreamWriter> jsonWriter(writer.newStreamWriter());
                    jsonWriter->write(jsonEntries, &outFile);
                    outFile.close();
                    spdlog::info("MANUAL mode: Saved {} entries to {}", totalEntries, tempFile);
                } else {
                    spdlog::error("MANUAL mode: Failed to save temp file: {}", tempFile);
                }

                // Update status to show parsing is done
                std::string updateQuery = "UPDATE uploaded_file SET status = 'PENDING', "
                                         "total_entries = " + std::to_string(totalEntries) + " "
                                         "WHERE id = '" + uploadId + "'";
                PGresult* updateRes = PQexec(conn, updateQuery.c_str());
                PQclear(updateRes);

                PQfinish(conn);
                return;  // Stop here for MANUAL mode
            }

            // AUTO mode: Continue with validation and DB save
            // Send validation started progress
            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::VALIDATION_IN_PROGRESS,
                    0, totalEntries, "인증서 검증 및 DB 저장 중..."));

            int cscaCount = 0;
            int dscCount = 0;
            int dscNcCount = 0;
            int crlCount = 0;
            int mlCount = 0;  // Master List count
            int processedEntries = 0;
            int ldapCertStoredCount = 0;
            int ldapCrlStoredCount = 0;
            int ldapMlStoredCount = 0;  // LDAP Master List count
            ValidationStats validationStats;  // Track validation statistics

            // Process each entry
            for (const auto& entry : entries) {
                try {
                    // Check for userCertificate;binary
                    if (entry.hasAttribute("userCertificate;binary")) {
                        parseCertificateEntry(conn, ld, uploadId, entry, "userCertificate;binary",
                                             cscaCount, dscCount, dscNcCount, ldapCertStoredCount, validationStats);
                    }
                    // Check for cACertificate;binary
                    else if (entry.hasAttribute("cACertificate;binary")) {
                        parseCertificateEntry(conn, ld, uploadId, entry, "cACertificate;binary",
                                             cscaCount, dscCount, dscNcCount, ldapCertStoredCount, validationStats);
                    }

                    // Check for CRL
                    if (entry.hasAttribute("certificateRevocationList;binary")) {
                        parseCrlEntry(conn, ld, uploadId, entry, crlCount, ldapCrlStoredCount);
                    }

                    // Check for Master List (pkdMasterListContent;binary - LDIF parser adds ;binary for base64 values)
                    if (entry.hasAttribute("pkdMasterListContent;binary") || entry.hasAttribute("pkdMasterListContent")) {
                        parseMasterListEntry(conn, ld, uploadId, entry, mlCount, ldapMlStoredCount);
                    }

                } catch (const std::exception& e) {
                    spdlog::warn("Error processing entry {}: {}", entry.dn, e.what());
                }

                processedEntries++;

                // Send progress update every 50 entries
                if (processedEntries % 50 == 0 || processedEntries == totalEntries) {
                    std::string progressMsg = "처리 중: " + std::to_string(cscaCount + dscCount) + "개 인증서, " +
                                             std::to_string(crlCount) + "개 CRL";
                    if (mlCount > 0) {
                        progressMsg += ", " + std::to_string(mlCount) + "개 ML";
                    }
                    ProgressManager::getInstance().sendProgress(
                        ProcessingProgress::create(uploadId, ProcessingStage::DB_SAVING_IN_PROGRESS,
                            processedEntries, totalEntries, progressMsg));

                    spdlog::info("Processing progress: {}/{} entries, {} certs ({} LDAP), {} CRLs ({} LDAP), {} MLs ({} LDAP)",
                                processedEntries, totalEntries,
                                cscaCount + dscCount, ldapCertStoredCount,
                                crlCount, ldapCrlStoredCount,
                                mlCount, ldapMlStoredCount);
                }
            }

            // Send LDAP saving progress
            std::string ldapProgressMsg = "LDAP 저장 완료: " + std::to_string(ldapCertStoredCount) + "개 인증서, " +
                                          std::to_string(ldapCrlStoredCount) + "개 CRL";
            if (ldapMlStoredCount > 0) {
                ldapProgressMsg += ", " + std::to_string(ldapMlStoredCount) + "개 ML";
            }
            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::LDAP_SAVING_IN_PROGRESS,
                    ldapCertStoredCount + ldapCrlStoredCount + ldapMlStoredCount,
                    cscaCount + dscCount + crlCount + mlCount, ldapProgressMsg));

            // Update final statistics (ml_count will be updated separately)
            updateUploadStatistics(conn, uploadId, "COMPLETED", cscaCount, dscCount, dscNcCount, crlCount,
                                  totalEntries, processedEntries, "");

            // Update ml_count in uploaded_file
            if (mlCount > 0) {
                std::string mlUpdateQuery = "UPDATE uploaded_file SET ml_count = " + std::to_string(mlCount) +
                                           " WHERE id = '" + uploadId + "'";
                PGresult* mlRes = PQexec(conn, mlUpdateQuery.c_str());
                PQclear(mlRes);
            }

            // Update validation statistics
            updateValidationStatistics(conn, uploadId,
                                       validationStats.validCount, validationStats.invalidCount,
                                       validationStats.pendingCount, validationStats.errorCount,
                                       validationStats.trustChainValidCount, validationStats.trustChainInvalidCount,
                                       validationStats.cscaNotFoundCount, validationStats.expiredCount,
                                       validationStats.revokedCount);

            // Send completion progress with validation info
            std::string completionMsg = "처리 완료: CSCA " + std::to_string(cscaCount) +
                                       "개, DSC " + std::to_string(dscCount) +
                                       (dscNcCount > 0 ? "개, DSC_NC " + std::to_string(dscNcCount) : "") +
                                       "개, CRL " + std::to_string(crlCount) + "개";
            if (mlCount > 0) {
                completionMsg += ", ML " + std::to_string(mlCount) + "개";
            }
            completionMsg += " (검증: " + std::to_string(validationStats.validCount) + " 성공, " +
                            std::to_string(validationStats.invalidCount) + " 실패, " +
                            std::to_string(validationStats.pendingCount) + " 보류)";
            int totalItems = cscaCount + dscCount + dscNcCount + crlCount + mlCount;
            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::COMPLETED,
                    totalItems, totalItems, completionMsg));

            spdlog::info("LDIF processing completed for upload {}: {} CSCA, {} DSC, {} DSC_NC, {} CRLs, {} MLs (LDAP: {} certs, {} CRLs, {} MLs)",
                        uploadId, cscaCount, dscCount, dscNcCount, crlCount, mlCount,
                        ldapCertStoredCount, ldapCrlStoredCount, ldapMlStoredCount);
            spdlog::info("Validation stats: {} valid, {} invalid, {} pending, {} csca_not_found, {} expired",
                        validationStats.validCount, validationStats.invalidCount, validationStats.pendingCount,
                        validationStats.cscaNotFoundCount, validationStats.expiredCount);
            */ // END OLD CODE

        } catch (const std::exception& e) {
            spdlog::error("LDIF processing failed for upload {}: {}", uploadId, e.what());
            updateUploadStatistics(conn, uploadId, "FAILED", 0, 0, 0, 0, 0, 0, e.what());
        }

        // Cleanup connections
        if (ld) {
            ldap_unbind_ext_s(ld, nullptr, nullptr);
        }
        PQfinish(conn);
    }).detach();
}

/**
 * @brief Core Master List processing logic (called by Strategy Pattern)
 * @param uploadId Upload record UUID
 * @param content Raw Master List file content
 * @param conn PostgreSQL connection
 * @param ld LDAP connection (can be nullptr for MANUAL mode Stage 2)
 */
void processMasterListContentCore(const std::string& uploadId, const std::vector<uint8_t>& content,
                                   PGconn* conn, LDAP* ld) {
    spdlog::info("Processing Master List core for upload {}: {} bytes, LDAP={}",
                 uploadId, content.size(), (ld ? "yes" : "no"));

    int cscaCount = 0;
    int dscCount = 0;
    int ldapStoredCount = 0;
    int skippedDuplicates = 0;
    int totalCerts = 0;

    try {
        // Send initial progress
        ProgressManager::getInstance().sendProgress(
            ProcessingProgress::create(uploadId, ProcessingStage::PARSING_STARTED, 0, 0, "CMS 파싱 시작"));

        // Validate CMS format
        if (content.empty() || content[0] != 0x30) {
            spdlog::error("Invalid Master List: not a valid CMS structure");
            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::FAILED, 0, 0, "Invalid CMS format"));
            updateUploadStatistics(conn, uploadId, "FAILED", 0, 0, 0, 0, 0, 0, "Invalid CMS format");
            return;
        }

        // Parse as CMS SignedData
        BIO* bio = BIO_new_mem_buf(content.data(), static_cast<int>(content.size()));
        CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
        BIO_free(bio);

        // Verify CMS signature with UN_CSCA trust anchor
        if (cms) {
            X509* trustAnchor = loadTrustAnchor();
            if (trustAnchor) {
                bool signatureValid = verifyCmsSignature(cms, trustAnchor);
                X509_free(trustAnchor);
                if (!signatureValid) {
                    spdlog::warn("Master List CMS signature verification failed - continuing with parsing");
                }
            }
        }

        // Extract certificates from CMS or PKCS7
        if (cms) {
            // CMS parsing succeeded
            spdlog::info("CMS SignedData parsed successfully, extracting certificates...");
            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::PARSING_IN_PROGRESS, 0, 0, "인증서 추출 중"));

            ASN1_OCTET_STRING** contentPtr = CMS_get0_content(cms);
            if (contentPtr && *contentPtr) {
                const unsigned char* contentData = ASN1_STRING_get0_data(*contentPtr);
                int contentLen = ASN1_STRING_length(*contentPtr);

                // Parse MasterList ASN.1 structure
                const unsigned char* p = contentData;
                int tag, xclass;
                long seqLen;
                int ret = ASN1_get_object(&p, &seqLen, &tag, &xclass, contentLen);

                if (ret != 0x80 && tag == V_ASN1_SEQUENCE) {
                    const unsigned char* seqEnd = p + seqLen;
                    long elemLen;
                    ret = ASN1_get_object(&p, &elemLen, &tag, &xclass, seqEnd - p);

                    // Find certList (SET)
                    const unsigned char* certSetStart = nullptr;
                    long certSetLen = 0;

                    if (tag == V_ASN1_INTEGER) {
                        // Skip version
                        p += elemLen;
                        if (p < seqEnd) {
                            ret = ASN1_get_object(&p, &elemLen, &tag, &xclass, seqEnd - p);
                            if (tag == V_ASN1_SET) {
                                certSetStart = p;
                                certSetLen = elemLen;
                            }
                        }
                    } else if (tag == V_ASN1_SET) {
                        certSetStart = p;
                        certSetLen = elemLen;
                    }

                    // Parse certificates
                    if (certSetStart && certSetLen > 0) {
                        const unsigned char* certPtr = certSetStart;
                        const unsigned char* certSetEnd = certSetStart + certSetLen;

                        while (certPtr < certSetEnd) {
                            X509* cert = d2i_X509(nullptr, &certPtr, certSetEnd - certPtr);
                            if (cert) {
                                totalCerts++;

                                // Extract certificate data
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
                                    std::string certType = "CSCA";

                                    // Validate certificate
                                    std::string validationStatus = "VALID";
                                    if (subjectDn == issuerDn) {
                                        auto cscaValidation = validateCscaCertificate(cert);
                                        validationStatus = cscaValidation.isValid ? "VALID" : "INVALID";
                                    }

                                    // Save to DB
                                    std::string certId = saveCertificate(conn, uploadId, certType, countryCode,
                                                                         subjectDn, issuerDn, serialNumber, fingerprint,
                                                                         notBefore, notAfter, derBytes);

                                    if (!certId.empty()) {
                                        cscaCount++;

                                        // Save to LDAP if connection available
                                        if (ld) {
                                            std::string ldapDn = saveCertificateToLdap(ld, certType, countryCode,
                                                                                        subjectDn, issuerDn, serialNumber,
                                                                                        fingerprint, derBytes);
                                            if (!ldapDn.empty()) {
                                                updateCertificateLdapStatus(conn, certId, ldapDn);
                                                ldapStoredCount++;
                                            }
                                        }
                                    }

                                    // Progress update
                                    if (totalCerts % 50 == 0) {
                                        ProgressManager::getInstance().sendProgress(
                                            ProcessingProgress::create(uploadId, ProcessingStage::DB_SAVING_IN_PROGRESS,
                                                totalCerts, totalCerts, "DB 저장 중: " + std::to_string(cscaCount) + " CSCA"));
                                    }
                                }
                                X509_free(cert);
                            }
                        }
                    }
                }
            }
            CMS_ContentInfo_free(cms);

        } else {
            // Fallback: PKCS7
            spdlog::debug("CMS parsing failed, trying PKCS7 fallback...");
            const uint8_t* p = content.data();
            PKCS7* p7 = d2i_PKCS7(nullptr, &p, static_cast<long>(content.size()));

            if (p7 && PKCS7_type_is_signed(p7)) {
                STACK_OF(X509)* certs = p7->d.sign->cert;
                if (certs) {
                    int numCerts = sk_X509_num(certs);
                    spdlog::info("Found {} certificates in Master List (PKCS7)", numCerts);

                    for (int i = 0; i < numCerts; i++) {
                        X509* cert = sk_X509_value(certs, i);
                        if (!cert) continue;

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

                            std::string certId = saveCertificate(conn, uploadId, "CSCA", countryCode,
                                                                 subjectDn, issuerDn, serialNumber, fingerprint,
                                                                 notBefore, notAfter, derBytes);

                            if (!certId.empty()) {
                                cscaCount++;
                                if (ld) {
                                    std::string ldapDn = saveCertificateToLdap(ld, "CSCA", countryCode,
                                                                                subjectDn, issuerDn, serialNumber,
                                                                                fingerprint, derBytes);
                                    if (!ldapDn.empty()) {
                                        updateCertificateLdapStatus(conn, certId, ldapDn);
                                        ldapStoredCount++;
                                    }
                                }
                            }
                        }
                    }
                }
                PKCS7_free(p7);
            } else {
                spdlog::error("Failed to parse Master List: neither CMS nor PKCS7");
                updateUploadStatistics(conn, uploadId, "FAILED", 0, 0, 0, 0, 0, 0, "CMS/PKCS7 parsing failed");
                return;
            }
        }

        // Update statistics
        ProgressManager::getInstance().sendProgress(
            ProcessingProgress::create(uploadId, ProcessingStage::DB_SAVING_IN_PROGRESS,
                100, 100, "DB 저장 완료: " + std::to_string(cscaCount) + " CSCA"));

        spdlog::info("Extracted {} certificates from Master List", totalCerts);
        updateUploadStatistics(conn, uploadId, "COMPLETED", cscaCount, dscCount, 0, 0, totalCerts, totalCerts, "");

        spdlog::info("Master List processing completed: {} CSCA, LDAP: {}", cscaCount, ldapStoredCount);

    } catch (const std::exception& e) {
        spdlog::error("Master List processing failed: {}", e.what());
        ProgressManager::getInstance().sendProgress(
            ProcessingProgress::create(uploadId, ProcessingStage::FAILED, 0, 0, "처리 실패", e.what()));
        updateUploadStatistics(conn, uploadId, "FAILED", 0, 0, 0, 0, 0, 0, e.what());
    }
}

/**
 * @brief Parse Master List (CMS SignedData) and extract CSCA certificates (DB + LDAP)
 * Master List contains CSCA certificates in CMS SignedData format
 */
void processMasterListFileAsync(const std::string& uploadId, const std::vector<uint8_t>& content) {
    std::thread([uploadId, content]() {
        spdlog::info("Starting async Master List processing for upload: {}", uploadId);

        // Connect to PostgreSQL
        std::string conninfo = "host=" + appConfig.dbHost +
                              " port=" + std::to_string(appConfig.dbPort) +
                              " dbname=" + appConfig.dbName +
                              " user=" + appConfig.dbUser +
                              " password=" + appConfig.dbPassword;

        PGconn* conn = PQconnectdb(conninfo.c_str());
        if (PQstatus(conn) != CONNECTION_OK) {
            spdlog::error("Database connection failed for async processing: {}", PQerrorMessage(conn));
            PQfinish(conn);
            return;
        }

        // Check processing_mode
        std::string modeQuery = "SELECT processing_mode FROM uploaded_file WHERE id = '" + uploadId + "'";
        PGresult* modeRes = PQexec(conn, modeQuery.c_str());
        std::string processingMode = "AUTO";  // Default to AUTO
        if (PQresultStatus(modeRes) == PGRES_TUPLES_OK && PQntuples(modeRes) > 0) {
            processingMode = PQgetvalue(modeRes, 0, 0);
        }
        PQclear(modeRes);

        spdlog::info("Processing mode for Master List upload {}: {}", uploadId, processingMode);

        // Connect to LDAP only if AUTO mode
        LDAP* ld = nullptr;
        if (processingMode == "AUTO") {
            ld = getLdapWriteConnection();
            if (!ld) {
                spdlog::warn("LDAP write connection failed - will only save to DB");
            } else {
                spdlog::info("LDAP write connection established for Master List upload {}", uploadId);
            }
        }

        try {
            int cscaCount = 0;
            int dscCount = 0;
            int ldapStoredCount = 0;
            int skippedDuplicates = 0;
            int totalCerts = 0;

            // Send initial progress
            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::PARSING_STARTED, 0, 0, "CMS 파싱 시작"));

            // Validate CMS format: first byte must be 0x30 (SEQUENCE tag)
            if (content.empty() || content[0] != 0x30) {
                spdlog::error("Invalid Master List: not a valid CMS structure (missing SEQUENCE tag)");
                ProgressManager::getInstance().sendProgress(
                    ProcessingProgress::create(uploadId, ProcessingStage::FAILED, 0, 0, "Invalid CMS format", "CMS 형식 오류"));
                updateUploadStatistics(conn, uploadId, "FAILED", 0, 0, 0, 0, 0, 0, "Invalid CMS format");
                if (ld) ldap_unbind_ext_s(ld, nullptr, nullptr);
                PQfinish(conn);
                return;
            }

            // Parse as CMS SignedData using OpenSSL CMS API
            BIO* bio = BIO_new_mem_buf(content.data(), static_cast<int>(content.size()));
            CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
            BIO_free(bio);

            // Verify CMS signature with UN_CSCA trust anchor
            if (cms) {
                X509* trustAnchor = loadTrustAnchor();
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
                        spdlog::info("Found {} certificates in Master List (PKCS7 fallback)", numCerts);

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

                            // Master List contains ONLY CSCA certificates (per ICAO Doc 9303)
                            // Including both self-signed and cross-signed/link CSCAs
                            std::string certType = "CSCA";

                            std::string certId = saveCertificate(conn, uploadId, certType, countryCode,
                                                                 subjectDn, issuerDn, serialNumber, fingerprint,
                                                                 notBefore, notAfter, derBytes);

                            if (!certId.empty()) {
                                cscaCount++;

                                if (ld) {
                                    std::string ldapDn = saveCertificateToLdap(ld, certType, countryCode,
                                                                                subjectDn, issuerDn, serialNumber,
                                                                                fingerprint, derBytes);
                                    if (!ldapDn.empty()) {
                                        updateCertificateLdapStatus(conn, certId, ldapDn);
                                        ldapStoredCount++;
                                    }
                                }
                            }
                        }
                    }
                    PKCS7_free(p7);
                } else {
                    spdlog::error("Failed to parse Master List: neither CMS nor PKCS7 parsing succeeded");
                    spdlog::error("OpenSSL error: {}", ERR_error_string(ERR_get_error(), nullptr));
                    updateUploadStatistics(conn, uploadId, "FAILED", 0, 0, 0, 0, 0, 0, "CMS/PKCS7 parsing failed");
                    if (ld) ldap_unbind_ext_s(ld, nullptr, nullptr);
                    PQfinish(conn);
                    return;
                }
            } else {
                // CMS parsing succeeded - extract certificates from encapsulated content
                // ICAO Master List structure: MasterList ::= SEQUENCE { version INTEGER OPTIONAL, certList SET OF Certificate }
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

                                        // Master List contains ONLY CSCA certificates (per ICAO Doc 9303)
                                        // Including both self-signed and cross-signed/link CSCAs
                                        std::string certType = "CSCA";
                                        std::string validationStatus = "VALID";
                                        std::string validationMessage = "";

                                        if (subjectDn == issuerDn) {
                                            // Self-signed CSCA - verify self-signature
                                            auto cscaValidation = validateCscaCertificate(cert);
                                            if (cscaValidation.isValid) {
                                                validationStatus = "VALID";
                                                spdlog::debug("CSCA self-signature verified: {}", subjectDn.substr(0, 50));
                                            } else if (cscaValidation.signatureValid) {
                                                // Self-signed but missing CA flag or key usage
                                                validationStatus = "WARNING";
                                                validationMessage = cscaValidation.errorMessage;
                                                spdlog::warn("CSCA validation warning: {} - {}", subjectDn.substr(0, 50), cscaValidation.errorMessage);
                                            } else {
                                                // Signature invalid
                                                validationStatus = "INVALID";
                                                validationMessage = cscaValidation.errorMessage;
                                                spdlog::error("CSCA self-signature FAILED: {} - {}", subjectDn.substr(0, 50), cscaValidation.errorMessage);
                                            }
                                        } else {
                                            // Cross-signed/Link CSCA - mark as valid (signed by another CSCA)
                                            spdlog::debug("Cross-signed CSCA: subject={}, issuer={}",
                                                         subjectDn.substr(0, 50), issuerDn.substr(0, 50));
                                        }
                                        totalCerts++;

                                        // Check for duplicate before saving
                                        if (certificateExistsByFingerprint(conn, fingerprint)) {
                                            skippedDuplicates++;
                                            spdlog::debug("Skipping duplicate certificate: fingerprint={}", fingerprint.substr(0, 16));
                                            X509_free(cert);
                                            continue;
                                        }

                                        // Send progress update
                                        if (totalCerts % 50 == 0) {
                                            ProgressManager::getInstance().sendProgress(
                                                ProcessingProgress::create(uploadId, ProcessingStage::DB_SAVING_IN_PROGRESS,
                                                    cscaCount + dscCount, totalCerts, "인증서 저장 중: " + std::to_string(cscaCount + dscCount) + "개"));
                                        }

                                        // 1. Save to DB with validation status
                                        std::string certId = saveCertificate(conn, uploadId, certType, countryCode,
                                                                             subjectDn, issuerDn, serialNumber, fingerprint,
                                                                             notBefore, notAfter, derBytes,
                                                                             validationStatus, validationMessage);

                                        if (!certId.empty()) {
                                            // All Master List certificates are CSCA
                                            cscaCount++;

                                            spdlog::debug("Saved CSCA from Master List to DB: country={}, fingerprint={}",
                                                         countryCode, fingerprint.substr(0, 16));

                                            // 2. Save to LDAP
                                            if (ld) {
                                                std::string ldapDn = saveCertificateToLdap(ld, certType, countryCode,
                                                                                            subjectDn, issuerDn, serialNumber,
                                                                                            fingerprint, derBytes);
                                                if (!ldapDn.empty()) {
                                                    updateCertificateLdapStatus(conn, certId, ldapDn);
                                                    ldapStoredCount++;
                                                    spdlog::debug("Saved {} from Master List to LDAP: {}", certType, ldapDn);
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

                            // Master List contains ONLY CSCA certificates (per ICAO Doc 9303)
                            // Including both self-signed and cross-signed/link CSCAs
                            std::string certType = "CSCA";

                            std::string certId = saveCertificate(conn, uploadId, certType, countryCode,
                                                                 subjectDn, issuerDn, serialNumber, fingerprint,
                                                                 notBefore, notAfter, derBytes);

                            if (!certId.empty()) {
                                cscaCount++;

                                if (ld) {
                                    std::string ldapDn = saveCertificateToLdap(ld, certType, countryCode,
                                                                                subjectDn, issuerDn, serialNumber,
                                                                                fingerprint, derBytes);
                                    if (!ldapDn.empty()) {
                                        updateCertificateLdapStatus(conn, certId, ldapDn);
                                        ldapStoredCount++;
                                    }
                                }
                            }
                        }

                        sk_X509_pop_free(certs, X509_free);
                    }
                }

                CMS_ContentInfo_free(cms);
            }

            // Update statistics (Master List contains only CSCA, no DSC or DSC_NC)
            // Note: ICAO Master List (.ml) extracts individual CSCA certificates, so ml_count = 0
            // Only Country Master Lists from LDIF (pkdMasterListContent) are counted as ML
            updateUploadStatistics(conn, uploadId, "COMPLETED", cscaCount, dscCount, 0, 0, 1, 1, "");

            // Send completion progress
            std::string completionMsg = "처리 완료: CSCA " + std::to_string(cscaCount) +
                                       "개, DSC " + std::to_string(dscCount) +
                                       "개 (중복 " + std::to_string(skippedDuplicates) + "개 건너뜀)";
            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::COMPLETED,
                    cscaCount + dscCount, totalCerts, completionMsg));

            spdlog::info("Master List processing completed for upload {}: {} CSCA, {} DSC certificates (LDAP: {}, duplicates skipped: {})",
                        uploadId, cscaCount, dscCount, ldapStoredCount, skippedDuplicates);

        } catch (const std::exception& e) {
            spdlog::error("Master List processing failed for upload {}: {}", uploadId, e.what());
            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::FAILED, 0, 0, "처리 실패", e.what()));
            updateUploadStatistics(conn, uploadId, "FAILED", 0, 0, 0, 0, 0, 0, e.what());
        }

        // Cleanup connections
        if (ld) {
            ldap_unbind_ext_s(ld, nullptr, nullptr);
        }
        PQfinish(conn);
    }).detach();
}

/**
 * @brief Check LDAP connectivity
 */
Json::Value checkLdap() {
    Json::Value result;
    result["name"] = "ldap";

    try {
        // Simple LDAP connection test using system ldapsearch
        auto start = std::chrono::steady_clock::now();

        std::string cmd = "ldapsearch -x -H ldap://" + appConfig.ldapHost + ":" +
                         std::to_string(appConfig.ldapPort) +
                         " -b \"\" -s base \"(objectclass=*)\" namingContexts 2>/dev/null | grep -q namingContexts";

        int ret = system(cmd.c_str());
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        if (ret == 0) {
            result["status"] = "UP";
            result["responseTimeMs"] = static_cast<int>(duration.count());
            result["host"] = appConfig.ldapHost;
            result["port"] = appConfig.ldapPort;
        } else {
            result["status"] = "DOWN";
            result["error"] = "LDAP connection failed";
        }
    } catch (const std::exception& e) {
        result["status"] = "DOWN";
        result["error"] = e.what();
    }

    return result;
}

void registerRoutes() {
    auto& app = drogon::app();

    // Manual mode: Trigger parse endpoint
    app.registerHandler(
        "/api/upload/{uploadId}/parse",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& uploadId) {
            spdlog::info("POST /api/upload/{}/parse - Trigger parsing", uploadId);

            // Connect to database to check if upload exists
            std::string conninfo = "host=" + appConfig.dbHost +
                                  " port=" + std::to_string(appConfig.dbPort) +
                                  " dbname=" + appConfig.dbName +
                                  " user=" + appConfig.dbUser +
                                  " password=" + appConfig.dbPassword;

            PGconn* conn = PQconnectdb(conninfo.c_str());
            if (PQstatus(conn) != CONNECTION_OK) {
                Json::Value error;
                error["success"] = false;
                error["message"] = "Database connection failed";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
                PQfinish(conn);
                return;
            }

            // Check if upload exists and get file path
            std::string query = "SELECT id, file_path, file_format FROM uploaded_file WHERE id = '" + uploadId + "'";
            PGresult* res = PQexec(conn, query.c_str());

            if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
                Json::Value error;
                error["success"] = false;
                error["message"] = "Upload not found";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k404NotFound);
                callback(resp);
                PQclear(res);
                PQfinish(conn);
                return;
            }

            // Get file path and format
            char* filePath = PQgetvalue(res, 0, 1);
            char* fileFormat = PQgetvalue(res, 0, 2);

            if (!filePath || strlen(filePath) == 0) {
                Json::Value error;
                error["success"] = false;
                error["message"] = "File path not found. File may not have been saved.";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k404NotFound);
                callback(resp);
                PQclear(res);
                PQfinish(conn);
                return;
            }

            std::string filePathStr(filePath);
            std::string fileFormatStr(fileFormat);
            PQclear(res);
            PQfinish(conn);

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
                // Use Strategy Pattern for Master List (same as LDIF)
                std::thread([uploadId, contentBytes]() {
                    spdlog::info("Starting async Master List processing via Strategy for upload: {}", uploadId);

                    std::string conninfo = "host=" + appConfig.dbHost +
                                          " port=" + std::to_string(appConfig.dbPort) +
                                          " dbname=" + appConfig.dbName +
                                          " user=" + appConfig.dbUser +
                                          " password=" + appConfig.dbPassword;

                    PGconn* conn = PQconnectdb(conninfo.c_str());
                    if (PQstatus(conn) != CONNECTION_OK) {
                        spdlog::error("Database connection failed for async processing: {}", PQerrorMessage(conn));
                        PQfinish(conn);
                        return;
                    }

                    // Get processing mode
                    std::string modeQuery = "SELECT processing_mode FROM uploaded_file WHERE id = '" + uploadId + "'";
                    PGresult* modeRes = PQexec(conn, modeQuery.c_str());
                    std::string processingMode = "AUTO";
                    if (PQresultStatus(modeRes) == PGRES_TUPLES_OK && PQntuples(modeRes) > 0) {
                        processingMode = PQgetvalue(modeRes, 0, 0);
                    }
                    PQclear(modeRes);

                    spdlog::info("Processing mode for Master List upload {}: {}", uploadId, processingMode);

                    // Connect to LDAP only if AUTO mode
                    LDAP* ld = nullptr;
                    if (processingMode == "AUTO") {
                        ld = getLdapWriteConnection();
                        if (!ld) {
                            spdlog::warn("LDAP write connection failed - will only save to DB");
                        }
                    }

                    try {
                        // Use Strategy Pattern
                        auto strategy = ProcessingStrategyFactory::create(processingMode);
                        strategy->processMasterListContent(uploadId, contentBytes, conn, ld);

                        // Send completion progress
                        ProgressManager::getInstance().sendProgress(
                            ProcessingProgress::create(uploadId, ProcessingStage::COMPLETED,
                                100, 100, "Master List 파싱 완료"));

                    } catch (const std::exception& e) {
                        spdlog::error("Master List processing via Strategy failed for upload {}: {}", uploadId, e.what());
                        ProgressManager::getInstance().sendProgress(
                            ProcessingProgress::create(uploadId, ProcessingStage::FAILED,
                                0, 0, "처리 실패", e.what()));
                    }

                    if (ld) ldap_unbind_ext_s(ld, nullptr, nullptr);
                    PQfinish(conn);
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
        },
        {drogon::Post}
    );

    // Manual mode: Trigger validate and DB save endpoint
    app.registerHandler(
        "/api/upload/{uploadId}/validate",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& uploadId) {
            spdlog::info("POST /api/upload/{}/validate - Trigger validation and DB save", uploadId);

            // Connect to database to check if upload exists
            std::string conninfo = "host=" + appConfig.dbHost +
                                  " port=" + std::to_string(appConfig.dbPort) +
                                  " dbname=" + appConfig.dbName +
                                  " user=" + appConfig.dbUser +
                                  " password=" + appConfig.dbPassword;

            PGconn* conn = PQconnectdb(conninfo.c_str());
            if (PQstatus(conn) != CONNECTION_OK) {
                Json::Value error;
                error["success"] = false;
                error["message"] = "Database connection failed";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
                PQfinish(conn);
                return;
            }

            // Check if upload exists
            std::string query = "SELECT id FROM uploaded_file WHERE id = '" + uploadId + "'";
            PGresult* res = PQexec(conn, query.c_str());

            if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
                Json::Value error;
                error["success"] = false;
                error["message"] = "Upload not found";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k404NotFound);
                callback(resp);
                PQclear(res);
                PQfinish(conn);
                return;
            }
            PQclear(res);
            PQfinish(conn);

            // Trigger validation and DB save in background (MANUAL mode Stage 2)
            std::thread([uploadId]() {
                spdlog::info("Starting DSC validation for upload: {}", uploadId);

                std::string conninfo = "host=" + appConfig.dbHost +
                                      " port=" + std::to_string(appConfig.dbPort) +
                                      " dbname=" + appConfig.dbName +
                                      " user=" + appConfig.dbUser +
                                      " password=" + appConfig.dbPassword;

                PGconn* conn = PQconnectdb(conninfo.c_str());
                if (PQstatus(conn) != CONNECTION_OK) {
                    spdlog::error("Database connection failed for validation");
                    PQfinish(conn);
                    return;
                }

                try {
                    // Send validation started
                    ProgressManager::getInstance().sendProgress(
                        ProcessingProgress::create(uploadId, ProcessingStage::VALIDATION_IN_PROGRESS,
                            0, 100, "인증서 검증 중..."));

                    // MANUAL mode Stage 2: Validate and save to DB
                    auto strategy = ProcessingStrategyFactory::create("MANUAL");
                    auto manualStrategy = dynamic_cast<ManualProcessingStrategy*>(strategy.get());
                    if (manualStrategy) {
                        manualStrategy->validateAndSaveToDb(uploadId, conn);
                    }

                    // Send DB save completed (Stage 2 완료)
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

                PQfinish(conn);
            }).detach();

            Json::Value result;
            result["success"] = true;
            result["message"] = "Validation processing started";
            result["uploadId"] = uploadId;

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Post}
    );

    // Cleanup failed upload endpoint
    app.registerHandler(
        "/api/upload/{uploadId}",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& uploadId) {
            spdlog::info("DELETE /api/upload/{} - Cleanup failed upload", uploadId);

            // Connect to database
            std::string conninfo = "host=" + appConfig.dbHost +
                                  " port=" + std::to_string(appConfig.dbPort) +
                                  " dbname=" + appConfig.dbName +
                                  " user=" + appConfig.dbUser +
                                  " password=" + appConfig.dbPassword;

            PGconn* conn = PQconnectdb(conninfo.c_str());
            if (PQstatus(conn) != CONNECTION_OK) {
                Json::Value error;
                error["success"] = false;
                error["message"] = "Database connection failed";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
                PQfinish(conn);
                return;
            }

            // Check if upload exists
            std::string query = "SELECT id FROM uploaded_file WHERE id = '" + uploadId + "'";
            PGresult* res = PQexec(conn, query.c_str());

            if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
                Json::Value error;
                error["success"] = false;
                error["message"] = "Upload not found";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k404NotFound);
                callback(resp);
                PQclear(res);
                PQfinish(conn);
                return;
            }
            PQclear(res);

            // Cleanup failed upload
            try {
                ManualProcessingStrategy::cleanupFailedUpload(uploadId, conn);

                Json::Value result;
                result["success"] = true;
                result["message"] = "Upload cleaned up successfully";
                result["uploadId"] = uploadId;

                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                callback(resp);
            } catch (const std::exception& e) {
                spdlog::error("Failed to cleanup upload {}: {}", uploadId, e.what());
                Json::Value error;
                error["success"] = false;
                error["message"] = std::string("Cleanup failed: ") + e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }

            PQfinish(conn);
        },
        {drogon::Delete}
    );

    // Manual mode: Trigger LDAP save endpoint
    app.registerHandler(
        "/api/upload/{uploadId}/ldap",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& uploadId) {
            spdlog::info("POST /api/upload/{}/ldap - Trigger LDAP save", uploadId);

            // Connect to database to check if upload exists
            std::string conninfo = "host=" + appConfig.dbHost +
                                  " port=" + std::to_string(appConfig.dbPort) +
                                  " dbname=" + appConfig.dbName +
                                  " user=" + appConfig.dbUser +
                                  " password=" + appConfig.dbPassword;

            PGconn* conn = PQconnectdb(conninfo.c_str());
            if (PQstatus(conn) != CONNECTION_OK) {
                Json::Value error;
                error["success"] = false;
                error["message"] = "Database connection failed";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
                PQfinish(conn);
                return;
            }

            // Check if upload exists
            std::string query = "SELECT id FROM uploaded_file WHERE id = '" + uploadId + "'";
            PGresult* res = PQexec(conn, query.c_str());

            if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
                Json::Value error;
                error["success"] = false;
                error["message"] = "Upload not found";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k404NotFound);
                callback(resp);
                PQclear(res);
                PQfinish(conn);
                return;
            }
            PQclear(res);
            PQfinish(conn);

            // Trigger LDAP save in background
            std::thread([uploadId]() {
                spdlog::info("Starting LDAP save for upload: {}", uploadId);

                std::string conninfo = "host=" + appConfig.dbHost +
                                      " port=" + std::to_string(appConfig.dbPort) +
                                      " dbname=" + appConfig.dbName +
                                      " user=" + appConfig.dbUser +
                                      " password=" + appConfig.dbPassword;

                PGconn* conn = PQconnectdb(conninfo.c_str());
                if (PQstatus(conn) != CONNECTION_OK) {
                    spdlog::error("Database connection failed for LDAP save");
                    PQfinish(conn);
                    return;
                }

                // Connect to LDAP (write connection - direct to primary master)
                LDAP* ld = getLdapWriteConnection();
                if (!ld) {
                    spdlog::error("LDAP write connection failed for upload {}", uploadId);
                    ProgressManager::getInstance().sendProgress(
                        ProcessingProgress::create(uploadId, ProcessingStage::FAILED,
                            0, 0, "LDAP 연결 실패"));
                    PQfinish(conn);
                    return;
                }

                try {
                    // Send LDAP saving started
                    ProgressManager::getInstance().sendProgress(
                        ProcessingProgress::create(uploadId, ProcessingStage::LDAP_SAVING_IN_PROGRESS,
                            0, 100, "LDAP에 저장 중..."));

                    // Get certificates that haven't been stored in LDAP yet
                    std::string certQuery = "SELECT id, certificate_binary, certificate_type, country_code, subject_dn, "
                                           "issuer_dn, serial_number, fingerprint_sha256 "
                                           "FROM certificate "
                                           "WHERE upload_id = '" + uploadId + "' "
                                           "AND (stored_in_ldap = false OR stored_in_ldap IS NULL) "
                                           "ORDER BY id";
                    PGresult* certRes = PQexec(conn, certQuery.c_str());

                    int totalCerts = 0;
                    int storedCerts = 0;

                    if (PQresultStatus(certRes) == PGRES_TUPLES_OK) {
                        totalCerts = PQntuples(certRes);
                        spdlog::info("Found {} certificates to save to LDAP for upload {}", totalCerts, uploadId);

                        for (int i = 0; i < totalCerts; i++) {
                            std::string certId = PQgetvalue(certRes, i, 0);
                            const char* certData = PQgetvalue(certRes, i, 1);
                            int certLen = PQgetlength(certRes, i, 1);
                            std::string certType = PQgetvalue(certRes, i, 2);
                            std::string country = PQgetvalue(certRes, i, 3);
                            std::string subjectDn = PQgetvalue(certRes, i, 4);
                            std::string issuerDn = PQgetvalue(certRes, i, 5);
                            std::string serialNumber = PQgetvalue(certRes, i, 6);
                            std::string fingerprint = PQgetvalue(certRes, i, 7);

                            std::vector<uint8_t> certBytes(certData, certData + certLen);

                            try {
                                // Save certificate to LDAP using existing function
                                std::string ldapDn = saveCertificateToLdap(ld, certType, country,
                                                                           subjectDn, issuerDn,
                                                                           serialNumber, fingerprint,
                                                                           certBytes);

                                if (!ldapDn.empty()) {
                                    storedCerts++;

                                    // Mark as stored in LDAP
                                    std::string updateQuery = "UPDATE certificate SET stored_in_ldap = true "
                                                             "WHERE id = '" + certId + "'";
                                    PGresult* updateRes = PQexec(conn, updateQuery.c_str());
                                    PQclear(updateRes);

                                    spdlog::debug("Saved certificate {} to LDAP: {}", certId, ldapDn);
                                }
                            } catch (const std::exception& e) {
                                spdlog::warn("Failed to save certificate {} to LDAP: {}", certId, e.what());
                            }

                            // Send progress update every 10 certificates
                            if ((i + 1) % 10 == 0 || (i + 1) == totalCerts) {
                                ProgressManager::getInstance().sendProgress(
                                    ProcessingProgress::create(uploadId, ProcessingStage::LDAP_SAVING_IN_PROGRESS,
                                        i + 1, totalCerts, "LDAP 저장 중: " + std::to_string(i + 1) + "/" + std::to_string(totalCerts)));
                            }
                        }
                    }
                    PQclear(certRes);

                    // Get CRLs that haven't been stored in LDAP yet
                    std::string crlQuery = "SELECT id, crl_binary, country_code, issuer_dn, fingerprint_sha256 "
                                          "FROM crl "
                                          "WHERE upload_id = '" + uploadId + "' "
                                          "AND (stored_in_ldap = false OR stored_in_ldap IS NULL) "
                                          "ORDER BY id";
                    PGresult* crlRes = PQexec(conn, crlQuery.c_str());

                    int totalCrls = 0;
                    int storedCrls = 0;

                    if (PQresultStatus(crlRes) == PGRES_TUPLES_OK) {
                        totalCrls = PQntuples(crlRes);
                        spdlog::info("Found {} CRLs to save to LDAP for upload {}", totalCrls, uploadId);

                        for (int i = 0; i < totalCrls; i++) {
                            std::string crlId = PQgetvalue(crlRes, i, 0);
                            const char* crlData = PQgetvalue(crlRes, i, 1);
                            int crlLen = PQgetlength(crlRes, i, 1);
                            std::string country = PQgetvalue(crlRes, i, 2);
                            std::string issuerDn = PQgetvalue(crlRes, i, 3);
                            std::string fingerprint = PQgetvalue(crlRes, i, 4);

                            std::vector<uint8_t> crlBytes(crlData, crlData + crlLen);

                            try {
                                // Save CRL to LDAP using existing helper function
                                std::string ldapDn = saveCrlToLdap(ld, country, issuerDn, fingerprint, crlBytes);

                                if (!ldapDn.empty()) {
                                    storedCrls++;

                                    // Mark as stored in LDAP
                                    std::string updateQuery = "UPDATE crl SET stored_in_ldap = true "
                                                             "WHERE id = '" + crlId + "'";
                                    PGresult* updateRes = PQexec(conn, updateQuery.c_str());
                                    PQclear(updateRes);

                                    spdlog::debug("Saved CRL {} to LDAP: {}", crlId, ldapDn);

                                    // Send progress update
                                    ProgressManager::getInstance().sendProgress(
                                        ProcessingProgress::create(uploadId, ProcessingStage::LDAP_SAVING_IN_PROGRESS,
                                            storedCerts + storedCrls, totalCerts + totalCrls,
                                            "LDAP 저장 중: " + std::to_string(storedCerts + storedCrls) + "/" +
                                            std::to_string(totalCerts + totalCrls)));
                                }
                            } catch (const std::exception& e) {
                                spdlog::warn("Failed to save CRL {} to LDAP: {}", crlId, e.what());
                            }
                        }
                    }
                    PQclear(crlRes);

                    ldap_unbind_ext_s(ld, nullptr, nullptr);

                    // Send LDAP saving completed
                    ProgressManager::getInstance().sendProgress(
                        ProcessingProgress::create(uploadId, ProcessingStage::COMPLETED,
                            100, 100, "LDAP 저장 완료: " + std::to_string(storedCerts) + "개 인증서, " +
                                     std::to_string(storedCrls) + "개 CRL"));

                    spdlog::info("LDAP save completed for upload {}: {} certificates, {} CRLs",
                                uploadId, storedCerts, storedCrls);
                } catch (const std::exception& e) {
                    spdlog::error("LDAP save failed for upload {}: {}", uploadId, e.what());
                    ProgressManager::getInstance().sendProgress(
                        ProcessingProgress::create(uploadId, ProcessingStage::FAILED,
                            0, 0, std::string("LDAP 저장 실패: ") + e.what()));
                }

                PQfinish(conn);
            }).detach();

            Json::Value result;
            result["success"] = true;
            result["message"] = "LDAP save processing started";
            result["uploadId"] = uploadId;

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Post}
    );

    // Health check endpoint
    app.registerHandler(
        "/api/health",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            Json::Value result;
            result["status"] = "UP";
            result["service"] = "icao-local-pkd";
            result["version"] = "1.0.0";
            result["timestamp"] = trantor::Date::now().toFormattedString(false);

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // Database health check endpoint
    app.registerHandler(
        "/api/health/database",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            auto result = checkDatabase();

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            if (result["status"].asString() != "UP") {
                resp->setStatusCode(drogon::k503ServiceUnavailable);
            }
            callback(resp);
        },
        {drogon::Get}
    );

    // LDAP health check endpoint
    app.registerHandler(
        "/api/health/ldap",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            auto result = checkLdap();

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            if (result["status"].asString() != "UP") {
                resp->setStatusCode(drogon::k503ServiceUnavailable);
            }
            callback(resp);
        },
        {drogon::Get}
    );

    // Re-validate DSC certificates endpoint
    app.registerHandler(
        "/api/validation/revalidate",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("POST /api/validation/revalidate - Re-validate DSC certificates");

            // Connect to PostgreSQL
            std::string conninfo = "host=" + appConfig.dbHost +
                                  " port=" + std::to_string(appConfig.dbPort) +
                                  " dbname=" + appConfig.dbName +
                                  " user=" + appConfig.dbUser +
                                  " password=" + appConfig.dbPassword;

            PGconn* conn = PQconnectdb(conninfo.c_str());
            if (PQstatus(conn) != CONNECTION_OK) {
                Json::Value error;
                error["success"] = false;
                error["message"] = "Database connection failed";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
                return;
            }

            // Get optional limit from query param (default: 10 for testing)
            int limit = 10;
            auto limitParam = req->getParameter("limit");
            if (!limitParam.empty()) {
                try {
                    limit = std::stoi(limitParam);
                } catch (...) {}
            }

            // Get DSC certificates that need re-validation (CSCA_NOT_FOUND)
            std::string query = "SELECT c.id, c.issuer_dn, c.certificate_binary "
                               "FROM certificate c "
                               "JOIN validation_result vr ON c.id = vr.certificate_id "
                               "WHERE c.certificate_type IN ('DSC', 'DSC_NC') "
                               "AND vr.error_code = 'CSCA_NOT_FOUND' "
                               "LIMIT " + std::to_string(limit);

            PGresult* res = PQexec(conn, query.c_str());
            if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                Json::Value error;
                error["success"] = false;
                error["message"] = "Query failed: " + std::string(PQerrorMessage(conn));
                PQclear(res);
                PQfinish(conn);
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
                return;
            }

            int totalDscs = PQntuples(res);
            int validatedCount = 0;
            int cscaFoundCount = 0;
            int signatureValidCount = 0;
            Json::Value details(Json::arrayValue);

            for (int i = 0; i < totalDscs; i++) {
                std::string certId = PQgetvalue(res, i, 0);
                std::string issuerDn = PQgetvalue(res, i, 1);

                // Try to find CSCA
                X509* csca = findCscaByIssuerDn(conn, issuerDn);

                Json::Value detail;
                detail["certId"] = certId;
                detail["issuerDn"] = issuerDn.substr(0, 100);

                if (csca) {
                    cscaFoundCount++;
                    detail["cscaFound"] = true;

                    // Parse DSC certificate
                    char* certData = PQgetvalue(res, i, 2);
                    int certLen = PQgetlength(res, i, 2);

                    std::vector<uint8_t> derBytes;
                    if (certLen > 2 && certData[0] == '\\' && certData[1] == 'x') {
                        for (int j = 2; j < certLen; j += 2) {
                            char hex[3] = {certData[j], certData[j+1], 0};
                            derBytes.push_back(static_cast<uint8_t>(strtol(hex, nullptr, 16)));
                        }
                    }

                    if (!derBytes.empty()) {
                        const uint8_t* data = derBytes.data();
                        X509* dsc = d2i_X509(nullptr, &data, static_cast<long>(derBytes.size()));
                        if (dsc) {
                            EVP_PKEY* cscaPubKey = X509_get_pubkey(csca);
                            if (cscaPubKey) {
                                int verifyResult = X509_verify(dsc, cscaPubKey);
                                detail["signatureValid"] = (verifyResult == 1);
                                if (verifyResult == 1) {
                                    signatureValidCount++;
                                }
                                EVP_PKEY_free(cscaPubKey);
                            }
                            X509_free(dsc);
                        }
                    }

                    X509_free(csca);
                } else {
                    detail["cscaFound"] = false;
                    detail["signatureValid"] = false;

                    // Check if CSCA exists with simple query
                    std::string checkQuery = "SELECT COUNT(*) FROM certificate WHERE certificate_type = 'CSCA' AND LOWER(subject_dn) = LOWER($1)";
                    const char* paramValues[1] = {issuerDn.c_str()};
                    PGresult* checkRes = PQexecParams(conn, checkQuery.c_str(), 1, nullptr, paramValues, nullptr, nullptr, 0);
                    if (PQresultStatus(checkRes) == PGRES_TUPLES_OK) {
                        int count = std::stoi(PQgetvalue(checkRes, 0, 0));
                        detail["cscaExistsInDb"] = count;
                    }
                    PQclear(checkRes);
                }

                validatedCount++;
                details.append(detail);
            }

            PQclear(res);
            PQfinish(conn);

            Json::Value result;
            result["success"] = true;
            result["totalProcessed"] = validatedCount;
            result["cscaFoundCount"] = cscaFoundCount;
            result["signatureValidCount"] = signatureValidCount;
            result["details"] = details;

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Post, drogon::Get}
    );

    // Upload LDIF file endpoint
    app.registerHandler(
        "/api/upload/ldif",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
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
                std::string fileName = file.getFileName();
                std::string content = std::string(file.fileData(), file.fileLength());
                std::vector<uint8_t> contentBytes(content.begin(), content.end());
                int64_t fileSize = static_cast<int64_t>(content.size());

                // Compute file hash
                std::string fileHash = computeFileHash(contentBytes);

                // Connect to database
                std::string conninfo = "host=" + appConfig.dbHost +
                                      " port=" + std::to_string(appConfig.dbPort) +
                                      " dbname=" + appConfig.dbName +
                                      " user=" + appConfig.dbUser +
                                      " password=" + appConfig.dbPassword;

                PGconn* conn = PQconnectdb(conninfo.c_str());
                if (PQstatus(conn) != CONNECTION_OK) {
                    std::string error = PQerrorMessage(conn);
                    PQfinish(conn);
                    Json::Value errorResp;
                    errorResp["success"] = false;
                    errorResp["message"] = "Database connection failed: " + error;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(errorResp);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
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

                // Check for duplicate file
                Json::Value duplicateCheck = checkDuplicateFile(conn, fileHash);
                if (!duplicateCheck.isNull()) {
                    PQfinish(conn);

                    Json::Value error;
                    error["success"] = false;
                    error["message"] = "Duplicate file detected. This file has already been uploaded.";

                    Json::Value errorDetail;
                    errorDetail["code"] = "DUPLICATE_FILE";
                    errorDetail["detail"] = "A file with the same content (SHA-256 hash) already exists in the system.";
                    error["error"] = errorDetail;

                    error["existingUpload"] = duplicateCheck;

                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k409Conflict);
                    callback(resp);

                    spdlog::warn("Duplicate LDIF file upload rejected: hash={}, existing_upload_id={}, original_file={}",
                                 fileHash.substr(0, 16),
                                 duplicateCheck["uploadId"].asString(),
                                 duplicateCheck["fileName"].asString());
                    return;
                }

                // Save upload record with processing mode
                std::string uploadId = saveUploadRecord(conn, fileName, fileSize, "LDIF", fileHash, processingMode);

                // Save file to disk for manual mode processing
                std::string uploadDir = "./uploads";
                std::string filePath = uploadDir + "/" + uploadId + ".ldif";
                std::ofstream outFile(filePath, std::ios::binary);
                if (outFile.is_open()) {
                    outFile.write(reinterpret_cast<const char*>(contentBytes.data()), contentBytes.size());
                    outFile.close();

                    // Update file_path in database
                    std::string updateQuery = "UPDATE uploaded_file SET file_path = '" + filePath + "' WHERE id = '" + uploadId + "'";
                    PGresult* updateRes = PQexec(conn, updateQuery.c_str());
                    PQclear(updateRes);
                }

                PQfinish(conn);

                // Start async processing only in AUTO mode
                if (processingMode == "AUTO" || processingMode == "auto") {
                    processLdifFileAsync(uploadId, contentBytes);
                }

                // Return success response
                Json::Value result;
                result["success"] = true;
                if (processingMode == "MANUAL" || processingMode == "manual") {
                    result["message"] = "LDIF file uploaded successfully. Use parse/validate/ldap endpoints to process manually.";
                } else {
                    result["message"] = "LDIF file uploaded successfully. Processing started.";
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

            } catch (const std::exception& e) {
                spdlog::error("LDIF upload failed: {}", e.what());
                Json::Value error;
                error["success"] = false;
                error["message"] = std::string("Upload failed: ") + e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Post}
    );

    // Upload Master List file endpoint
    app.registerHandler(
        "/api/upload/masterlist",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
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
                std::string fileName = file.getFileName();
                std::string content = std::string(file.fileData(), file.fileLength());
                std::vector<uint8_t> contentBytes(content.begin(), content.end());
                int64_t fileSize = static_cast<int64_t>(content.size());

                // Compute file hash
                std::string fileHash = computeFileHash(contentBytes);

                // Connect to database
                std::string conninfo = "host=" + appConfig.dbHost +
                                      " port=" + std::to_string(appConfig.dbPort) +
                                      " dbname=" + appConfig.dbName +
                                      " user=" + appConfig.dbUser +
                                      " password=" + appConfig.dbPassword;

                PGconn* conn = PQconnectdb(conninfo.c_str());
                if (PQstatus(conn) != CONNECTION_OK) {
                    std::string error = PQerrorMessage(conn);
                    PQfinish(conn);
                    Json::Value errorResp;
                    errorResp["success"] = false;
                    errorResp["message"] = "Database connection failed: " + error;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(errorResp);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
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

                // Check for duplicate file
                Json::Value duplicateCheck = checkDuplicateFile(conn, fileHash);
                if (!duplicateCheck.isNull()) {
                    PQfinish(conn);

                    Json::Value error;
                    error["success"] = false;
                    error["message"] = "Duplicate file detected. This file has already been uploaded.";

                    Json::Value errorDetail;
                    errorDetail["code"] = "DUPLICATE_FILE";
                    errorDetail["detail"] = "A file with the same content (SHA-256 hash) already exists in the system.";
                    error["error"] = errorDetail;

                    error["existingUpload"] = duplicateCheck;

                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k409Conflict);
                    callback(resp);

                    spdlog::warn("Duplicate Master List file upload rejected: hash={}, existing_upload_id={}, original_file={}",
                                 fileHash.substr(0, 16),
                                 duplicateCheck["uploadId"].asString(),
                                 duplicateCheck["fileName"].asString());
                    return;
                }

                // Save upload record with processing mode
                std::string uploadId = saveUploadRecord(conn, fileName, fileSize, "ML", fileHash, processingMode);

                // Save file to disk for manual mode processing
                std::string uploadDir = "./uploads";
                std::string filePath = uploadDir + "/" + uploadId + ".ml";
                std::ofstream outFile(filePath, std::ios::binary);
                if (outFile.is_open()) {
                    outFile.write(reinterpret_cast<const char*>(contentBytes.data()), contentBytes.size());
                    outFile.close();

                    // Update file_path in database
                    std::string updateQuery = "UPDATE uploaded_file SET file_path = '" + filePath + "' WHERE id = '" + uploadId + "'";
                    PGresult* updateRes = PQexec(conn, updateQuery.c_str());
                    PQclear(updateRes);
                }

                PQfinish(conn);

                // Start async processing only in AUTO mode
                if (processingMode == "AUTO" || processingMode == "auto") {
                    processMasterListFileAsync(uploadId, contentBytes);
                }

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

            } catch (const std::exception& e) {
                spdlog::error("Master List upload failed: {}", e.what());
                Json::Value error;
                error["success"] = false;
                error["message"] = std::string("Upload failed: ") + e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Post}
    );

    // Upload statistics endpoint - returns UploadStatisticsOverview format
    app.registerHandler(
        "/api/upload/statistics",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/upload/statistics");

            std::string conninfo = "host=" + appConfig.dbHost +
                                  " port=" + std::to_string(appConfig.dbPort) +
                                  " dbname=" + appConfig.dbName +
                                  " user=" + appConfig.dbUser +
                                  " password=" + appConfig.dbPassword;

            PGconn* conn = PQconnectdb(conninfo.c_str());
            Json::Value result;

            if (PQstatus(conn) == CONNECTION_OK) {
                // Get total uploads
                PGresult* res = PQexec(conn, "SELECT COUNT(*) FROM uploaded_file");
                result["totalUploads"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                // Get successful uploads
                res = PQexec(conn, "SELECT COUNT(*) FROM uploaded_file WHERE status = 'COMPLETED'");
                result["successfulUploads"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                // Get failed uploads
                res = PQexec(conn, "SELECT COUNT(*) FROM uploaded_file WHERE status = 'FAILED'");
                result["failedUploads"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                // Get total certificates (sum of certificate_count from all uploads)
                res = PQexec(conn, "SELECT COALESCE(SUM(csca_count + dsc_count + dsc_nc_count), 0) FROM uploaded_file");
                result["totalCertificates"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                // Get CRL count (sum of crl_count from all uploads)
                res = PQexec(conn, "SELECT COALESCE(SUM(crl_count), 0) FROM uploaded_file");
                result["crlCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                // Get CSCA count from certificate table
                res = PQexec(conn, "SELECT COUNT(*) FROM certificate WHERE certificate_type = 'CSCA'");
                result["cscaCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                // Get DSC count from certificate table (conformant DSC only)
                res = PQexec(conn, "SELECT COUNT(*) FROM certificate WHERE certificate_type = 'DSC'");
                result["dscCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                // Get DSC_NC count from certificate table (non-conformant DSC)
                res = PQexec(conn, "SELECT COUNT(*) FROM certificate WHERE certificate_type = 'DSC_NC'");
                result["dscNcCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                // Get distinct country count from certificate table
                res = PQexec(conn, "SELECT COUNT(DISTINCT country_code) FROM certificate");
                result["countriesCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                // Get Master List count (sum of ml_count from all uploads)
                res = PQexec(conn, "SELECT COALESCE(SUM(ml_count), 0) FROM uploaded_file");
                result["mlCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                // Validation statistics from validation_result table
                Json::Value validation;

                res = PQexec(conn, "SELECT COUNT(*) FROM validation_result WHERE validation_status = 'VALID'");
                validation["validCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                res = PQexec(conn, "SELECT COUNT(*) FROM validation_result WHERE validation_status = 'INVALID'");
                validation["invalidCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                res = PQexec(conn, "SELECT COUNT(*) FROM validation_result WHERE validation_status = 'PENDING'");
                validation["pendingCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                res = PQexec(conn, "SELECT COUNT(*) FROM validation_result WHERE validation_status = 'ERROR'");
                validation["errorCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                res = PQexec(conn, "SELECT COUNT(*) FROM validation_result WHERE trust_chain_valid = TRUE");
                validation["trustChainValidCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                res = PQexec(conn, "SELECT COUNT(*) FROM validation_result WHERE trust_chain_valid = FALSE");
                validation["trustChainInvalidCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                res = PQexec(conn, "SELECT COUNT(*) FROM validation_result WHERE csca_found = FALSE AND certificate_type IN ('DSC', 'DSC_NC')");
                validation["cscaNotFoundCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                res = PQexec(conn, "SELECT COUNT(*) FROM validation_result WHERE is_expired = TRUE");
                validation["expiredCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                res = PQexec(conn, "SELECT COUNT(*) FROM validation_result WHERE crl_check_status = 'REVOKED'");
                validation["revokedCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                result["validation"] = validation;

            } else {
                result["totalUploads"] = 0;
                result["successfulUploads"] = 0;
                result["failedUploads"] = 0;
                result["totalCertificates"] = 0;
                result["cscaCount"] = 0;
                result["dscCount"] = 0;
                result["dscNcCount"] = 0;
                result["crlCount"] = 0;
                result["mlCount"] = 0;
                result["countriesCount"] = 0;

                // Empty validation stats
                Json::Value validation;
                validation["validCount"] = 0;
                validation["invalidCount"] = 0;
                validation["pendingCount"] = 0;
                validation["trustChainValidCount"] = 0;
                validation["trustChainInvalidCount"] = 0;
                validation["cscaNotFoundCount"] = 0;
                validation["expiredCount"] = 0;
                validation["revokedCount"] = 0;
                result["validation"] = validation;
            }

            PQfinish(conn);
            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // Upload history endpoint - returns PageResponse format
    app.registerHandler(
        "/api/upload/history",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/upload/history");

            // Get query parameters
            int page = 0;
            int size = 20;
            if (auto p = req->getParameter("page"); !p.empty()) {
                page = std::stoi(p);
            }
            if (auto s = req->getParameter("size"); !s.empty()) {
                size = std::stoi(s);
            }

            std::string conninfo = "host=" + appConfig.dbHost +
                                  " port=" + std::to_string(appConfig.dbPort) +
                                  " dbname=" + appConfig.dbName +
                                  " user=" + appConfig.dbUser +
                                  " password=" + appConfig.dbPassword;

            PGconn* conn = PQconnectdb(conninfo.c_str());
            Json::Value result;
            result["content"] = Json::Value(Json::arrayValue);
            result["page"] = page;
            result["size"] = size;

            if (PQstatus(conn) == CONNECTION_OK) {
                // Get total count
                PGresult* countRes = PQexec(conn, "SELECT COUNT(*) FROM uploaded_file");
                int totalElements = (PQntuples(countRes) > 0) ? std::stoi(PQgetvalue(countRes, 0, 0)) : 0;
                PQclear(countRes);

                result["totalElements"] = totalElements;
                result["totalPages"] = (totalElements + size - 1) / size;
                result["first"] = (page == 0);
                result["last"] = (page >= result["totalPages"].asInt() - 1);

                // Get paginated results with validation statistics
                int offset = page * size;
                std::string query = "SELECT id, file_name, file_format, file_size, status, "
                                   "csca_count, dsc_count, dsc_nc_count, crl_count, COALESCE(ml_count, 0), error_message, "
                                   "upload_timestamp, completed_timestamp, "
                                   "COALESCE(validation_valid_count, 0), COALESCE(validation_invalid_count, 0), "
                                   "COALESCE(validation_pending_count, 0), COALESCE(validation_error_count, 0), "
                                   "COALESCE(trust_chain_valid_count, 0), COALESCE(trust_chain_invalid_count, 0), "
                                   "COALESCE(csca_not_found_count, 0), COALESCE(expired_count, 0), COALESCE(revoked_count, 0) "
                                   "FROM uploaded_file ORDER BY upload_timestamp DESC "
                                   "LIMIT " + std::to_string(size) + " OFFSET " + std::to_string(offset);

                PGresult* res = PQexec(conn, query.c_str());
                if (PQresultStatus(res) == PGRES_TUPLES_OK) {
                    for (int i = 0; i < PQntuples(res); i++) {
                        Json::Value item;
                        item["id"] = PQgetvalue(res, i, 0);
                        item["fileName"] = PQgetvalue(res, i, 1);
                        item["fileFormat"] = PQgetvalue(res, i, 2);
                        item["fileSize"] = static_cast<Json::Int64>(std::stoll(PQgetvalue(res, i, 3)));
                        item["status"] = PQgetvalue(res, i, 4);
                        int cscaCount = std::stoi(PQgetvalue(res, i, 5));
                        int dscCount = std::stoi(PQgetvalue(res, i, 6));
                        int dscNcCount = std::stoi(PQgetvalue(res, i, 7));
                        item["cscaCount"] = cscaCount;
                        item["dscCount"] = dscCount;
                        item["dscNcCount"] = dscNcCount;
                        item["certificateCount"] = cscaCount + dscCount + dscNcCount;  // Keep for backward compatibility
                        item["crlCount"] = std::stoi(PQgetvalue(res, i, 8));
                        item["mlCount"] = std::stoi(PQgetvalue(res, i, 9));  // Master List count
                        item["errorMessage"] = PQgetvalue(res, i, 10) ? PQgetvalue(res, i, 10) : "";
                        item["createdAt"] = PQgetvalue(res, i, 11);
                        item["updatedAt"] = PQgetvalue(res, i, 12);

                        // Validation statistics
                        Json::Value validation;
                        validation["validCount"] = std::stoi(PQgetvalue(res, i, 13));
                        validation["invalidCount"] = std::stoi(PQgetvalue(res, i, 14));
                        validation["pendingCount"] = std::stoi(PQgetvalue(res, i, 15));
                        validation["errorCount"] = std::stoi(PQgetvalue(res, i, 16));
                        validation["trustChainValidCount"] = std::stoi(PQgetvalue(res, i, 17));
                        validation["trustChainInvalidCount"] = std::stoi(PQgetvalue(res, i, 18));
                        validation["cscaNotFoundCount"] = std::stoi(PQgetvalue(res, i, 19));
                        validation["expiredCount"] = std::stoi(PQgetvalue(res, i, 20));
                        validation["revokedCount"] = std::stoi(PQgetvalue(res, i, 21));
                        item["validation"] = validation;

                        result["content"].append(item);
                    }
                }
                PQclear(res);
            } else {
                result["totalElements"] = 0;
                result["totalPages"] = 0;
                result["first"] = true;
                result["last"] = true;
            }

            PQfinish(conn);
            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // Get individual upload status - GET /api/upload/{uploadId}
    app.registerHandler(
        "/api/upload/{uploadId}",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& uploadId) {
            spdlog::info("GET /api/upload/{}", uploadId);

            std::string conninfo = "host=" + appConfig.dbHost +
                                  " port=" + std::to_string(appConfig.dbPort) +
                                  " dbname=" + appConfig.dbName +
                                  " user=" + appConfig.dbUser +
                                  " password=" + appConfig.dbPassword;

            PGconn* conn = PQconnectdb(conninfo.c_str());
            Json::Value result;

            if (PQstatus(conn) == CONNECTION_OK) {
                // Get upload details with validation statistics
                std::string query = "SELECT id, file_name, file_format, file_size, status, processing_mode, "
                                   "csca_count, dsc_count, dsc_nc_count, crl_count, COALESCE(ml_count, 0), "
                                   "total_entries, processed_entries, error_message, "
                                   "upload_timestamp, completed_timestamp, "
                                   "COALESCE(validation_valid_count, 0), COALESCE(validation_invalid_count, 0), "
                                   "COALESCE(validation_pending_count, 0), COALESCE(validation_error_count, 0), "
                                   "COALESCE(trust_chain_valid_count, 0), COALESCE(trust_chain_invalid_count, 0), "
                                   "COALESCE(csca_not_found_count, 0), COALESCE(expired_count, 0), COALESCE(revoked_count, 0) "
                                   "FROM uploaded_file WHERE id = '" + uploadId + "'";

                PGresult* res = PQexec(conn, query.c_str());
                if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
                    result["success"] = true;
                    Json::Value data;
                    data["id"] = PQgetvalue(res, 0, 0);
                    data["fileName"] = PQgetvalue(res, 0, 1);
                    data["fileFormat"] = PQgetvalue(res, 0, 2);
                    data["fileSize"] = static_cast<Json::Int64>(std::stoll(PQgetvalue(res, 0, 3)));
                    data["status"] = PQgetvalue(res, 0, 4);
                    data["processingMode"] = PQgetvalue(res, 0, 5);

                    int cscaCount = std::stoi(PQgetvalue(res, 0, 6));
                    int dscCount = std::stoi(PQgetvalue(res, 0, 7));
                    int dscNcCount = std::stoi(PQgetvalue(res, 0, 8));
                    data["cscaCount"] = cscaCount;
                    data["dscCount"] = dscCount;
                    data["dscNcCount"] = dscNcCount;
                    data["certificateCount"] = cscaCount + dscCount + dscNcCount;
                    data["crlCount"] = std::stoi(PQgetvalue(res, 0, 9));
                    data["mlCount"] = std::stoi(PQgetvalue(res, 0, 10));
                    data["totalEntries"] = std::stoi(PQgetvalue(res, 0, 11));
                    data["processedEntries"] = std::stoi(PQgetvalue(res, 0, 12));
                    data["errorMessage"] = PQgetvalue(res, 0, 13) ? PQgetvalue(res, 0, 13) : "";
                    data["createdAt"] = PQgetvalue(res, 0, 14);
                    data["updatedAt"] = PQgetvalue(res, 0, 15);

                    // Validation statistics
                    Json::Value validation;
                    validation["validCount"] = std::stoi(PQgetvalue(res, 0, 16));
                    validation["invalidCount"] = std::stoi(PQgetvalue(res, 0, 17));
                    validation["pendingCount"] = std::stoi(PQgetvalue(res, 0, 18));
                    validation["errorCount"] = std::stoi(PQgetvalue(res, 0, 19));
                    validation["trustChainValidCount"] = std::stoi(PQgetvalue(res, 0, 20));
                    validation["trustChainInvalidCount"] = std::stoi(PQgetvalue(res, 0, 21));
                    validation["cscaNotFoundCount"] = std::stoi(PQgetvalue(res, 0, 22));
                    validation["expiredCount"] = std::stoi(PQgetvalue(res, 0, 23));
                    validation["revokedCount"] = std::stoi(PQgetvalue(res, 0, 24));
                    data["validation"] = validation;

                    // Check LDAP upload status by querying certificate table
                    std::string ldapQuery = "SELECT COUNT(*) as total, "
                                          "SUM(CASE WHEN stored_in_ldap = true THEN 1 ELSE 0 END) as in_ldap "
                                          "FROM certificate WHERE upload_id = '" + uploadId + "'";
                    PGresult* ldapRes = PQexec(conn, ldapQuery.c_str());
                    if (PQresultStatus(ldapRes) == PGRES_TUPLES_OK && PQntuples(ldapRes) > 0) {
                        int totalCerts = std::stoi(PQgetvalue(ldapRes, 0, 0));
                        int ldapCerts = PQgetvalue(ldapRes, 0, 1) ? std::stoi(PQgetvalue(ldapRes, 0, 1)) : 0;
                        data["ldapUploadedCount"] = ldapCerts;
                        data["ldapPendingCount"] = totalCerts - ldapCerts;
                    } else {
                        data["ldapUploadedCount"] = 0;
                        data["ldapPendingCount"] = 0;
                    }
                    PQclear(ldapRes);

                    result["data"] = data;
                } else {
                    result["success"] = false;
                    result["error"] = "Upload not found";
                }
                PQclear(res);
            } else {
                result["success"] = false;
                result["error"] = "Database connection failed";
            }

            PQfinish(conn);
            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // Country statistics endpoint - GET /api/upload/countries
    app.registerHandler(
        "/api/upload/countries",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/upload/countries");

            // Get query parameters for limit (default 20)
            int limit = 20;
            if (auto l = req->getParameter("limit"); !l.empty()) {
                limit = std::stoi(l);
                if (limit > 100) limit = 100;  // Cap at 100
            }

            std::string conninfo = "host=" + appConfig.dbHost +
                                  " port=" + std::to_string(appConfig.dbPort) +
                                  " dbname=" + appConfig.dbName +
                                  " user=" + appConfig.dbUser +
                                  " password=" + appConfig.dbPassword;

            PGconn* conn = PQconnectdb(conninfo.c_str());
            Json::Value result(Json::arrayValue);

            if (PQstatus(conn) == CONNECTION_OK) {
                // Query certificate counts by country, ordered by total count
                std::string query =
                    "SELECT country_code, "
                    "SUM(CASE WHEN certificate_type = 'CSCA' THEN 1 ELSE 0 END) as csca_count, "
                    "SUM(CASE WHEN certificate_type = 'DSC' THEN 1 ELSE 0 END) as dsc_count, "
                    "SUM(CASE WHEN certificate_type = 'DSC_NC' THEN 1 ELSE 0 END) as dsc_nc_count, "
                    "COUNT(*) as total "
                    "FROM certificate "
                    "WHERE country_code IS NOT NULL AND country_code != '' "
                    "GROUP BY country_code "
                    "ORDER BY total DESC "
                    "LIMIT " + std::to_string(limit);

                PGresult* res = PQexec(conn, query.c_str());
                if (PQresultStatus(res) == PGRES_TUPLES_OK) {
                    for (int i = 0; i < PQntuples(res); i++) {
                        Json::Value item;
                        item["country"] = PQgetvalue(res, i, 0);
                        item["csca"] = std::stoi(PQgetvalue(res, i, 1));
                        item["dsc"] = std::stoi(PQgetvalue(res, i, 2));
                        item["dscNc"] = std::stoi(PQgetvalue(res, i, 3));
                        item["total"] = std::stoi(PQgetvalue(res, i, 4));
                        result.append(item);
                    }
                }
                PQclear(res);
            }

            PQfinish(conn);
            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // SSE Progress stream endpoint - GET /api/progress/stream/{uploadId}
    app.registerHandler(
        "/api/progress/stream/{uploadId}",
        [](const drogon::HttpRequestPtr& /* req */,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& uploadId) {
            spdlog::info("GET /api/progress/stream/{} - SSE progress stream", uploadId);

            // Create SSE response with chunked encoding
            // Use shared_ptr wrapper since ResponseStreamPtr is unique_ptr (non-copyable)
            auto resp = drogon::HttpResponse::newAsyncStreamResponse(
                [uploadIdCopy = uploadId](drogon::ResponseStreamPtr streamPtr) {
                    // Convert unique_ptr to shared_ptr for lambda capture
                    auto stream = std::shared_ptr<drogon::ResponseStream>(streamPtr.release());

                    // Send initial connection event
                    std::string connectedEvent = "event: connected\ndata: {\"message\":\"SSE connection established for " + uploadIdCopy + "\"}\n\n";
                    stream->send(connectedEvent);

                    // Register callback for progress updates
                    ProgressManager::getInstance().registerSseCallback(uploadIdCopy,
                        [stream, uploadIdCopy](const std::string& data) {
                            try {
                                stream->send(data);
                            } catch (...) {
                                ProgressManager::getInstance().unregisterSseCallback(uploadIdCopy);
                            }
                        });

                    // Send cached progress if available
                    auto progress = ProgressManager::getInstance().getProgress(uploadIdCopy);
                    if (progress) {
                        std::string sseData = "event: progress\ndata: " + progress->toJson() + "\n\n";
                        stream->send(sseData);
                    }
                });

            // Use setContentTypeString to properly set SSE content type
            // This replaces the default text/plain set by newAsyncStreamResponse
            resp->setContentTypeString("text/event-stream; charset=utf-8");
            resp->addHeader("Cache-Control", "no-cache");
            resp->addHeader("Connection", "keep-alive");
            resp->addHeader("Access-Control-Allow-Origin", "*");

            callback(resp);
        },
        {drogon::Get}
    );

    // Progress status endpoint - GET /api/progress/status/{uploadId}
    app.registerHandler(
        "/api/progress/status/{uploadId}",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& uploadId) {
            spdlog::info("GET /api/progress/status/{}", uploadId);

            auto progress = ProgressManager::getInstance().getProgress(uploadId);

            Json::Value result;
            if (progress) {
                result["exists"] = true;
                result["uploadId"] = progress->uploadId;
                result["stage"] = stageToString(progress->stage);
                result["stageName"] = stageToKorean(progress->stage);
                result["percentage"] = progress->percentage;
                result["processedCount"] = progress->processedCount;
                result["totalCount"] = progress->totalCount;
                result["message"] = progress->message;
                result["errorMessage"] = progress->errorMessage;
            } else {
                result["exists"] = false;
            }

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // PA statistics endpoint - returns PAStatisticsOverview format
    app.registerHandler(
        "/api/pa/statistics",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/pa/statistics");

            // Return PAStatisticsOverview format matching frontend expectations
            Json::Value result;
            result["totalVerifications"] = 0;
            result["validCount"] = 0;
            result["invalidCount"] = 0;
            result["errorCount"] = 0;
            result["averageProcessingTimeMs"] = 0;
            result["countriesVerified"] = 0;

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // PA verify endpoint - POST /api/pa/verify
    app.registerHandler(
        "/api/pa/verify",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("POST /api/pa/verify - Passive Authentication verification");

            // Mock response for PA verification
            Json::Value result;
            result["success"] = true;

            Json::Value data;
            data["id"] = "pa-" + std::to_string(std::time(nullptr));
            data["status"] = "VALID";
            data["overallValid"] = true;
            data["verifiedAt"] = trantor::Date::now().toFormattedString(false);
            data["processingTimeMs"] = 150;

            // Step results
            Json::Value sodParsing;
            sodParsing["step"] = "SOD_PARSING";
            sodParsing["status"] = "SUCCESS";
            sodParsing["message"] = "SOD 파싱 완료";
            data["sodParsing"] = sodParsing;

            Json::Value dscExtraction;
            dscExtraction["step"] = "DSC_EXTRACTION";
            dscExtraction["status"] = "SUCCESS";
            dscExtraction["message"] = "DSC 인증서 추출 완료";
            data["dscExtraction"] = dscExtraction;

            Json::Value cscaLookup;
            cscaLookup["step"] = "CSCA_LOOKUP";
            cscaLookup["status"] = "SUCCESS";
            cscaLookup["message"] = "CSCA 인증서 조회 완료";
            data["cscaLookup"] = cscaLookup;

            Json::Value trustChainValidation;
            trustChainValidation["step"] = "TRUST_CHAIN_VALIDATION";
            trustChainValidation["status"] = "SUCCESS";
            trustChainValidation["message"] = "Trust Chain 검증 완료";
            data["trustChainValidation"] = trustChainValidation;

            Json::Value sodSignatureValidation;
            sodSignatureValidation["step"] = "SOD_SIGNATURE_VALIDATION";
            sodSignatureValidation["status"] = "SUCCESS";
            sodSignatureValidation["message"] = "SOD 서명 검증 완료";
            data["sodSignatureValidation"] = sodSignatureValidation;

            Json::Value dataGroupHashValidation;
            dataGroupHashValidation["step"] = "DATA_GROUP_HASH_VALIDATION";
            dataGroupHashValidation["status"] = "SUCCESS";
            dataGroupHashValidation["message"] = "Data Group 해시 검증 완료";
            data["dataGroupHashValidation"] = dataGroupHashValidation;

            Json::Value crlCheck;
            crlCheck["step"] = "CRL_CHECK";
            crlCheck["status"] = "SUCCESS";
            crlCheck["message"] = "CRL 확인 완료 - 인증서 유효";
            data["crlCheck"] = crlCheck;

            result["data"] = data;

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            resp->setStatusCode(drogon::k200OK);
            callback(resp);
        },
        {drogon::Post}
    );

    // LDAP health check endpoint (for frontend Dashboard)
    app.registerHandler(
        "/api/ldap/health",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/ldap/health");
            auto result = checkLdap();
            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            if (result["status"].asString() != "UP") {
                resp->setStatusCode(drogon::k503ServiceUnavailable);
            }
            callback(resp);
        },
        {drogon::Get}
    );

    // PA history endpoint - returns PageResponse format
    app.registerHandler(
        "/api/pa/history",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/pa/history");

            // Get query parameters
            int page = 0;
            int size = 20;
            if (auto p = req->getParameter("page"); !p.empty()) {
                page = std::stoi(p);
            }
            if (auto s = req->getParameter("size"); !s.empty()) {
                size = std::stoi(s);
            }

            // Return PageResponse format matching frontend expectations
            Json::Value result;
            result["content"] = Json::Value(Json::arrayValue);
            result["page"] = page;
            result["size"] = size;
            result["totalElements"] = 0;
            result["totalPages"] = 0;
            result["first"] = true;
            result["last"] = true;

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // Root endpoint
    app.registerHandler(
        "/",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            Json::Value result;
            result["name"] = "ICAO Local PKD";
            result["description"] = "ICAO Local PKD Management and Passive Authentication System";
            result["version"] = "1.0.0";
            result["endpoints"]["health"] = "/api/health";
            result["endpoints"]["upload"] = "/api/upload";
            result["endpoints"]["pa"] = "/api/pa";
            result["endpoints"]["ldap"] = "/api/ldap";

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // API info endpoint
    app.registerHandler(
        "/api",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            Json::Value result;
            result["api"] = "ICAO Local PKD REST API";
            result["version"] = "v1";

            Json::Value endpoints(Json::arrayValue);

            Json::Value health;
            health["method"] = "GET";
            health["path"] = "/api/health";
            health["description"] = "Health check endpoint";
            endpoints.append(health);

            Json::Value healthDb;
            healthDb["method"] = "GET";
            healthDb["path"] = "/api/health/database";
            healthDb["description"] = "Database health check";
            endpoints.append(healthDb);

            Json::Value healthLdap;
            healthLdap["method"] = "GET";
            healthLdap["path"] = "/api/health/ldap";
            healthLdap["description"] = "LDAP health check";
            endpoints.append(healthLdap);

            Json::Value uploadLdif;
            uploadLdif["method"] = "POST";
            uploadLdif["path"] = "/api/upload/ldif";
            uploadLdif["description"] = "Upload LDIF file";
            endpoints.append(uploadLdif);

            Json::Value uploadMl;
            uploadMl["method"] = "POST";
            uploadMl["path"] = "/api/upload/masterlist";
            uploadMl["description"] = "Upload Master List file";
            endpoints.append(uploadMl);

            Json::Value uploadHistory;
            uploadHistory["method"] = "GET";
            uploadHistory["path"] = "/api/upload/history";
            uploadHistory["description"] = "Get upload history";
            endpoints.append(uploadHistory);

            Json::Value uploadStats;
            uploadStats["method"] = "GET";
            uploadStats["path"] = "/api/upload/statistics";
            uploadStats["description"] = "Get upload statistics";
            endpoints.append(uploadStats);

            Json::Value paVerify;
            paVerify["method"] = "POST";
            paVerify["path"] = "/api/pa/verify";
            paVerify["description"] = "Perform Passive Authentication";
            endpoints.append(paVerify);

            Json::Value paHistory;
            paHistory["method"] = "GET";
            paHistory["path"] = "/api/pa/history";
            paHistory["description"] = "Get PA verification history";
            endpoints.append(paHistory);

            Json::Value paStats;
            paStats["method"] = "GET";
            paStats["path"] = "/api/pa/statistics";
            paStats["description"] = "Get PA verification statistics";
            endpoints.append(paStats);

            result["endpoints"] = endpoints;

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // OpenAPI specification endpoint
    app.registerHandler(
        "/api/openapi.yaml",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/openapi.yaml");

            // OpenAPI 3.0 specification
            std::string openApiSpec = R"(openapi: 3.0.3
info:
  title: PKD Management Service API
  description: ICAO Local PKD Management Service - Certificate upload, validation, and PA verification
  version: 1.0.0
servers:
  - url: /
tags:
  - name: Health
    description: Health check endpoints
  - name: Upload
    description: Certificate upload operations
  - name: Validation
    description: Certificate validation
  - name: PA
    description: Passive Authentication
  - name: Progress
    description: Upload progress tracking
paths:
  /api/health:
    get:
      tags: [Health]
      summary: Application health check
      responses:
        '200':
          description: Service is healthy
  /api/health/database:
    get:
      tags: [Health]
      summary: Database health check
      responses:
        '200':
          description: Database status
  /api/health/ldap:
    get:
      tags: [Health]
      summary: LDAP health check
      responses:
        '200':
          description: LDAP status
  /api/upload/ldif:
    post:
      tags: [Upload]
      summary: Upload LDIF file
      requestBody:
        content:
          multipart/form-data:
            schema:
              type: object
              properties:
                file:
                  type: string
                  format: binary
      responses:
        '200':
          description: Upload successful
  /api/upload/masterlist:
    post:
      tags: [Upload]
      summary: Upload Master List file
      requestBody:
        content:
          multipart/form-data:
            schema:
              type: object
              properties:
                file:
                  type: string
                  format: binary
      responses:
        '200':
          description: Upload successful
  /api/upload/statistics:
    get:
      tags: [Upload]
      summary: Get upload statistics
      responses:
        '200':
          description: Statistics data
  /api/upload/history:
    get:
      tags: [Upload]
      summary: Get upload history
      parameters:
        - name: limit
          in: query
          schema:
            type: integer
        - name: offset
          in: query
          schema:
            type: integer
      responses:
        '200':
          description: Upload history
  /api/upload/countries:
    get:
      tags: [Upload]
      summary: Get country statistics
      responses:
        '200':
          description: Country stats
  /api/validation/revalidate:
    post:
      tags: [Validation]
      summary: Re-validate DSC trust chains
      responses:
        '200':
          description: Revalidation result
  /api/pa/verify:
    post:
      tags: [PA]
      summary: Verify Passive Authentication
      requestBody:
        content:
          application/json:
            schema:
              type: object
              properties:
                sod:
                  type: string
                dataGroups:
                  type: object
      responses:
        '200':
          description: Verification result
  /api/pa/statistics:
    get:
      tags: [PA]
      summary: Get PA statistics
      responses:
        '200':
          description: PA stats
  /api/pa/history:
    get:
      tags: [PA]
      summary: Get PA history
      responses:
        '200':
          description: PA history
  /api/progress/stream/{uploadId}:
    get:
      tags: [Progress]
      summary: SSE progress stream
      parameters:
        - name: uploadId
          in: path
          required: true
          schema:
            type: string
      responses:
        '200':
          description: SSE stream
  /api/progress/status/{uploadId}:
    get:
      tags: [Progress]
      summary: Get progress status
      parameters:
        - name: uploadId
          in: path
          required: true
          schema:
            type: string
      responses:
        '200':
          description: Progress status
)";

            auto resp = drogon::HttpResponse::newHttpResponse();
            resp->setBody(openApiSpec);
            resp->setContentTypeCode(drogon::CT_TEXT_PLAIN);
            resp->addHeader("Content-Type", "application/x-yaml");
            callback(resp);
        },
        {drogon::Get}
    );

    // Swagger UI redirect
    app.registerHandler(
        "/api/docs",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            auto resp = drogon::HttpResponse::newRedirectionResponse("/swagger-ui/index.html");
            callback(resp);
        },
        {drogon::Get}
    );

    spdlog::info("API routes registered");
}

} // anonymous namespace

/**
 * @brief Main entry point
 */
int main(int argc, char* argv[]) {
    // Print banner
    printBanner();

    // Initialize logging
    initializeLogging();

    // Load configuration from environment
    appConfig = AppConfig::fromEnvironment();

    spdlog::info("====== ICAO Local PKD v1.4.10 LDAP-UPLOAD-FIX ======");
    spdlog::info("Database: {}:{}/{}", appConfig.dbHost, appConfig.dbPort, appConfig.dbName);
    spdlog::info("LDAP: {}:{}", appConfig.ldapHost, appConfig.ldapPort);

    try {
        auto& app = drogon::app();

        // Server settings
        app.setLogPath("logs")
           .setLogLevel(trantor::Logger::kInfo)
           .addListener("0.0.0.0", appConfig.serverPort)
           .setThreadNum(appConfig.threadNum)
           .enableGzip(true)
           .setClientMaxBodySize(100 * 1024 * 1024)  // 100MB max upload
           .setUploadPath("./uploads")
           .setDocumentRoot("./static");

        // Enable CORS for React.js frontend
        app.registerPreSendingAdvice([](const drogon::HttpRequestPtr& req,
                                         const drogon::HttpResponsePtr& resp) {
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-User-Id");
        });

        // Handle OPTIONS requests for CORS preflight
        app.registerHandler(
            "/{path}",
            [](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& path) {
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k204NoContent);
                callback(resp);
            },
            {drogon::Options}
        );

        // Register routes
        registerRoutes();

        spdlog::info("Server starting on http://0.0.0.0:{}", appConfig.serverPort);
        spdlog::info("Press Ctrl+C to stop the server");

        // Run the server
        app.run();

    } catch (const std::exception& e) {
        spdlog::error("Application error: {}", e.what());
        return 1;
    }

    spdlog::info("Server stopped");
    return 0;
}
