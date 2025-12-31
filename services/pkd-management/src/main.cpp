/**
 * @file main.cpp
 * @brief PKD Management Service - ICAO Local PKD Management
 *
 * C++ REST API based ICAO Local PKD Management Service.
 * Handles file upload (LDIF/Master List), parsing, certificate validation,
 * and LDAP synchronization.
 *
 * @author SmartCore Inc.
 * @date 2025-12-30
 * @version 1.0.0
 */

#include <drogon/drogon.h>
#include <trantor/utils/Date.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/rotating_file_sink.h>

#include <iostream>
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

namespace {

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
  ____  _  ______    __  __                                                   _
 |  _ \| |/ /  _ \  |  \/  | __ _ _ __   __ _  __ _  ___ _ __ ___   ___ _ __ | |_
 | |_) | ' /| | | | | |\/| |/ _` | '_ \ / _` |/ _` |/ _ \ '_ ` _ \ / _ \ '_ \| __|
 |  __/| . \| |_| | | |  | | (_| | | | | (_| | (_| |  __/ | | | | |  __/ | | | |_
 |_|   |_|\_\____/  |_|  |_|\__,_|_| |_|\__,_|\__, |\___|_| |_| |_|\___|_| |_|\__|
                                               |___/
)" << std::endl;
    std::cout << "  PKD Management Service - ICAO Local PKD" << std::endl;
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
                              const std::string& fileHash) {
    std::string uploadId = generateUuid();

    std::string query = "INSERT INTO uploaded_file (id, file_name, original_file_name, file_hash, "
                       "file_size, file_format, status, upload_timestamp) VALUES ("
                       "'" + uploadId + "', "
                       + escapeSqlString(conn, fileName) + ", "
                       + escapeSqlString(conn, fileName) + ", "
                       "'" + fileHash + "', "
                       + std::to_string(fileSize) + ", "
                       "'" + format + "', "
                       "'PROCESSING', "
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

/**
 * @brief LDIF Entry structure
 */
struct LdifEntry {
    std::string dn;
    std::map<std::string, std::vector<std::string>> attributes;

    bool hasAttribute(const std::string& name) const {
        return attributes.find(name) != attributes.end();
    }

    std::string getFirstAttribute(const std::string& name) const {
        auto it = attributes.find(name);
        if (it != attributes.end() && !it->second.empty()) {
            return it->second[0];
        }
        return "";
    }
};

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
        if (!currentAttrName.empty() && currentAttrName != "dn") {
            currentEntry.attributes[currentAttrName].push_back(currentAttrValue);
        }
        currentAttrName.clear();
        currentAttrValue.clear();
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
            // Continuation line - append to current value
            // For DN, currentEntry.dn is set but we use currentAttrValue for continuation
            if (inContinuation) {
                if (currentAttrName == "dn") {
                    // DN continuation
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
            // Keep currentAttrName as "dn" for continuation handling
            // Don't clear currentAttrName here
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
    size_t toLen = 0;
    unsigned char* escaped = PQescapeByteaConn(conn, data.data(), data.size(), &toLen);
    if (!escaped) return "";
    std::string result(reinterpret_cast<char*>(escaped), toLen - 1);
    PQfreemem(escaped);
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
 * @brief Ensure the base ICAO PKD DIT structure exists in LDAP
 * Creates the hierarchy: dc=pkd -> dc=download -> dc=data/dc=nc-data
 * This must be called before creating country entries
 */
bool ensureBaseDitStructure(LDAP* ld) {
    // The base DN is: dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
    // First we need to create dc=pkd, then dc=download, dc=data, dc=nc-data under it

    std::string baseDn = appConfig.ldapBaseDn;  // dc=pkd,dc=ldap,dc=smartcoreinc,dc=com

    // 0. Create dc=pkd,dc=ldap,dc=smartcoreinc,dc=com (if not exists)
    LDAPMessage* result = nullptr;
    int rc = ldap_search_ext_s(ld, baseDn.c_str(), LDAP_SCOPE_BASE, "(objectClass=*)",
                                nullptr, 0, nullptr, nullptr, nullptr, 1, &result);
    if (result) {
        ldap_msgfree(result);
    }

    if (rc == LDAP_NO_SUCH_OBJECT) {
        // Create dc=pkd entry
        LDAPMod modObjectClass;
        modObjectClass.mod_op = LDAP_MOD_ADD;
        modObjectClass.mod_type = const_cast<char*>("objectClass");
        char* ocVals[] = {const_cast<char*>("dcObject"), const_cast<char*>("organization"), const_cast<char*>("top"), nullptr};
        modObjectClass.mod_values = ocVals;

        LDAPMod modDc;
        modDc.mod_op = LDAP_MOD_ADD;
        modDc.mod_type = const_cast<char*>("dc");
        char* dcVal[] = {const_cast<char*>("pkd"), nullptr};
        modDc.mod_values = dcVal;

        LDAPMod modO;
        modO.mod_op = LDAP_MOD_ADD;
        modO.mod_type = const_cast<char*>("o");
        char* oVal[] = {const_cast<char*>("pkd"), nullptr};
        modO.mod_values = oVal;

        LDAPMod* mods[] = {&modObjectClass, &modDc, &modO, nullptr};

        rc = ldap_add_ext_s(ld, baseDn.c_str(), mods, nullptr, nullptr);
        if (rc != LDAP_SUCCESS && rc != LDAP_ALREADY_EXISTS) {
            spdlog::error("Failed to create dc=pkd entry: {}", ldap_err2string(rc));
            return false;
        }
        spdlog::info("Created LDAP entry: {}", baseDn);
    }

    // 1. Create dc=download,dc=pkd,... (if not exists)
    std::string downloadDn = "dc=download," + baseDn;

    result = nullptr;
    rc = ldap_search_ext_s(ld, downloadDn.c_str(), LDAP_SCOPE_BASE, "(objectClass=*)",
                            nullptr, 0, nullptr, nullptr, nullptr, 1, &result);
    if (result) {
        ldap_msgfree(result);
    }

    if (rc == LDAP_NO_SUCH_OBJECT) {
        // Create dc=download entry
        LDAPMod modObjectClass;
        modObjectClass.mod_op = LDAP_MOD_ADD;
        modObjectClass.mod_type = const_cast<char*>("objectClass");
        char* ocVals[] = {const_cast<char*>("dcObject"), const_cast<char*>("organization"), const_cast<char*>("top"), nullptr};
        modObjectClass.mod_values = ocVals;

        LDAPMod modDc;
        modDc.mod_op = LDAP_MOD_ADD;
        modDc.mod_type = const_cast<char*>("dc");
        char* dcVal[] = {const_cast<char*>("download"), nullptr};
        modDc.mod_values = dcVal;

        LDAPMod modO;
        modO.mod_op = LDAP_MOD_ADD;
        modO.mod_type = const_cast<char*>("o");
        char* oVal[] = {const_cast<char*>("download"), nullptr};
        modO.mod_values = oVal;

        LDAPMod* mods[] = {&modObjectClass, &modDc, &modO, nullptr};

        rc = ldap_add_ext_s(ld, downloadDn.c_str(), mods, nullptr, nullptr);
        if (rc != LDAP_SUCCESS && rc != LDAP_ALREADY_EXISTS) {
            spdlog::error("Failed to create dc=download entry: {}", ldap_err2string(rc));
            return false;
        }
        spdlog::info("Created LDAP entry: {}", downloadDn);
    }

    // 2. Create dc=data,dc=download,... (for CSCA, DSC, CRL, ML)
    std::string dataDn = "dc=data," + downloadDn;

    result = nullptr;
    rc = ldap_search_ext_s(ld, dataDn.c_str(), LDAP_SCOPE_BASE, "(objectClass=*)",
                            nullptr, 0, nullptr, nullptr, nullptr, 1, &result);
    if (result) {
        ldap_msgfree(result);
    }

    if (rc == LDAP_NO_SUCH_OBJECT) {
        LDAPMod modObjectClass;
        modObjectClass.mod_op = LDAP_MOD_ADD;
        modObjectClass.mod_type = const_cast<char*>("objectClass");
        char* ocVals[] = {const_cast<char*>("dcObject"), const_cast<char*>("organization"), const_cast<char*>("top"), nullptr};
        modObjectClass.mod_values = ocVals;

        LDAPMod modDc;
        modDc.mod_op = LDAP_MOD_ADD;
        modDc.mod_type = const_cast<char*>("dc");
        char* dcVal[] = {const_cast<char*>("data"), nullptr};
        modDc.mod_values = dcVal;

        LDAPMod modO;
        modO.mod_op = LDAP_MOD_ADD;
        modO.mod_type = const_cast<char*>("o");
        char* oVal[] = {const_cast<char*>("data"), nullptr};
        modO.mod_values = oVal;

        LDAPMod* mods[] = {&modObjectClass, &modDc, &modO, nullptr};

        rc = ldap_add_ext_s(ld, dataDn.c_str(), mods, nullptr, nullptr);
        if (rc != LDAP_SUCCESS && rc != LDAP_ALREADY_EXISTS) {
            spdlog::error("Failed to create dc=data entry: {}", ldap_err2string(rc));
            return false;
        }
        spdlog::info("Created LDAP entry: {}", dataDn);
    }

    // 3. Create dc=nc-data,dc=download,... (for DSC_NC - non-conformant)
    std::string ncDataDn = "dc=nc-data," + downloadDn;

    result = nullptr;
    rc = ldap_search_ext_s(ld, ncDataDn.c_str(), LDAP_SCOPE_BASE, "(objectClass=*)",
                            nullptr, 0, nullptr, nullptr, nullptr, 1, &result);
    if (result) {
        ldap_msgfree(result);
    }

    if (rc == LDAP_NO_SUCH_OBJECT) {
        LDAPMod modObjectClass;
        modObjectClass.mod_op = LDAP_MOD_ADD;
        modObjectClass.mod_type = const_cast<char*>("objectClass");
        char* ocVals[] = {const_cast<char*>("dcObject"), const_cast<char*>("organization"), const_cast<char*>("top"), nullptr};
        modObjectClass.mod_values = ocVals;

        LDAPMod modDc;
        modDc.mod_op = LDAP_MOD_ADD;
        modDc.mod_type = const_cast<char*>("dc");
        char* dcVal[] = {const_cast<char*>("nc-data"), nullptr};
        modDc.mod_values = dcVal;

        LDAPMod modO;
        modO.mod_op = LDAP_MOD_ADD;
        modO.mod_type = const_cast<char*>("o");
        char* oVal[] = {const_cast<char*>("nc-data"), nullptr};
        modO.mod_values = oVal;

        LDAPMod* mods[] = {&modObjectClass, &modDc, &modO, nullptr};

        rc = ldap_add_ext_s(ld, ncDataDn.c_str(), mods, nullptr, nullptr);
        if (rc != LDAP_SUCCESS && rc != LDAP_ALREADY_EXISTS) {
            spdlog::error("Failed to create dc=nc-data entry: {}", ldap_err2string(rc));
            return false;
        }
        spdlog::info("Created LDAP entry: {}", ncDataDn);
    }

    spdlog::debug("Base LDAP DIT structure verified");
    return true;
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
 * @param ouType: "data" (default), "nc-data", or specific OU name like "ml", "csca", "dsc", "crl"
 */
bool ensureCountryOuExists(LDAP* ld, const std::string& countryCode, const std::string& ouType = "data") {
    if (countryCode.empty()) {
        spdlog::warn("Cannot create country OU: empty country code");
        return false;
    }

    // Determine data container and OU to create
    std::string dataContainer = (ouType == "nc-data") ? "dc=nc-data" : "dc=data";
    std::string countryDn = "c=" + countryCode + "," + dataContainer + ",dc=download," + appConfig.ldapBaseDn;

    // Check if country entry exists
    LDAPMessage* result = nullptr;
    int rc = ldap_search_ext_s(ld, countryDn.c_str(), LDAP_SCOPE_BASE, "(objectClass=*)",
                                nullptr, 0, nullptr, nullptr, nullptr, 1, &result);

    if (result) {
        ldap_msgfree(result);
    }

    if (rc == LDAP_SUCCESS) {
        // Country exists, check if we need to create specific OU
    } else if (rc == LDAP_NO_SUCH_OBJECT) {
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
        spdlog::debug("Created country entry: {}", countryDn);
    } else {
        spdlog::warn("LDAP search for country {} failed: {}", countryCode, ldap_err2string(rc));
        return false;
    }

    // Determine which OUs to create
    std::vector<std::string> ous;
    if (ouType == "nc-data") {
        ous = {"dsc"};
    } else if (ouType == "ml" || ouType == "csca" || ouType == "dsc" || ouType == "crl") {
        // Create only the specific OU requested
        ous = {ouType};
    } else {
        // Default: create all standard OUs
        ous = {"csca", "dsc", "crl", "ml"};
    }

    for (const auto& ouName : ous) {
        std::string ouDn = "o=" + ouName + "," + countryDn;

        // Check if OU exists
        result = nullptr;
        rc = ldap_search_ext_s(ld, ouDn.c_str(), LDAP_SCOPE_BASE, "(objectClass=*)",
                                nullptr, 0, nullptr, nullptr, nullptr, 1, &result);
        if (result) {
            ldap_msgfree(result);
        }
        if (rc == LDAP_SUCCESS) {
            continue;  // OU already exists
        }

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
        if (rc == LDAP_SUCCESS) {
            spdlog::debug("Created OU: {}", ouDn);
        } else if (rc != LDAP_ALREADY_EXISTS) {
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
    std::string ouType = isNcData ? "nc-data" : "data";

    // Ensure country structure exists
    if (!ensureCountryOuExists(ld, countryCode, ouType)) {
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
    if (!ensureCountryOuExists(ld, countryCode, "crl")) {
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
                       "E'" + byteaEscaped + "', "
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
                       "E'" + byteaEscaped + "', "
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
 * @brief Parse and save certificate from LDIF entry (DB + LDAP)
 */
bool parseCertificateEntry(PGconn* conn, LDAP* ld, const std::string& uploadId,
                           const LdifEntry& entry, const std::string& attrName,
                           int& cscaCount, int& dscCount, int& ldapStoredCount) {
    std::string base64Value = entry.getFirstAttribute(attrName);
    if (base64Value.empty()) return false;

    std::vector<uint8_t> derBytes = base64Decode(base64Value);
    if (derBytes.empty()) return false;

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

    // Determine certificate type
    std::string certType;
    if (subjectDn == issuerDn) {
        certType = "CSCA";
        cscaCount++;
    } else if (entry.dn.find("nc-data") != std::string::npos) {
        certType = "DSC_NC";
        dscCount++;
    } else {
        certType = "DSC";
        dscCount++;
    }

    X509_free(cert);

    // 1. Save to DB
    std::string certId = saveCertificate(conn, uploadId, certType, countryCode,
                                         subjectDn, issuerDn, serialNumber, fingerprint,
                                         notBefore, notAfter, derBytes);

    if (!certId.empty()) {
        spdlog::debug("Saved certificate to DB: type={}, country={}, fingerprint={}",
                     certType, countryCode, fingerprint.substr(0, 16));

        // 2. Save to LDAP
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

// =============================================================================
// Master List (pkdMasterListContent) Functions
// =============================================================================

/**
 * @brief Escape special characters in DN for LDAP RDN usage
 * Characters: , = + < > # ; " \
 */
std::string escapeDnForRdn(const std::string& dn) {
    std::string result;
    result.reserve(dn.size() * 2);
    for (char c : dn) {
        switch (c) {
            case ',': result += "\\,"; break;
            case '=': result += "\\="; break;
            case '+': result += "\\+"; break;
            case '<': result += "\\<"; break;
            case '>': result += "\\>"; break;
            case '#': result += "\\#"; break;
            case ';': result += "\\;"; break;
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            default: result += c; break;
        }
    }
    return result;
}

/**
 * @brief Extract CSCA DN from the LDIF entry's cn attribute or DN
 * The cn attribute contains the CSCA's subject DN (e.g., CN=CSCA-FRANCE,O=Gouv,C=FR)
 */
std::string extractCscaDnFromEntry(const LdifEntry& entry) {
    // First try cn attribute
    if (entry.hasAttribute("cn")) {
        return entry.getFirstAttribute("cn");
    }

    // Extract from DN: cn=CN\=CSCA-FRANCE\,O\=Gouv\,C\=FR,o=ml,c=FR,...
    // The cn component contains the escaped CSCA DN
    std::string dn = entry.dn;
    if (dn.substr(0, 3) == "cn=") {
        size_t endPos = 0;
        bool inEscape = false;
        for (size_t i = 3; i < dn.size(); i++) {
            if (inEscape) {
                inEscape = false;
                continue;
            }
            if (dn[i] == '\\') {
                inEscape = true;
                continue;
            }
            if (dn[i] == ',') {
                endPos = i;
                break;
            }
        }
        if (endPos > 3) {
            std::string escapedCn = dn.substr(3, endPos - 3);
            // Unescape the DN
            std::string result;
            for (size_t i = 0; i < escapedCn.size(); i++) {
                if (escapedCn[i] == '\\' && i + 1 < escapedCn.size()) {
                    result += escapedCn[i + 1];
                    i++;
                } else {
                    result += escapedCn[i];
                }
            }
            return result;
        }
    }
    return "";
}

/**
 * @brief Save Master List to database
 * @return master_list ID or empty string on failure
 */
std::string saveMasterList(PGconn* conn, const std::string& uploadId,
                           const std::string& countryCode, const std::string& cscaDn,
                           int version, int cscaCount, const std::string& fingerprint,
                           const std::vector<uint8_t>& cmsBinary) {
    std::string mlId = generateUuid();
    std::string byteaEscaped = escapeBytea(conn, cmsBinary);

    std::string query = "INSERT INTO master_list (id, upload_id, signer_country, "
                       "signer_dn, version, csca_certificate_count, fingerprint_sha256, "
                       "ml_binary, created_at) VALUES ("
                       "'" + mlId + "', "
                       "'" + uploadId + "', "
                       + escapeSqlString(conn, countryCode) + ", "
                       + escapeSqlString(conn, cscaDn) + ", "
                       + std::to_string(version) + ", "
                       + std::to_string(cscaCount) + ", "
                       "'" + fingerprint + "', "
                       "E'" + byteaEscaped + "', NOW()) "
                       "ON CONFLICT DO NOTHING";

    PGresult* res = PQexec(conn, query.c_str());
    bool success = (PQresultStatus(res) == PGRES_COMMAND_OK);
    PQclear(res);

    return success ? mlId : "";
}

/**
 * @brief Save Master List to LDAP (o=ml node)
 * DN format: cn={ESCAPED-CSCA-DN},o=ml,c={COUNTRY},dc=data,dc=download,dc=pkd,...
 */
std::string saveMasterListToLdap(LDAP* ld, const std::string& countryCode,
                                  const std::string& cscaDn, int version,
                                  const std::string& fingerprint,
                                  const std::vector<uint8_t>& cmsBinary) {
    if (!ld) return "";

    // Build LDAP DN
    std::string escapedCscaDn = escapeDnForRdn(cscaDn);
    std::string baseDn = appConfig.ldapBaseDn;
    std::string dn = "cn=" + escapedCscaDn + ",o=ml,c=" + countryCode +
                     ",dc=data,dc=download," + baseDn;

    // Ensure country/ml OU exists
    ensureCountryOuExists(ld, countryCode, "ml");

    // Check if entry exists
    bool exists = false;
    LDAPMessage* searchResult = nullptr;
    std::string filter = "(objectClass=pkdMasterList)";
    int rc = ldap_search_ext_s(ld, dn.c_str(), LDAP_SCOPE_BASE, filter.c_str(),
                                nullptr, 0, nullptr, nullptr, nullptr, 1, &searchResult);
    if (rc == LDAP_SUCCESS && ldap_count_entries(ld, searchResult) > 0) {
        exists = true;
    }
    if (searchResult) ldap_msgfree(searchResult);

    // Prepare attribute values
    std::string versionStr = std::to_string(version);
    std::string snStr = fingerprint.substr(0, 16); // Use first 16 chars of fingerprint as serial

    // Binary data for pkdMasterListContent
    struct berval bv;
    bv.bv_val = const_cast<char*>(reinterpret_cast<const char*>(cmsBinary.data()));
    bv.bv_len = cmsBinary.size();
    struct berval* bvals[] = {&bv, nullptr};

    if (exists) {
        // Modify existing entry
        LDAPMod modPkdContent;
        modPkdContent.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
        modPkdContent.mod_type = const_cast<char*>("pkdMasterListContent");
        modPkdContent.mod_bvalues = bvals;

        char* versionVals[] = {const_cast<char*>(versionStr.c_str()), nullptr};
        LDAPMod modVersion;
        modVersion.mod_op = LDAP_MOD_REPLACE;
        modVersion.mod_type = const_cast<char*>("pkdVersion");
        modVersion.mod_vals.modv_strvals = versionVals;

        LDAPMod* mods[] = {&modPkdContent, &modVersion, nullptr};

        rc = ldap_modify_ext_s(ld, dn.c_str(), mods, nullptr, nullptr);
        if (rc != LDAP_SUCCESS) {
            spdlog::warn("Failed to modify Master List in LDAP: {} ({})", ldap_err2string(rc), dn);
            return "";
        }
        spdlog::debug("Modified Master List in LDAP: {}", dn);
    } else {
        // Add new entry
        char* objectClasses[] = {
            const_cast<char*>("top"),
            const_cast<char*>("person"),
            const_cast<char*>("pkdMasterList"),
            const_cast<char*>("pkdDownload"),
            nullptr
        };
        char* cnVals[] = {const_cast<char*>(cscaDn.c_str()), nullptr};
        char* snVals[] = {const_cast<char*>(snStr.c_str()), nullptr};
        char* versionVals[] = {const_cast<char*>(versionStr.c_str()), nullptr};

        LDAPMod modOc, modCn, modSn, modVersion, modPkdContent;

        modOc.mod_op = LDAP_MOD_ADD;
        modOc.mod_type = const_cast<char*>("objectClass");
        modOc.mod_vals.modv_strvals = objectClasses;

        modCn.mod_op = LDAP_MOD_ADD;
        modCn.mod_type = const_cast<char*>("cn");
        modCn.mod_vals.modv_strvals = cnVals;

        modSn.mod_op = LDAP_MOD_ADD;
        modSn.mod_type = const_cast<char*>("sn");
        modSn.mod_vals.modv_strvals = snVals;

        modVersion.mod_op = LDAP_MOD_ADD;
        modVersion.mod_type = const_cast<char*>("pkdVersion");
        modVersion.mod_vals.modv_strvals = versionVals;

        modPkdContent.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
        modPkdContent.mod_type = const_cast<char*>("pkdMasterListContent");
        modPkdContent.mod_bvalues = bvals;

        LDAPMod* mods[] = {&modOc, &modCn, &modSn, &modVersion, &modPkdContent, nullptr};

        rc = ldap_add_ext_s(ld, dn.c_str(), mods, nullptr, nullptr);
        if (rc != LDAP_SUCCESS) {
            spdlog::warn("Failed to add Master List to LDAP: {} ({})", ldap_err2string(rc), dn);
            return "";
        }
        spdlog::debug("Added Master List to LDAP: {}", dn);
    }

    return dn;
}

/**
 * @brief Update Master List DB record with LDAP DN
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

/**
 * @brief Parse and save Master List from LDIF entry (DB + LDAP)
 * Handles pkdMasterListContent attribute which contains CMS SignedData with CSCA certificates
 */
bool parseMasterListEntry(PGconn* conn, LDAP* ld, const std::string& uploadId,
                          const LdifEntry& entry, int& mlCount, int& ldapMlStoredCount) {
    // Check for pkdMasterListContent;binary (base64 encoded CMS SignedData)
    std::string base64Value = entry.getFirstAttribute("pkdMasterListContent;binary");
    if (base64Value.empty()) {
        // Also check without ;binary suffix
        base64Value = entry.getFirstAttribute("pkdMasterListContent");
        if (base64Value.empty()) return false;
    }

    std::vector<uint8_t> cmsBinary = base64Decode(base64Value);
    if (cmsBinary.empty()) {
        spdlog::warn("Failed to decode Master List content from entry: {}", entry.dn);
        return false;
    }

    // Validate CMS format (must start with 0x30 SEQUENCE tag)
    if (cmsBinary[0] != 0x30) {
        spdlog::warn("Invalid Master List CMS format in entry: {}", entry.dn);
        return false;
    }

    // Extract CSCA DN from entry first (needed for country code extraction)
    std::string cscaDn = extractCscaDnFromEntry(entry);
    if (cscaDn.empty()) {
        cscaDn = "UNKNOWN";
    }

    // Extract country code - try multiple sources
    std::string countryCode = "";

    // 1. Try from LDIF entry DN (e.g., ,c=FR,dc=data,...)
    size_t cPos = entry.dn.find(",c=");
    if (cPos != std::string::npos) {
        size_t cEnd = entry.dn.find(',', cPos + 3);
        if (cEnd == std::string::npos) cEnd = entry.dn.size();
        countryCode = entry.dn.substr(cPos + 3, cEnd - cPos - 3);
    }

    // 2. If empty or XX, try from CSCA DN (e.g., C=FR at the end)
    if (countryCode.empty() || countryCode == "XX") {
        // Look for C= or ,C= in CSCA DN
        std::string upperCscaDn = cscaDn;
        std::transform(upperCscaDn.begin(), upperCscaDn.end(), upperCscaDn.begin(), ::toupper);

        size_t cPosInCsca = upperCscaDn.rfind(",C=");
        if (cPosInCsca == std::string::npos) {
            cPosInCsca = upperCscaDn.find("C=");
        } else {
            cPosInCsca += 1; // Skip the comma
        }

        if (cPosInCsca != std::string::npos) {
            size_t valueStart = cPosInCsca + 2;
            size_t valueEnd = cscaDn.find(',', valueStart);
            if (valueEnd == std::string::npos) valueEnd = cscaDn.size();
            countryCode = cscaDn.substr(valueStart, valueEnd - valueStart);
        }
    }

    // Convert to uppercase and validate
    std::transform(countryCode.begin(), countryCode.end(), countryCode.begin(), ::toupper);
    if (countryCode.empty() || countryCode.length() != 2) {
        countryCode = "XX";
        spdlog::warn("Could not extract valid country code from entry: {}", entry.dn.substr(0, 80));
    }

    // Get version from pkdVersion attribute
    int version = 0;
    if (entry.hasAttribute("pkdVersion")) {
        try {
            version = std::stoi(entry.getFirstAttribute("pkdVersion"));
        } catch (...) {}
    }

    // Compute fingerprint
    std::string fingerprint = computeFileHash(cmsBinary);

    // Parse CMS to count CSCA certificates (optional - for statistics)
    int cscaCount = 0;
    BIO* bio = BIO_new_mem_buf(cmsBinary.data(), static_cast<int>(cmsBinary.size()));
    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (cms) {
        // Try to extract encapsulated content and count certificates
        ASN1_OCTET_STRING** contentPtr = CMS_get0_content(cms);
        if (contentPtr && *contentPtr) {
            const unsigned char* contentData = (*contentPtr)->data;
            int contentLen = (*contentPtr)->length;

            // Parse MasterList structure: SEQUENCE { version? INTEGER, certList SET OF Certificate }
            const unsigned char* p = contentData;
            long outerLen;
            int outerTag, outerClass;
            ASN1_get_object(&p, &outerLen, &outerTag, &outerClass, contentLen);

            if (outerTag == V_ASN1_SEQUENCE) {
                // Check if first element is INTEGER (version) or SET (certificates)
                const unsigned char* inner = p;
                long innerLen;
                int innerTag, innerClass;
                ASN1_get_object(&inner, &innerLen, &innerTag, &innerClass, outerLen);

                if (innerTag == V_ASN1_INTEGER) {
                    // Skip version, move to SET
                    p = inner + innerLen;
                    ASN1_get_object(&p, &innerLen, &innerTag, &innerClass, outerLen - (p - contentData));
                } else {
                    p = inner - 1; // Rewind to re-parse SET
                    ASN1_get_object(&p, &innerLen, &innerTag, &innerClass, outerLen);
                }

                if (innerTag == V_ASN1_SET) {
                    // Count certificates in SET
                    const unsigned char* certP = p;
                    long remaining = innerLen;
                    while (remaining > 0) {
                        const unsigned char* certStart = certP;
                        X509* cert = d2i_X509(nullptr, &certP, remaining);
                        if (cert) {
                            cscaCount++;
                            X509_free(cert);
                            remaining -= (certP - certStart);
                        } else {
                            break;
                        }
                    }
                }
            }
        }
        CMS_ContentInfo_free(cms);
    }

    spdlog::info("Parsed Master List: country={}, csca_dn={}, version={}, certs={}, fingerprint={}",
                countryCode, cscaDn.substr(0, 30), version, cscaCount, fingerprint.substr(0, 16));

    // 1. Save to DB
    std::string mlId = saveMasterList(conn, uploadId, countryCode, cscaDn, version,
                                       cscaCount, fingerprint, cmsBinary);

    if (!mlId.empty()) {
        mlCount++;
        spdlog::debug("Saved Master List to DB: country={}, fingerprint={}",
                     countryCode, fingerprint.substr(0, 16));

        // 2. Save to LDAP
        if (ld) {
            std::string ldapDn = saveMasterListToLdap(ld, countryCode, cscaDn, version,
                                                       fingerprint, cmsBinary);
            if (!ldapDn.empty()) {
                updateMasterListLdapStatus(conn, mlId, ldapDn);
                ldapMlStoredCount++;
                spdlog::debug("Saved Master List to LDAP: {}", ldapDn);
            }
        }
    }

    return !mlId.empty();
}

/**
 * @brief Update uploaded_file with parsing statistics (including ML count)
 */
void updateUploadStatistics(PGconn* conn, const std::string& uploadId,
                           const std::string& status, int cscaCount, int dscCount,
                           int crlCount, int mlCount, int totalEntries, int processedEntries,
                           const std::string& errorMessage) {
    std::string query = "UPDATE uploaded_file SET "
                       "status = '" + status + "', "
                       "csca_count = " + std::to_string(cscaCount) + ", "
                       "dsc_count = " + std::to_string(dscCount) + ", "
                       "crl_count = " + std::to_string(crlCount) + ", "
                       "ml_count = " + std::to_string(mlCount) + ", "
                       "total_entries = " + std::to_string(totalEntries) + ", "
                       "processed_entries = " + std::to_string(processedEntries) + ", "
                       "error_message = " + escapeSqlString(conn, errorMessage) + ", "
                       "completed_timestamp = NOW() "
                       "WHERE id = '" + uploadId + "'";

    PGresult* res = PQexec(conn, query.c_str());
    PQclear(res);
}

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

        // Connect to LDAP (write connection - direct to primary master)
        LDAP* ld = getLdapWriteConnection();
        if (!ld) {
            spdlog::warn("LDAP write connection failed - will only save to DB");
        } else {
            spdlog::info("LDAP write connection established for upload {}", uploadId);
            // Ensure base DIT structure exists before processing entries
            if (!ensureBaseDitStructure(ld)) {
                spdlog::warn("Failed to ensure base DIT structure - LDAP storage may fail");
            }
        }

        try {
            // Send initial parsing progress
            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::PARSING_STARTED, 0, 0, "LDIF 파싱 시작"));

            std::string contentStr(content.begin(), content.end());

            // Parse LDIF content
            std::vector<LdifEntry> entries = parseLdifContent(contentStr);
            int totalEntries = static_cast<int>(entries.size());

            spdlog::info("Parsed {} LDIF entries for upload {}", totalEntries, uploadId);

            // Send parsing completed progress
            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::PARSING_COMPLETED, 0, totalEntries,
                    "LDIF 파싱 완료: " + std::to_string(totalEntries) + "개 엔트리"));

            int cscaCount = 0;
            int dscCount = 0;
            int crlCount = 0;
            int mlCount = 0;
            int processedEntries = 0;
            int ldapCertStoredCount = 0;
            int ldapCrlStoredCount = 0;
            int ldapMlStoredCount = 0;

            // Process each entry
            for (const auto& entry : entries) {
                try {
                    // Determine entry type based on DN pattern and attributes
                    bool isMasterListEntry = (entry.dn.find(",o=ml,") != std::string::npos) ||
                                             entry.hasAttribute("pkdMasterListContent;binary") ||
                                             entry.hasAttribute("pkdMasterListContent");

                    if (isMasterListEntry) {
                        // Process Master List entry (pkdMasterListContent)
                        parseMasterListEntry(conn, ld, uploadId, entry, mlCount, ldapMlStoredCount);
                    }
                    // Check for userCertificate;binary (DSC certificates)
                    else if (entry.hasAttribute("userCertificate;binary")) {
                        parseCertificateEntry(conn, ld, uploadId, entry, "userCertificate;binary",
                                             cscaCount, dscCount, ldapCertStoredCount);
                    }
                    // Check for cACertificate;binary (CSCA certificates)
                    else if (entry.hasAttribute("cACertificate;binary")) {
                        parseCertificateEntry(conn, ld, uploadId, entry, "cACertificate;binary",
                                             cscaCount, dscCount, ldapCertStoredCount);
                    }

                    // Check for CRL (can be in same entry or separate)
                    if (entry.hasAttribute("certificateRevocationList;binary")) {
                        parseCrlEntry(conn, ld, uploadId, entry, crlCount, ldapCrlStoredCount);
                    }

                } catch (const std::exception& e) {
                    spdlog::warn("Error processing entry {}: {}", entry.dn, e.what());
                }

                processedEntries++;

                // Send progress every 50 entries or at the end
                if (processedEntries % 50 == 0 || processedEntries == totalEntries) {
                    int percentage = totalEntries > 0 ? (processedEntries * 100 / totalEntries) : 100;
                    std::string progressMsg = "처리 중: " + std::to_string(processedEntries) + "/" +
                                             std::to_string(totalEntries) + " 엔트리 (" +
                                             std::to_string(cscaCount) + " CSCA, " +
                                             std::to_string(dscCount) + " DSC, " +
                                             std::to_string(crlCount) + " CRL)";
                    ProgressManager::getInstance().sendProgress(
                        ProcessingProgress::create(uploadId, ProcessingStage::DB_SAVING_IN_PROGRESS,
                            processedEntries, totalEntries, progressMsg));

                    // Log progress every 100 entries
                    if (processedEntries % 100 == 0) {
                        spdlog::info("Processing progress: {}/{} entries, {} certs ({} LDAP), {} CRLs ({} LDAP), {} MLs ({} LDAP)",
                                    processedEntries, totalEntries,
                                    cscaCount + dscCount, ldapCertStoredCount,
                                    crlCount, ldapCrlStoredCount,
                                    mlCount, ldapMlStoredCount);
                    }
                }
            }

            // Update final statistics
            updateUploadStatistics(conn, uploadId, "COMPLETED", cscaCount, dscCount, crlCount, mlCount,
                                  totalEntries, processedEntries, "");

            // Send completion progress
            std::string completionMsg = "처리 완료: " + std::to_string(cscaCount) + " CSCA, " +
                                       std::to_string(dscCount) + " DSC, " +
                                       std::to_string(crlCount) + " CRL, " +
                                       std::to_string(mlCount) + " ML (LDAP: " +
                                       std::to_string(ldapCertStoredCount) + " 인증서, " +
                                       std::to_string(ldapCrlStoredCount) + " CRL)";
            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::COMPLETED,
                    processedEntries, totalEntries, completionMsg));

            spdlog::info("LDIF processing completed for upload {}: {} CSCA, {} DSC, {} CRLs, {} MLs (LDAP: {} certs, {} CRLs, {} MLs)",
                        uploadId, cscaCount, dscCount, crlCount, mlCount,
                        ldapCertStoredCount, ldapCrlStoredCount, ldapMlStoredCount);

        } catch (const std::exception& e) {
            spdlog::error("LDIF processing failed for upload {}: {}", uploadId, e.what());
            // Send failure progress
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

        // Connect to LDAP (write connection - direct to primary master)
        LDAP* ld = getLdapWriteConnection();
        if (!ld) {
            spdlog::warn("LDAP write connection failed - will only save to DB");
        } else {
            spdlog::info("LDAP write connection established for Master List upload {}", uploadId);
            // Ensure base DIT structure exists before processing entries
            if (!ensureBaseDitStructure(ld)) {
                spdlog::warn("Failed to ensure base DIT structure - LDAP storage may fail");
            }
        }

        try {
            int cscaCount = 0;
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

                            // Master List only contains CSCA certificates
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

                                        // Master List contains ONLY CSCA certificates
                                        // This includes: self-signed CSCAs and CSCA Link certificates (key rollover)
                                        std::string certType = "CSCA";  // All certs in Master List are CSCAs
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
                                            // CSCA Link certificate (issued by previous CSCA for key rollover)
                                            validationStatus = "VALID";
                                            validationMessage = "CSCA Link certificate (key rollover)";
                                            spdlog::debug("CSCA Link certificate: {} issued by {}", subjectDn.substr(0, 40), issuerDn.substr(0, 40));
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
                                                    cscaCount, totalCerts, "CSCA 인증서 저장 중: " + std::to_string(cscaCount) + "개"));
                                        }

                                        // 1. Save to DB with validation status
                                        std::string certId = saveCertificate(conn, uploadId, certType, countryCode,
                                                                             subjectDn, issuerDn, serialNumber, fingerprint,
                                                                             notBefore, notAfter, derBytes,
                                                                             validationStatus, validationMessage);

                                        if (!certId.empty()) {
                                            cscaCount++;  // All certs in Master List are CSCAs

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

                            spdlog::info("Extracted {} CSCA certificates from Master List encapsulated content", cscaCount);
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

                            // Master List only contains CSCA certificates
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

            // Update statistics (Master List only contains CSCA certificates)
            updateUploadStatistics(conn, uploadId, "COMPLETED", cscaCount, 0, 0, 0, totalCerts, cscaCount, "");

            // Send completion progress
            std::string completionMsg = "처리 완료: CSCA " + std::to_string(cscaCount) +
                                       "개 (중복 " + std::to_string(skippedDuplicates) + "개 건너뜀)";
            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::COMPLETED,
                    cscaCount, totalCerts, completionMsg));

            spdlog::info("Master List processing completed for upload {}: {} CSCA certificates (LDAP: {}, duplicates skipped: {})",
                        uploadId, cscaCount, ldapStoredCount, skippedDuplicates);

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

/**
 * @brief Register API controllers and routes
 */
void registerRoutes() {
    auto& app = drogon::app();

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

                // Save upload record
                std::string uploadId = saveUploadRecord(conn, fileName, fileSize, "LDIF", fileHash);
                PQfinish(conn);

                // Start async processing
                processLdifFileAsync(uploadId, contentBytes);

                // Return success response
                Json::Value result;
                result["success"] = true;
                result["message"] = "LDIF file uploaded successfully. Processing started.";

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

                // Save upload record
                std::string uploadId = saveUploadRecord(conn, fileName, fileSize, "ML", fileHash);
                PQfinish(conn);

                // Start async processing
                processMasterListFileAsync(uploadId, contentBytes);

                // Return success response
                Json::Value result;
                result["success"] = true;
                result["message"] = "Master List file uploaded successfully. Processing started.";

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
                res = PQexec(conn, "SELECT COALESCE(SUM(csca_count + dsc_count), 0) FROM uploaded_file");
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

                // Get DSC count from certificate table
                res = PQexec(conn, "SELECT COUNT(*) FROM certificate WHERE certificate_type IN ('DSC', 'DSC_NC')");
                result["dscCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

                // Get distinct country count from certificate table
                res = PQexec(conn, "SELECT COUNT(DISTINCT country_code) FROM certificate");
                result["countriesCount"] = (PQntuples(res) > 0) ? std::stoi(PQgetvalue(res, 0, 0)) : 0;
                PQclear(res);

            } else {
                result["totalUploads"] = 0;
                result["successfulUploads"] = 0;
                result["failedUploads"] = 0;
                result["totalCertificates"] = 0;
                result["cscaCount"] = 0;
                result["dscCount"] = 0;
                result["crlCount"] = 0;
                result["countriesCount"] = 0;
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

                // Get paginated results
                int offset = page * size;
                std::string query = "SELECT id, file_name, file_format, file_size, status, "
                                   "(csca_count + dsc_count) as certificate_count, crl_count, error_message, upload_timestamp, completed_timestamp "
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
                        item["certificateCount"] = std::stoi(PQgetvalue(res, i, 5));
                        item["crlCount"] = std::stoi(PQgetvalue(res, i, 6));
                        item["errorMessage"] = PQgetvalue(res, i, 7) ? PQgetvalue(res, i, 7) : "";
                        item["createdAt"] = PQgetvalue(res, i, 8);
                        item["updatedAt"] = PQgetvalue(res, i, 9);
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

            resp->setContentTypeString("text/event-stream");
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

    // Root endpoint
    app.registerHandler(
        "/",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            Json::Value result;
            result["name"] = "PKD Management Service";
            result["description"] = "ICAO Local PKD Management Service - File Upload, Parsing, and Certificate Validation";
            result["version"] = "1.0.0";
            result["endpoints"]["health"] = "/api/health";
            result["endpoints"]["upload"] = "/api/upload";
            result["endpoints"]["ldap"] = "/api/ldap";
            result["endpoints"]["certificates"] = "/api/certificates";

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
            result["api"] = "PKD Management Service REST API";
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

            Json::Value progressStream;
            progressStream["method"] = "GET";
            progressStream["path"] = "/api/progress/stream/{uploadId}";
            progressStream["description"] = "SSE progress stream for upload";
            endpoints.append(progressStream);

            result["endpoints"] = endpoints;

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
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

    spdlog::info("Starting PKD Management Service...");
    spdlog::info("Database: {}:{}/{}", appConfig.dbHost, appConfig.dbPort, appConfig.dbName);
    spdlog::info("LDAP Read: {}:{}", appConfig.ldapHost, appConfig.ldapPort);
    spdlog::info("LDAP Write: {}:{}", appConfig.ldapWriteHost, appConfig.ldapWritePort);

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
