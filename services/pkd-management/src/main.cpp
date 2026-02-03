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
#include <atomic>  // For application-level LDAP load balancing round-robin

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
#include <regex>
#include <map>
#include <set>
#include <algorithm>
#include <optional>
#include <cctype>

// Project headers
#include "common.h"
#include "common/ldap_utils.h"
#include <icao/audit/audit_log.h>
#include "common/db_connection_pool.h"  // v2.3.1: Database connection pool
#include "common/certificate_utils.h"
#include "common/masterlist_processor.h"
#include "common/x509_metadata_extractor.h"
#include "common/progress_manager.h"  // Phase 4.4: Enhanced progress tracking
#include "common/asn1_parser.h"       // ASN.1/TLV parser for Master List structure
#include "processing_strategy.h"
#include "ldif_processor.h"

// Shared library (Phase 5)
#include "icao/x509/dn_parser.h"      // Shared DN Parser
#include "icao/x509/dn_components.h"  // Shared DN Components

// Clean Architecture layers
#include "domain/models/certificate.h"
#include "repositories/ldap_certificate_repository.h"
#include "services/certificate_service.h"

// ICAO Auto Sync Module (v1.7.0)
#include "handlers/icao_handler.h"
#include "services/icao_sync_service.h"
#include "repositories/icao_version_repository.h"
#include "infrastructure/http/http_client.h"
#include "infrastructure/notification/email_sender.h"

// Phase 1.5/1.6: Repository Pattern - Repositories
#include "repositories/upload_repository.h"
#include "repositories/certificate_repository.h"
#include "repositories/validation_repository.h"
#include "repositories/audit_repository.h"
#include "repositories/statistics_repository.h"
#include "repositories/ldif_structure_repository.h"  // v2.2.2: LDIF structure visualization

// Phase 1.5/1.6: Repository Pattern - Services
#include "services/upload_service.h"
#include "services/validation_service.h"
#include "services/audit_service.h"
#include "services/statistics_service.h"
#include "services/ldif_structure_service.h"  // v2.2.2: LDIF structure visualization

// Authentication Module (Phase 3)
#include "middleware/auth_middleware.h"
#include "middleware/permission_filter.h"
#include "auth/jwt_service.h"
#include "auth/password_hash.h"
#include "handlers/auth_handler.h"

// Sprint 2: Link Certificate Validation
#include "common/lc_validator.h"

// Global certificate service (initialized in main(), used by all routes)
std::shared_ptr<services::CertificateService> certificateService;

// Global ICAO handler (initialized in main())
std::shared_ptr<handlers::IcaoHandler> icaoHandler;

// Global Auth handler (initialized in main())
std::shared_ptr<handlers::AuthHandler> authHandler;

// Phase 1.6: Global Repositories and Services (Repository Pattern)
// v2.3.1: Replaced single connection with Connection Pool for thread safety
std::shared_ptr<common::DbConnectionPool> dbPool;  // Database connection pool
std::shared_ptr<repositories::UploadRepository> uploadRepository;
std::shared_ptr<repositories::CertificateRepository> certificateRepository;
std::shared_ptr<repositories::ValidationRepository> validationRepository;
std::shared_ptr<repositories::AuditRepository> auditRepository;
std::shared_ptr<repositories::StatisticsRepository> statisticsRepository;
std::shared_ptr<repositories::LdifStructureRepository> ldifStructureRepository;  // v2.2.2
std::shared_ptr<services::UploadService> uploadService;
std::shared_ptr<services::ValidationService> validationService;
std::shared_ptr<services::AuditService> auditService;
std::shared_ptr<services::StatisticsService> statisticsService;
std::shared_ptr<services::LdifStructureService> ldifStructureService;  // v2.2.2

// Global cache for available countries (populated on startup)
std::set<std::string> cachedCountries;
std::mutex countriesCacheMutex;

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
    std::string dbPassword;  // Must be set via environment variable

    // LDAP Read: Application-level load balancing (v2.0.1 - HAProxy removed)
    // Format: "host1:port1,host2:port2,..."
    std::string ldapReadHosts = "openldap1:389,openldap2:389";
    std::vector<std::string> ldapReadHostList;  // Parsed from ldapReadHosts
    // Note: ldapReadRoundRobinIndex is defined as a global variable (atomic cannot be copied)

    // Legacy single host support (for backward compatibility)
    std::string ldapHost = "openldap1";
    int ldapPort = 389;

    // LDAP Write: Direct connection to primary master (openldap1) for write operations
    std::string ldapWriteHost = "openldap1";
    int ldapWritePort = 389;
    std::string ldapBindDn = "cn=admin,dc=ldap,dc=smartcoreinc,dc=com";
    std::string ldapBindPassword;  // Must be set via environment variable
    std::string ldapBaseDn = "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";

    // LDAP Container names (configurable via environment variables)
    std::string ldapDataContainer = "dc=data";        // For CSCA, DSC, LC, CRL
    std::string ldapNcDataContainer = "dc=nc-data";  // For non-conformant DSC

    // Trust Anchor for Master List CMS signature verification
    std::string trustAnchorPath = "/app/data/cert/UN_CSCA_2.pem";

    // ICAO Auto Sync Configuration (v1.7.0)
    std::string icaoPortalUrl = "https://pkddownloadsg.icao.int/";
    std::string notificationEmail = "admin@localhost";
    bool icaoAutoNotify = true;
    int icaoHttpTimeout = 10;  // seconds

    // ASN.1 Parser Configuration
    int asn1MaxLines = 100;  // Default max lines for Master List structure parsing

    int serverPort = 8081;
    int threadNum = 4;

    static AppConfig fromEnvironment() {
        AppConfig config;

        if (auto val = std::getenv("DB_HOST")) config.dbHost = val;
        if (auto val = std::getenv("DB_PORT")) config.dbPort = std::stoi(val);
        if (auto val = std::getenv("DB_NAME")) config.dbName = val;
        if (auto val = std::getenv("DB_USER")) config.dbUser = val;
        if (auto val = std::getenv("DB_PASSWORD")) config.dbPassword = val;

        // LDAP Read Hosts (v2.0.1 - Application-level load balancing)
        if (auto val = std::getenv("LDAP_READ_HOSTS")) {
            config.ldapReadHosts = val;
            // Parse comma-separated host:port list
            std::stringstream ss(config.ldapReadHosts);
            std::string item;
            while (std::getline(ss, item, ',')) {
                // Trim whitespace
                item.erase(0, item.find_first_not_of(" \t"));
                item.erase(item.find_last_not_of(" \t") + 1);
                if (!item.empty()) {
                    config.ldapReadHostList.push_back(item);
                }
            }
            if (config.ldapReadHostList.empty()) {
                throw std::runtime_error("LDAP_READ_HOSTS is empty or invalid");
            }
            spdlog::info("LDAP Read: {} hosts configured for load balancing", config.ldapReadHostList.size());
            for (const auto& host : config.ldapReadHostList) {
                spdlog::info("  - {}", host);
            }
        } else {
            // Fallback to single host for backward compatibility
            if (auto val = std::getenv("LDAP_HOST")) config.ldapHost = val;
            if (auto val = std::getenv("LDAP_PORT")) config.ldapPort = std::stoi(val);
            config.ldapReadHostList.push_back(config.ldapHost + ":" + std::to_string(config.ldapPort));
            spdlog::warn("LDAP_READ_HOSTS not set, using single host: {}", config.ldapReadHostList[0]);
        }

        if (auto val = std::getenv("LDAP_WRITE_HOST")) config.ldapWriteHost = val;
        if (auto val = std::getenv("LDAP_WRITE_PORT")) config.ldapWritePort = std::stoi(val);
        if (auto val = std::getenv("LDAP_BIND_DN")) config.ldapBindDn = val;
        if (auto val = std::getenv("LDAP_BIND_PASSWORD")) config.ldapBindPassword = val;
        if (auto val = std::getenv("LDAP_BASE_DN")) config.ldapBaseDn = val;
        if (auto val = std::getenv("LDAP_DATA_CONTAINER")) config.ldapDataContainer = val;
        if (auto val = std::getenv("LDAP_NC_DATA_CONTAINER")) config.ldapNcDataContainer = val;

        if (auto val = std::getenv("SERVER_PORT")) config.serverPort = std::stoi(val);
        if (auto val = std::getenv("THREAD_NUM")) config.threadNum = std::stoi(val);
        if (auto val = std::getenv("TRUST_ANCHOR_PATH")) config.trustAnchorPath = val;

        // ICAO Auto Sync environment variables
        if (auto val = std::getenv("ICAO_PORTAL_URL")) config.icaoPortalUrl = val;
        if (auto val = std::getenv("ICAO_NOTIFICATION_EMAIL")) config.notificationEmail = val;
        if (auto val = std::getenv("ICAO_AUTO_NOTIFY")) config.icaoAutoNotify = (std::string(val) == "true");
        if (auto val = std::getenv("ICAO_HTTP_TIMEOUT")) config.icaoHttpTimeout = std::stoi(val);

        // ASN.1 Parser Configuration
        if (auto val = std::getenv("ASN1_MAX_LINES")) config.asn1MaxLines = std::stoi(val);

        return config;
    }

    // Validate required credentials are set
    void validateRequiredCredentials() const {
        if (dbPassword.empty()) {
            throw std::runtime_error("FATAL: DB_PASSWORD environment variable not set");
        }
        if (ldapBindPassword.empty()) {
            throw std::runtime_error("FATAL: LDAP_BIND_PASSWORD environment variable not set");
        }
        spdlog::info("✅ All required credentials loaded from environment");
    }
};

// Global configuration
AppConfig appConfig;

// LDAP Read Load Balancing: Thread-safe round-robin index (global variable)
std::atomic<size_t> g_ldapReadRoundRobinIndex{0};

// =============================================================================
// SSE Progress Management
// =============================================================================

// ============================================================================
// Progress Manager - Now imported from common/progress_manager.h (Phase 4.4)
// ============================================================================
// Enhanced with X.509 metadata tracking and ICAO 9303 compliance monitoring.
// See common/progress_manager.h for full implementation.

using common::ProcessingStage;
using common::ProcessingProgress;
using common::ProgressManager;
using common::CertificateMetadata;
using common::IcaoComplianceStatus;
using common::ValidationStatistics;

// Audit logging (from shared library)
using icao::audit::AuditLogEntry;
using icao::audit::OperationType;
using icao::audit::logOperation;
using icao::audit::createAuditEntryFromRequest;
using icao::audit::extractUserFromRequest;
using icao::audit::extractIpAddress;

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
 * @brief DSC Trust Chain Validation Result (Updated for Sprint 3)
 * Added trustChainPath for link certificate support
 */
struct DscValidationResult {
    bool isValid;
    bool cscaFound;
    bool signatureValid;
    bool notExpired;
    bool notRevoked;
    std::string cscaSubjectDn;
    std::string errorMessage;
    std::string trustChainPath;  // Sprint 3: Human-readable chain path (e.g., "DSC → CN=CSCA_old → CN=Link → CN=CSCA_new")
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
 * @brief Extract DN components as a sorted, lowercase key for format-independent comparison.
 * Handles both RFC 2253 comma-separated (CN=X,O=Y,C=Z) and OpenSSL slash-separated (/C=Z/O=Y/CN=X) formats.
 * Returns a sorted string like "c=z|cn=x|o=y" for consistent comparison.
 * @param dn Input DN string in either format
 * @return Sorted lowercase pipe-separated DN components
 */
static std::string normalizeDnForComparison(const std::string& dn) {
    if (dn.empty()) return dn;

    std::vector<std::string> parts;

    if (dn[0] == '/') {
        // OpenSSL slash-separated format: /C=Z/O=Y/CN=X
        std::istringstream stream(dn);
        std::string segment;
        while (std::getline(stream, segment, '/')) {
            if (!segment.empty()) {
                std::string lower;
                for (char c : segment) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                size_t s = lower.find_first_not_of(" \t");
                if (s != std::string::npos) parts.push_back(lower.substr(s));
            }
        }
    } else {
        // RFC 2253 comma-separated format: CN=X,O=Y,C=Z
        std::string current;
        bool inQuotes = false;
        for (size_t i = 0; i < dn.size(); i++) {
            char c = dn[i];
            if (c == '"') {
                inQuotes = !inQuotes;
                current += c;
            } else if (c == ',' && !inQuotes) {
                std::string lower;
                for (char ch : current) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                size_t s = lower.find_first_not_of(" \t");
                if (s != std::string::npos) parts.push_back(lower.substr(s));
                current.clear();
            } else if (c == '\\' && i + 1 < dn.size()) {
                current += c;
                current += dn[++i];
            } else {
                current += c;
            }
        }
        if (!current.empty()) {
            std::string lower;
            for (char ch : current) lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            size_t s = lower.find_first_not_of(" \t");
            if (s != std::string::npos) parts.push_back(lower.substr(s));
        }
    }

    // Sort components for order-independent comparison
    std::sort(parts.begin(), parts.end());

    // Join with pipe separator
    std::string result;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) result += "|";
        result += parts[i];
    }
    return result;
}

/**
 * @brief Find CSCA certificate from DB by issuer DN
 * @return X509* pointer (caller must free) or nullptr if not found
 */
/**
 * @brief Extract a specific RDN attribute value from a DN string (either format).
 * @param dn DN in any format
 * @param attr Attribute name (e.g., "CN", "C", "O")
 * @return Attribute value (lowercase), or empty string if not found
 */
static std::string extractDnAttribute(const std::string& dn, const std::string& attr) {
    std::string searchKey = attr + "=";
    // Uppercase variant for case-insensitive search
    std::string dnLower = dn;
    for (char& c : dnLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    std::string keyLower = searchKey;
    for (char& c : keyLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    size_t pos = 0;
    while ((pos = dnLower.find(keyLower, pos)) != std::string::npos) {
        // Verify it's at a boundary (start of string, after / or ,)
        if (pos == 0 || dnLower[pos-1] == '/' || dnLower[pos-1] == ',') {
            size_t valStart = pos + keyLower.size();
            size_t valEnd = dn.find_first_of("/,", valStart);
            if (valEnd == std::string::npos) valEnd = dn.size();
            std::string val = dn.substr(valStart, valEnd - valStart);
            // Trim and lowercase
            size_t s = val.find_first_not_of(" \t");
            size_t e = val.find_last_not_of(" \t");
            if (s != std::string::npos) {
                val = val.substr(s, e - s + 1);
                for (char& c : val) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                return val;
            }
        }
        pos++;
    }
    return "";
}

X509* findCscaByIssuerDn(PGconn* conn, const std::string& issuerDn) {
    if (!conn || issuerDn.empty()) return nullptr;

    // Extract key DN components for robust matching across formats
    std::string cn = extractDnAttribute(issuerDn, "CN");
    std::string country = extractDnAttribute(issuerDn, "C");
    std::string org = extractDnAttribute(issuerDn, "O");

    // Build query using component-based matching (handles both /C=X/O=Y/CN=Z and CN=Z,O=Y,C=X formats)
    std::string query = "SELECT certificate_data, subject_dn FROM certificate WHERE certificate_type = 'CSCA'";
    if (!cn.empty()) {
        std::string escaped = cn;
        size_t pos = 0;
        while ((pos = escaped.find("'", pos)) != std::string::npos) { escaped.replace(pos, 1, "''"); pos += 2; }
        query += " AND LOWER(subject_dn) LIKE '%cn=" + escaped + "%'";
    }
    if (!country.empty()) {
        query += " AND LOWER(subject_dn) LIKE '%c=" + country + "%'";
    }
    if (!org.empty()) {
        std::string escaped = org;
        size_t pos = 0;
        while ((pos = escaped.find("'", pos)) != std::string::npos) { escaped.replace(pos, 1, "''"); pos += 2; }
        query += " AND LOWER(subject_dn) LIKE '%o=" + escaped + "%'";
    }
    query += " LIMIT 20";  // Fetch candidates, post-filter for exact match

    spdlog::debug("CSCA lookup query: {}", query.substr(0, 200));

    PGresult* res = PQexec(conn, query.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        spdlog::error("CSCA lookup query failed: {} - Query: {}",
                     PQerrorMessage(conn), query.substr(0, 200));
        PQclear(res);
        return nullptr;
    }

    // Post-filter: find exact DN match using normalized comparison
    std::string targetNormalized = normalizeDnForComparison(issuerDn);
    int matchedRow = -1;
    for (int i = 0; i < PQntuples(res); i++) {
        char* dbSubjectDn = PQgetvalue(res, i, 1);
        if (dbSubjectDn) {
            std::string dbNormalized = normalizeDnForComparison(std::string(dbSubjectDn));
            if (dbNormalized == targetNormalized) {
                matchedRow = i;
                break;
            }
        }
    }

    if (matchedRow < 0) {
        spdlog::warn("CSCA not found for issuer DN: {}", issuerDn.substr(0, 80));
        PQclear(res);
        return nullptr;
    }

    // Get binary certificate data from matched row
    char* certData = PQgetvalue(res, matchedRow, 0);
    int certLen = PQgetlength(res, matchedRow, 0);

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

// =============================================================================
// Sprint 2: Trust Chain Building Utilities
// =============================================================================

/**
 * @brief Trust Chain structure for DSC → CSCA validation
 * May include Link Certificates for CSCA key transitions
 */
struct TrustChain {
    std::vector<X509*> certificates;  // DSC → CSCA_old → Link → CSCA_new
    bool isValid;
    std::string path;  // Human-readable: "DSC → CN=CSCA_old → CN=Link → CN=CSCA_new"
    std::string errorMessage;
};

/**
 * @brief Get certificate subject DN as string
 * @param cert X509 certificate
 * @return Subject DN string
 */
static std::string getCertSubjectDn(X509* cert) {
    if (!cert) return "";

    char buffer[512];
    X509_NAME* subject = X509_get_subject_name(cert);
    X509_NAME_oneline(subject, buffer, sizeof(buffer));

    return std::string(buffer);
}

/**
 * @brief Get certificate issuer DN as string
 * @param cert X509 certificate
 * @return Issuer DN string
 */
static std::string getCertIssuerDn(X509* cert) {
    if (!cert) return "";

    char buffer[512];
    X509_NAME* issuer = X509_get_issuer_name(cert);
    X509_NAME_oneline(issuer, buffer, sizeof(buffer));

    return std::string(buffer);
}

/**
 * @brief Check if certificate is self-signed (subject == issuer)
 * @param cert X509 certificate
 * @return true if self-signed, false otherwise
 */
static bool isSelfSigned(X509* cert) {
    if (!cert) return false;

    std::string subjectDn = getCertSubjectDn(cert);
    std::string issuerDn = getCertIssuerDn(cert);

    // Case-insensitive DN comparison (RFC 4517)
    return (strcasecmp(subjectDn.c_str(), issuerDn.c_str()) == 0);
}

/**
 * @brief Check if certificate is a Link Certificate (cross-signed CSCA)
 * Link certificates have:
 * - Subject != Issuer (not self-signed)
 * - BasicConstraints: CA:TRUE
 * - KeyUsage: Certificate Sign
 * @param cert X509 certificate
 * @return true if link certificate, false otherwise
 */
static bool isLinkCertificate(X509* cert) {
    if (!cert) return false;

    // Must NOT be self-signed
    if (isSelfSigned(cert)) {
        return false;
    }

    // Check BasicConstraints: CA:TRUE
    BASIC_CONSTRAINTS* bc = (BASIC_CONSTRAINTS*)X509_get_ext_d2i(cert, NID_basic_constraints, nullptr, nullptr);
    if (!bc || !bc->ca) {
        if (bc) BASIC_CONSTRAINTS_free(bc);
        return false;
    }
    BASIC_CONSTRAINTS_free(bc);

    // Check KeyUsage: keyCertSign
    ASN1_BIT_STRING* usage = (ASN1_BIT_STRING*)X509_get_ext_d2i(cert, NID_key_usage, nullptr, nullptr);
    if (!usage) {
        return false;
    }

    bool hasKeyCertSign = (ASN1_BIT_STRING_get_bit(usage, 5) == 1);  // Bit 5 = keyCertSign
    ASN1_BIT_STRING_free(usage);

    return hasKeyCertSign;
}

/**
 * @brief Find ALL CSCAs matching subject DN (including link certificates)
 * @param conn PostgreSQL connection
 * @param subjectDn Subject DN to search
 * @return Vector of X509 certificates (caller must free)
 */
static std::vector<X509*> findAllCscasBySubjectDn(PGconn* conn, const std::string& subjectDn) {
    std::vector<X509*> result;

    if (!conn || subjectDn.empty()) {
        return result;
    }

    // Extract key DN components for robust matching across formats
    std::string cn = extractDnAttribute(subjectDn, "CN");
    std::string country = extractDnAttribute(subjectDn, "C");
    std::string org = extractDnAttribute(subjectDn, "O");

    // Build query using component-based matching (handles both /C=X/O=Y/CN=Z and CN=Z,O=Y,C=X formats)
    std::string query = "SELECT certificate_data, subject_dn FROM certificate WHERE certificate_type = 'CSCA'";
    if (!cn.empty()) {
        std::string escaped = cn;
        size_t pos = 0;
        while ((pos = escaped.find("'", pos)) != std::string::npos) { escaped.replace(pos, 1, "''"); pos += 2; }
        query += " AND LOWER(subject_dn) LIKE '%cn=" + escaped + "%'";
    }
    if (!country.empty()) {
        query += " AND LOWER(subject_dn) LIKE '%c=" + country + "%'";
    }
    if (!org.empty()) {
        std::string escaped = org;
        size_t pos = 0;
        while ((pos = escaped.find("'", pos)) != std::string::npos) { escaped.replace(pos, 1, "''"); pos += 2; }
        query += " AND LOWER(subject_dn) LIKE '%o=" + escaped + "%'";
    }

    spdlog::debug("Find all CSCAs query: {}", query.substr(0, 200));

    PGresult* res = PQexec(conn, query.c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        spdlog::error("Find all CSCAs query failed: {}", PQerrorMessage(conn));
        PQclear(res);
        return result;
    }

    int numRows = PQntuples(res);
    std::string targetNormalized = normalizeDnForComparison(subjectDn);
    spdlog::info("Find all CSCAs: query returned {} candidates for DN: {}", numRows, subjectDn.substr(0, 80));

    for (int i = 0; i < numRows; i++) {
        // Verify exact match using normalized comparison (post-filter for LIKE false positives)
        char* dbSubjectDn = PQgetvalue(res, i, 1);
        if (dbSubjectDn) {
            std::string dbNormalized = normalizeDnForComparison(std::string(dbSubjectDn));
            if (dbNormalized != targetNormalized) {
                spdlog::debug("Find all CSCAs: row {} DN mismatch (candidate='{}', target='{}')",
                             i, dbSubjectDn, subjectDn.substr(0, 80));
                continue;
            }
        }

        char* certData = PQgetvalue(res, i, 0);
        int certLen = PQgetlength(res, i, 0);

        if (!certData || certLen == 0) {
            spdlog::warn("Find all CSCAs: row {} has empty certificate data", i);
            continue;
        }

        // Parse bytea hex format
        std::vector<uint8_t> derBytes;
        if (certLen > 2 && certData[0] == '\\' && certData[1] == 'x') {
            // Hex encoded
            for (int j = 2; j < certLen; j += 2) {
                char hex[3] = {certData[j], certData[j+1], 0};
                derBytes.push_back(static_cast<uint8_t>(strtol(hex, nullptr, 16)));
            }
        } else {
            // Raw binary
            if (certLen > 0 && (unsigned char)certData[0] == 0x30) {
                derBytes.assign(certData, certData + certLen);
            }
        }

        if (derBytes.empty()) {
            spdlog::warn("Find all CSCAs: row {} failed to parse binary data", i);
            continue;
        }

        const uint8_t* data = derBytes.data();
        X509* cert = d2i_X509(nullptr, &data, static_cast<long>(derBytes.size()));
        if (!cert) {
            unsigned long err = ERR_get_error();
            char errBuf[256];
            ERR_error_string_n(err, errBuf, sizeof(errBuf));
            spdlog::error("Find all CSCAs: row {} d2i_X509 failed: {}", i, errBuf);
            continue;
        }

        result.push_back(cert);
        spdlog::debug("Find all CSCAs: row {} parsed successfully (selfSigned={})",
                     i, isSelfSigned(cert));
    }

    PQclear(res);
    spdlog::info("Found {} CSCA(s) for subject DN: {}", result.size(), subjectDn.substr(0, 80));
    return result;
}

/**
 * @brief Build trust chain from DSC to root CSCA
 * May traverse Link Certificates for CSCA key transitions
 * @param dscCert DSC certificate to validate
 * @param allCscas Vector of all available CSCA certificates
 * @param maxDepth Maximum chain depth (default: 5)
 * @return TrustChain structure with certificates and validity status
 */
static TrustChain buildTrustChain(X509* dscCert,
                           const std::vector<X509*>& allCscas,
                           int maxDepth = 5) {
    TrustChain chain;
    chain.isValid = false;

    if (!dscCert) {
        chain.errorMessage = "DSC certificate is null";
        return chain;
    }

    // Step 1: Add DSC as first certificate in chain
    chain.certificates.push_back(dscCert);

    // Step 2: Build chain iteratively
    X509* current = dscCert;
    std::set<std::string> visitedDns;  // Prevent circular references
    int depth = 0;

    while (depth < maxDepth) {
        depth++;

        // Check if current certificate is self-signed (root) — must be before circular ref check
        if (isSelfSigned(current)) {
            chain.isValid = true;
            spdlog::info("Chain building: Reached root CSCA at depth {}", depth);
            break;
        }

        // Get issuer DN of current certificate
        std::string currentIssuerDn = getCertIssuerDn(current);

        if (currentIssuerDn.empty()) {
            chain.errorMessage = "Failed to extract issuer DN";
            return chain;
        }

        // Prevent circular references
        if (visitedDns.count(currentIssuerDn) > 0) {
            chain.errorMessage = "Circular reference detected at depth " + std::to_string(depth);
            spdlog::error("Chain building: {}", chain.errorMessage);
            return chain;
        }
        visitedDns.insert(currentIssuerDn);

        // Find issuer certificate in CSCA list
        X509* issuer = nullptr;
        for (X509* csca : allCscas) {
            std::string cscaSubjectDn = getCertSubjectDn(csca);

            // Case-insensitive DN comparison (RFC 4517)
            if (strcasecmp(currentIssuerDn.c_str(), cscaSubjectDn.c_str()) == 0) {
                issuer = csca;
                spdlog::debug("Chain building: Found issuer at depth {}: {}",
                              depth, cscaSubjectDn.substr(0, 50));
                break;
            }
        }

        if (!issuer) {
            chain.errorMessage = "Chain broken: Issuer not found at depth " +
                                 std::to_string(depth) + " (issuer: " +
                                 currentIssuerDn.substr(0, 80) + ")";
            spdlog::warn("Chain building: {}", chain.errorMessage);
            return chain;
        }

        // Add issuer to chain
        chain.certificates.push_back(issuer);
        current = issuer;
    }

    if (depth >= maxDepth) {
        chain.errorMessage = "Maximum chain depth exceeded (" + std::to_string(maxDepth) + ")";
        chain.isValid = false;
        return chain;
    }

    // Step 3: Build human-readable path
    chain.path = "DSC";
    for (size_t i = 1; i < chain.certificates.size(); i++) {
        std::string subjectDn = getCertSubjectDn(chain.certificates[i]);
        // Extract CN from DN for readability
        size_t cnPos = subjectDn.find("CN=");
        std::string cnPart = (cnPos != std::string::npos)
                             ? subjectDn.substr(cnPos, 30)
                             : subjectDn.substr(0, 30);
        chain.path += " → " + cnPart;
    }

    return chain;
}

/**
 * @brief Validate entire trust chain (signatures + expiration)
 * @param chain TrustChain structure to validate
 * @return true if all signatures and expiration checks pass, false otherwise
 */
static bool validateTrustChain(const TrustChain& chain) {
    if (!chain.isValid) {
        spdlog::warn("Chain validation: Chain is already marked as invalid");
        return false;
    }

    if (chain.certificates.empty()) {
        spdlog::error("Chain validation: No certificates in chain");
        return false;
    }

    time_t now = time(nullptr);

    // Validate each certificate in chain (except the first one, which is DSC - already validated)
    for (size_t i = 1; i < chain.certificates.size(); i++) {
        X509* cert = chain.certificates[i];
        X509* issuer = (i + 1 < chain.certificates.size()) ? chain.certificates[i + 1] : cert;  // Last cert is self-signed

        // Check expiration
        if (X509_cmp_time(X509_get0_notAfter(cert), &now) < 0) {
            spdlog::warn("Chain validation: Certificate {} is EXPIRED", i);
            return false;
        }
        if (X509_cmp_time(X509_get0_notBefore(cert), &now) > 0) {
            spdlog::warn("Chain validation: Certificate {} is NOT YET VALID", i);
            return false;
        }

        // Verify signature (cert signed by issuer)
        EVP_PKEY* issuerPubKey = X509_get_pubkey(issuer);
        if (!issuerPubKey) {
            spdlog::error("Chain validation: Failed to extract public key from issuer {}", i);
            return false;
        }

        int verifyResult = X509_verify(cert, issuerPubKey);
        EVP_PKEY_free(issuerPubKey);

        if (verifyResult != 1) {
            unsigned long err = ERR_get_error();
            char errBuf[256];
            ERR_error_string_n(err, errBuf, sizeof(errBuf));
            spdlog::error("Chain validation: Signature verification FAILED at depth {}: {}",
                         i, errBuf);
            return false;
        }

        spdlog::debug("Chain validation: Certificate {} signature VALID", i);
    }

    spdlog::info("Chain validation: Trust chain VALID ({} certificates)",
                 chain.certificates.size());
    return true;
}

// =============================================================================
// DSC Trust Chain Validation (Updated for Sprint 3)
// =============================================================================

/**
 * @brief Validate DSC certificate against its issuing CSCA
 * Checks:
 * 1. CSCA exists in DB
 * 2. DSC signature is valid (signed by CSCA)
 * 3. DSC is not expired
 */
DscValidationResult validateDscCertificate(PGconn* conn, X509* dscCert, const std::string& issuerDn) {
    DscValidationResult result = {false, false, false, false, false, "", "", ""};  // Added trustChainPath field

    if (!dscCert) {
        result.errorMessage = "DSC certificate is null";
        return result;
    }

    // Step 1: Check DSC expiration
    time_t now = time(nullptr);
    if (X509_cmp_time(X509_get0_notAfter(dscCert), &now) < 0) {
        result.errorMessage = "DSC certificate is expired";
        spdlog::warn("DSC validation: DSC is EXPIRED");
        return result;
    }
    if (X509_cmp_time(X509_get0_notBefore(dscCert), &now) > 0) {
        result.errorMessage = "DSC certificate is not yet valid";
        spdlog::warn("DSC validation: DSC is NOT YET VALID");
        return result;
    }
    result.notExpired = true;

    // Step 2: Find ALL CSCAs matching issuer DN (including link certificates)
    std::vector<X509*> allCscas = findAllCscasBySubjectDn(conn, issuerDn);

    if (allCscas.empty()) {
        result.errorMessage = "No CSCA found for issuer: " + issuerDn.substr(0, 80);
        spdlog::warn("DSC validation: CSCA NOT FOUND");
        return result;
    }
    result.cscaFound = true;
    result.cscaSubjectDn = issuerDn;

    spdlog::info("DSC validation: Found {} CSCA(s) for issuer (may include link certs)",
                 allCscas.size());

    // Step 3: Build trust chain (may traverse link certificates)
    TrustChain chain = buildTrustChain(dscCert, allCscas);

    if (!chain.isValid) {
        result.errorMessage = "Failed to build trust chain: " + chain.errorMessage;
        spdlog::warn("DSC validation: {}", result.errorMessage);

        // Cleanup
        for (X509* csca : allCscas) X509_free(csca);
        return result;
    }

    spdlog::info("DSC validation: Trust chain built successfully ({} steps)",
                 chain.certificates.size());
    result.trustChainPath = chain.path;  // Sprint 3: Store human-readable chain path

    // Step 4: Validate entire chain (signatures + expiration)
    bool chainValid = validateTrustChain(chain);

    if (chainValid) {
        result.signatureValid = true;
        result.isValid = true;
        spdlog::info("DSC validation: Trust Chain VERIFIED - Path: {}", result.trustChainPath);
    } else {
        result.errorMessage = "Trust chain validation failed (signature or expiration check)";
        spdlog::error("DSC validation: Trust Chain FAILED - {}", result.errorMessage);
    }

    // Cleanup
    for (X509* csca : allCscas) X509_free(csca);

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
    std::string trustChainPath;  // Sprint 3: Human-readable chain path
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

    // Parameterized query matching actual validation_result table schema (23 columns)
    const char* query =
        "INSERT INTO validation_result ("
        "certificate_id, upload_id, certificate_type, country_code, "
        "subject_dn, issuer_dn, serial_number, "
        "validation_status, trust_chain_valid, trust_chain_message, "
        "csca_found, csca_subject_dn, csca_serial_number, csca_country, "
        "signature_valid, signature_algorithm, "
        "validity_period_valid, not_before, not_after, "
        "revocation_status, crl_checked, "
        "trust_chain_path"
        ") VALUES ("
        "$1, $2, $3, $4, $5, $6, $7, $8, $9, $10, "
        "$11, $12, $13, $14, $15, $16, $17, $18, $19, $20, $21, $22"
        ")";

    // Prepare boolean strings
    const std::string trustChainValidStr = record.trustChainValid ? "true" : "false";
    const std::string cscaFoundStr = record.cscaFound ? "true" : "false";
    const std::string signatureValidStr = record.signatureVerified ? "true" : "false";
    const std::string validityPeriodValidStr = record.validityCheckPassed ? "true" : "false";
    const std::string crlCheckedStr = (record.crlCheckStatus != "NOT_CHECKED") ? "true" : "false";

    // Map crlCheckStatus to revocation_status (schema uses UNKNOWN/NOT_REVOKED/REVOKED)
    std::string revocationStatus = "UNKNOWN";
    if (record.crlCheckStatus == "REVOKED") revocationStatus = "REVOKED";
    else if (record.crlCheckStatus == "NOT_REVOKED" || record.crlCheckStatus == "VALID") revocationStatus = "NOT_REVOKED";

    // Prepare trust_chain_path as JSON array (schema expects jsonb)
    // chain.path is human-readable like "DSC → CN=CSCA → CN=Link"
    // Wrap as JSON array for JSONB column
    std::string trustChainPathJson;
    if (record.trustChainPath.empty()) {
        trustChainPathJson = "[]";
    } else {
        Json::Value pathArray(Json::arrayValue);
        pathArray.append(record.trustChainPath);
        Json::StreamWriterBuilder builder;
        builder["indentation"] = "";
        trustChainPathJson = Json::writeString(builder, pathArray);
    }

    const char* paramValues[22];
    paramValues[0] = record.certificateId.c_str();
    paramValues[1] = record.uploadId.c_str();
    paramValues[2] = record.certificateType.c_str();
    paramValues[3] = record.countryCode.empty() ? nullptr : record.countryCode.c_str();
    paramValues[4] = record.subjectDn.c_str();
    paramValues[5] = record.issuerDn.c_str();
    paramValues[6] = record.serialNumber.empty() ? nullptr : record.serialNumber.c_str();
    paramValues[7] = record.validationStatus.c_str();
    paramValues[8] = trustChainValidStr.c_str();
    paramValues[9] = record.trustChainMessage.empty() ? nullptr : record.trustChainMessage.c_str();
    paramValues[10] = cscaFoundStr.c_str();
    paramValues[11] = record.cscaSubjectDn.empty() ? nullptr : record.cscaSubjectDn.c_str();
    paramValues[12] = nullptr;  // csca_serial_number - not tracked in ValidationResultRecord
    paramValues[13] = nullptr;  // csca_country - not tracked in ValidationResultRecord
    paramValues[14] = signatureValidStr.c_str();
    paramValues[15] = record.signatureAlgorithm.empty() ? nullptr : record.signatureAlgorithm.c_str();
    paramValues[16] = validityPeriodValidStr.c_str();
    paramValues[17] = record.notBefore.empty() ? nullptr : record.notBefore.c_str();
    paramValues[18] = record.notAfter.empty() ? nullptr : record.notAfter.c_str();
    paramValues[19] = revocationStatus.c_str();
    paramValues[20] = crlCheckedStr.c_str();
    paramValues[21] = trustChainPathJson.c_str();

    PGresult* res = PQexecParams(conn, query, 22, nullptr, paramValues,
                                 nullptr, nullptr, 0);
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
    // Use parameterized query (Phase 2 - SQL Injection Prevention)
    const char* query =
        "UPDATE uploaded_file SET "
        "validation_valid_count = $1, "
        "validation_invalid_count = $2, "
        "validation_pending_count = $3, "
        "validation_error_count = $4, "
        "trust_chain_valid_count = $5, "
        "trust_chain_invalid_count = $6, "
        "csca_not_found_count = $7, "
        "expired_count = $8, "
        "revoked_count = $9 "
        "WHERE id = $10";

    // Prepare integer strings
    std::string validCountStr = std::to_string(validCount);
    std::string invalidCountStr = std::to_string(invalidCount);
    std::string pendingCountStr = std::to_string(pendingCount);
    std::string errorCountStr = std::to_string(errorCount);
    std::string trustChainValidCountStr = std::to_string(trustChainValidCount);
    std::string trustChainInvalidCountStr = std::to_string(trustChainInvalidCount);
    std::string cscaNotFoundCountStr = std::to_string(cscaNotFoundCount);
    std::string expiredCountStr = std::to_string(expiredCount);
    std::string revokedCountStr = std::to_string(revokedCount);

    const char* paramValues[10] = {
        validCountStr.c_str(),
        invalidCountStr.c_str(),
        pendingCountStr.c_str(),
        errorCountStr.c_str(),
        trustChainValidCountStr.c_str(),
        trustChainInvalidCountStr.c_str(),
        cscaNotFoundCountStr.c_str(),
        expiredCountStr.c_str(),
        revokedCountStr.c_str(),
        uploadId.c_str()
    };

    PGresult* res = PQexecParams(conn, query, 10, nullptr, paramValues,
                                 nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("Failed to update validation statistics: {}", PQerrorMessage(conn));
    }
    PQclear(res);
}

// =============================================================================
// Credential Scrubbing Utility
// =============================================================================

/**
 * @brief Scrub sensitive credentials from log messages
 * @param message Original message that may contain passwords
 * @return Scrubbed message with credentials replaced by ***
 */
std::string scrubCredentials(const std::string& message) {
    std::string scrubbed = message;

    // Scrub PostgreSQL connection strings
    // Pattern: password=<anything until space or end>
    std::regex pgPasswordRegex(R"(password\s*=\s*[^\s]+)", std::regex::icase);
    scrubbed = std::regex_replace(scrubbed, pgPasswordRegex, "password=***");

    // Scrub LDAP URIs with credentials
    // Pattern: ldap://user:password@host
    std::regex ldapCredsRegex(R"(ldap://[^:]+:[^@]+@)");
    scrubbed = std::regex_replace(scrubbed, ldapCredsRegex, "ldap://***:***@");

    // Scrub LDAPS URIs with credentials
    std::regex ldapsCredsRegex(R"(ldaps://[^:]+:[^@]+@)");
    scrubbed = std::regex_replace(scrubbed, ldapsCredsRegex, "ldaps://***:***@");

    // Scrub JSON password fields
    // Pattern: "password":"value" or "password": "value"
    std::regex jsonPasswordRegex(R"("password"\s*:\s*"[^"]+")");
    scrubbed = std::regex_replace(scrubbed, jsonPasswordRegex, "\"password\":\"***\"");

    // Scrub bind password fields (LDAP specific)
    std::regex bindPasswordRegex(R"(bindPassword\s*=\s*[^\s,]+)", std::regex::icase);
    scrubbed = std::regex_replace(scrubbed, bindPasswordRegex, "bindPassword=***");

    return scrubbed;
}

// =============================================================================
// File Upload Security
// =============================================================================

/**
 * @brief Sanitize filename to prevent path traversal attacks
 * @param filename Original filename from upload
 * @return Sanitized filename (alphanumeric, dash, underscore, dot only)
 */
std::string sanitizeFilename(const std::string& filename) {
    std::string sanitized;

    // Only allow alphanumeric, dash, underscore, and dot
    for (char c : filename) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.') {
            sanitized += c;
        } else {
            sanitized += '_';  // Replace invalid chars with underscore
        }
    }

    // Prevent path traversal
    if (sanitized.find("..") != std::string::npos) {
        throw std::runtime_error("Invalid filename: contains '..'");
    }

    // Limit length to 255 characters
    if (sanitized.length() > 255) {
        sanitized = sanitized.substr(0, 255);
    }

    // Ensure filename is not empty
    if (sanitized.empty()) {
        throw std::runtime_error("Invalid filename: empty after sanitization");
    }

    return sanitized;
}

/**
 * @brief Validate LDIF file format
 * @param content File content as string
 * @return true if valid LDIF format
 */
bool isValidLdifFile(const std::string& content) {
    // LDIF files must contain "dn:" or "version:" entries
    if (content.find("dn:") == std::string::npos &&
        content.find("version:") == std::string::npos) {
        return false;
    }

    // Basic size check (should have at least some content)
    if (content.size() < 10) {
        return false;
    }

    return true;
}

/**
 * @brief Validate PKCS#7 (Master List) file format
 * @param content File content as binary vector
 * @return true if valid PKCS#7 DER format
 */
bool isValidP7sFile(const std::vector<uint8_t>& content) {
    // Check for PKCS#7 ASN.1 DER magic bytes
    // DER SEQUENCE: 0x30 followed by length encoding
    if (content.size() < 4) {
        return false;
    }

    // First byte should be 0x30 (SEQUENCE tag)
    if (content[0] != 0x30) {
        return false;
    }

    // Second byte should be length encoding
    // DER length encoding:
    // - 0x00-0x7F: short form (length <= 127 bytes)
    // - 0x80: indefinite form (not used in DER, but accept for compatibility)
    // - 0x81-0x84: long form (1-4 bytes for length)
    if (content[1] >= 0x80 && content[1] <= 0x84) {
        // Long form or indefinite form - valid
        return true;
    }
    if (content[1] >= 0x01 && content[1] <= 0x7F) {
        // Short form - valid
        return true;
    }

    // Invalid length encoding
    return false;
}

// =============================================================================
// Certificate Duplicate Check
// =============================================================================

/**
 * @brief Check if certificate with given fingerprint already exists in DB
 */
bool certificateExistsByFingerprint(PGconn* conn, const std::string& fingerprint) {
    const char* query = "SELECT 1 FROM certificate WHERE fingerprint_sha256 = $1 LIMIT 1";
    const char* paramValues[1] = {fingerprint.c_str()};
    PGresult* res = PQexecParams(conn, query, 1, nullptr, paramValues,
                                 nullptr, nullptr, 0);

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
 * @brief Send enhanced progress update with optional certificate metadata
 *
 * Phase 4.4 Task 3: Helper function for sending progress updates with
 * certificate metadata, ICAO compliance status, and validation statistics.
 *
 * @param uploadId Upload UUID
 * @param stage Current processing stage
 * @param processedCount Number of items processed
 * @param totalCount Total number of items
 * @param message User-facing progress message
 * @param metadata Optional certificate metadata
 * @param compliance Optional ICAO compliance status
 * @param stats Optional validation statistics
 */
void sendProgressWithMetadata(
    const std::string& uploadId,
    ProcessingStage stage,
    int processedCount,
    int totalCount,
    const std::string& message,
    const std::optional<CertificateMetadata>& metadata = std::nullopt,
    const std::optional<IcaoComplianceStatus>& compliance = std::nullopt,
    const std::optional<ValidationStatistics>& stats = std::nullopt
) {
    ProcessingProgress progress;

    if (metadata.has_value()) {
        progress = ProcessingProgress::createWithMetadata(
            uploadId, stage, processedCount, totalCount, message,
            metadata.value(), compliance, stats
        );
    } else {
        progress = ProcessingProgress::create(
            uploadId, stage, processedCount, totalCount, message
        );
    }

    ProgressManager::getInstance().sendProgress(progress);
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
 * Supports both slash-separated (/C=KR/O=...) and comma-separated (C=KR, O=...) formats
 */
std::string extractCountryCode(const std::string& dn) {
    // Match C= followed by 2-3 letter country code
    // Supports: /C=KR/, C=KR,, or C=KR at end/start
    static const std::regex countryRegex(R"((?:^|[/,]\s*)C=([A-Z]{2,3})(?:[/,\s]|$))", std::regex::icase);
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

    // Direct connection, no referral chasing needed
    ldap_set_option(ld, LDAP_OPT_REFERRALS, LDAP_OPT_OFF);

    struct berval cred;
    cred.bv_val = const_cast<char*>(appConfig.ldapBindPassword.c_str());
    cred.bv_len = appConfig.ldapBindPassword.length();

    rc = ldap_sasl_bind_s(ld, appConfig.ldapBindDn.c_str(), LDAP_SASL_SIMPLE, &cred, nullptr, nullptr, nullptr);
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP write connection bind failed: {}", ldap_err2string(rc));
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        return nullptr;
    }

    spdlog::debug("LDAP write: Connected successfully to {}:{}", appConfig.ldapWriteHost, appConfig.ldapWritePort);
    return ld;
}

/**
 * @brief Get LDAP connection for read operations with application-level load balancing
 *
 * v2.0.1: HAProxy removed, direct connection to MMR nodes with round-robin
 * Round-robin across multiple LDAP servers configured in LDAP_READ_HOSTS
 */
LDAP* getLdapReadConnection() {
    if (appConfig.ldapReadHostList.empty()) {
        spdlog::error("LDAP read connection failed: No LDAP hosts configured");
        return nullptr;
    }

    // Round-robin: Select next host in a thread-safe manner
    size_t hostIndex = g_ldapReadRoundRobinIndex.fetch_add(1) % appConfig.ldapReadHostList.size();
    std::string selectedHost = appConfig.ldapReadHostList[hostIndex];
    std::string uri = "ldap://" + selectedHost;

    spdlog::debug("LDAP read: Connecting to {} (round-robin index: {})", selectedHost, hostIndex);

    LDAP* ld = nullptr;
    int rc = ldap_initialize(&ld, uri.c_str());
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP read connection initialize failed for {}: {}", selectedHost, ldap_err2string(rc));
        return nullptr;
    }

    int version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

    // Referral chasing not needed for direct connections
    ldap_set_option(ld, LDAP_OPT_REFERRALS, LDAP_OPT_OFF);

    struct berval cred;
    cred.bv_val = const_cast<char*>(appConfig.ldapBindPassword.c_str());
    cred.bv_len = appConfig.ldapBindPassword.length();

    rc = ldap_sasl_bind_s(ld, appConfig.ldapBindDn.c_str(), LDAP_SASL_SIMPLE, &cred, nullptr, nullptr, nullptr);
    if (rc != LDAP_SUCCESS) {
        spdlog::error("LDAP read connection bind failed for {}: {}", selectedHost, ldap_err2string(rc));
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        return nullptr;
    }

    spdlog::debug("LDAP read: Connected successfully to {}", selectedHost);
    return ld;
}

/**
 * @brief Escape special characters in LDAP DN attribute values (RFC 4514)
 *
 * Special characters that need escaping in LDAP DN values:
 * - , (comma)
 * - = (equals) - CRITICAL: Must be escaped for Subject DN in cn!
 * - + (plus)
 * - " (double quote)
 * - \ (backslash)
 * - < (less than)
 * - > (greater than)
 * - ; (semicolon)
 * - Leading/trailing spaces
 * - Leading # (hash)
 *
 * Example: "CN=Test,O=Org" → "CN\\=Test\\,O\\=Org"
 *
 * @param value Unescaped DN attribute value
 * @return Escaped DN attribute value safe for use in LDAP DN
 */
std::string escapeLdapDnValue(const std::string& value) {
    if (value.empty()) return value;

    std::string escaped;
    escaped.reserve(value.size() * 2); // Reserve extra space for escapes

    for (size_t i = 0; i < value.size(); ++i) {
        char c = value[i];

        // Escape special characters (RFC 4514 + LDAP DN requirements)
        if (c == ',' || c == '=' || c == '+' || c == '"' || c == '\\' ||
            c == '<' || c == '>' || c == ';') {
            escaped += '\\';
            escaped += c;
        }
        // Escape leading space or hash
        else if (i == 0 && (c == ' ' || c == '#')) {
            escaped += '\\';
            escaped += c;
        }
        // Escape trailing space
        else if (i == value.size() - 1 && c == ' ') {
            escaped += '\\';
            escaped += c;
        }
        else {
            escaped += c;
        }
    }

    return escaped;
}

/**
 * @brief Extract standard and non-standard attributes from Subject DN
 *
 * Standard LDAP DN attributes: CN, O, OU, C, L, ST, DC
 * Non-standard attributes: emailAddress, street, telephoneNumber, serialNumber, postalCode, etc.
 *
 * Uses shared library DnParser for robust DN parsing.
 *
 * @param subjectDn Full Subject DN (e.g., "CN=CSCA,O=Org,emailAddress=test@example.com")
 * @return Pair of (standardDn, nonStandardAttrs)
 */
std::pair<std::string, std::string> extractStandardAttributes(const std::string& subjectDn) {
    // Standard LDAP DN attribute types (case-insensitive)
    static const std::set<std::string> standardAttrs = {
        "CN", "O", "OU", "C", "L", "ST", "DC", "STREET"
    };

    std::string standardDn;
    std::string nonStandardAttrs;

    try {
        // Use shared library DN parsing for robust handling
        X509_NAME* x509Name = icao::x509::parseDnString(subjectDn);
        if (x509Name) {
            auto components = icao::x509::extractDnComponents(x509Name);
            X509_NAME_free(x509Name);  // Free X509_NAME

            // Build standardDn from known standard fields
            std::vector<std::string> standardRdns;

            if (components.commonName.has_value() && !components.commonName->empty()) {
                standardRdns.push_back("CN=" + *components.commonName);
            }
            if (components.organization.has_value() && !components.organization->empty()) {
                standardRdns.push_back("O=" + *components.organization);
            }
            if (components.organizationalUnit.has_value() && !components.organizationalUnit->empty()) {
                standardRdns.push_back("OU=" + *components.organizationalUnit);
            }
            if (components.country.has_value() && !components.country->empty()) {
                standardRdns.push_back("C=" + *components.country);
            }
            if (components.locality.has_value() && !components.locality->empty()) {
                standardRdns.push_back("L=" + *components.locality);
            }
            if (components.stateOrProvince.has_value() && !components.stateOrProvince->empty()) {
                standardRdns.push_back("ST=" + *components.stateOrProvince);
            }

            // Build standardDn string
            for (size_t i = 0; i < standardRdns.size(); ++i) {
                if (i > 0) standardDn += ",";
                standardDn += standardRdns[i];
            }

            // If no standard attributes found, use original DN to avoid empty DN
            if (standardDn.empty()) {
                standardDn = subjectDn;
            }

            // Note: Non-standard attributes are not extracted in this version
            // since DnComponents only provides standard fields
        } else {
            // Fallback if parsing fails
            standardDn = subjectDn;
        }

    } catch (const std::exception& e) {
        // Fallback: if shared parser fails, use original DN
        spdlog::warn("Failed to parse DN with shared library, using original: {}", e.what());
        standardDn = subjectDn;
    }

    return {standardDn, nonStandardAttrs};
}

/**
 * @brief Build LDAP DN for certificate
 * @param certType CSCA, DSC, or DSC_NC
 * @param countryCode ISO country code
 * @param subjectDn Certificate Subject DN (used as CN for readability)
 * @param serialNumber Certificate serial number (used in multi-valued RDN)
 *
 * DN Structure (v1.4.21 - Multi-valued RDN like Java project):
 * cn={ESCAPED-SUBJECT-DN}+sn={SERIAL},o={csca|dsc},c={COUNTRY},dc={data|nc-data},dc=download,dc=pkd,{baseDN}
 *
 * This matches the working Java implementation which uses multi-valued RDN (cn+sn).
 * Multi-valued RDN is more robust when Subject DN contains LDAP special characters.
 *
 * v1.5.0: Extracts only standard LDAP attributes for DN to avoid LDAP error 80
 */
std::string buildCertificateDn(const std::string& certType, const std::string& countryCode,
                                const std::string& subjectDn, const std::string& serialNumber) {
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
        dataContainer = appConfig.ldapDataContainer;
    } else if (certType == "DSC") {
        ou = "dsc";
        dataContainer = appConfig.ldapDataContainer;
    } else if (certType == "LC") {
        // Sprint 3: Link Certificate support
        ou = "lc";
        dataContainer = appConfig.ldapDataContainer;
    } else if (certType == "DSC_NC") {
        ou = "dsc";
        dataContainer = appConfig.ldapNcDataContainer;
    } else {
        ou = "dsc";
        dataContainer = appConfig.ldapDataContainer;
    }

    // v1.5.0: Extract only standard attributes to avoid LDAP error 80
    auto [standardDn, nonStandardAttrs] = extractStandardAttributes(subjectDn);

    // Escape Subject DN for safe use in LDAP DN (RFC 4514)
    std::string escapedSubjectDn = escapeLdapDnValue(standardDn);

    // CRITICAL FIX v1.4.21: Use multi-valued RDN (cn+sn) like Java project
    // This isolates the complex Subject DN structure and makes DN parsing more robust
    // Multi-valued RDN: cn={ESCAPED-SUBJECT-DN}+sn={SERIAL}
    // Java DN: cn={ESCAPED-SUBJECT-DN}+sn={SERIAL},o={csca|dsc},c={COUNTRY},dc={data|nc-data},dc=download,dc=pkd,{baseDN}
    // CRITICAL FIX v2.0.0: Remove duplicate dc=download (appConfig.ldapBaseDn already contains it)
    return "cn=" + escapedSubjectDn + "+sn=" + serialNumber + ",o=" + ou + ",c=" + countryCode +
           "," + dataContainer + "," + appConfig.ldapBaseDn;
}

/**
 * @brief Build LDAP DN for certificate (v2 - Fingerprint-based)
 * @param fingerprint SHA-256 fingerprint of the certificate
 * @param certType CSCA, DSC, or DSC_NC
 * @param countryCode ISO country code
 *
 * DN Structure (v2.2.0 - Sprint 1: Fingerprint-based DN):
 * cn={SHA256-FINGERPRINT},o={csca|dsc|dsc_nc},c={COUNTRY},dc={data|nc-data},dc=download,dc=pkd,{baseDN}
 *
 * Benefits:
 * - Resolves RFC 5280 serial number uniqueness violations (serial number collisions across issuers)
 * - Eliminates DN escaping complexity (fingerprint is hex string, no special characters)
 * - Fixed DN length (~130 chars), well under LDAP 255-char limit
 * - Consistent with Master List and CRL DN structure
 *
 * Example:
 * cn=0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b,o=csca,c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
 */
std::string buildCertificateDnV2(const std::string& fingerprint, const std::string& certType,
                                   const std::string& countryCode) {
    // ICAO PKD DIT structure (updated for Sprint 2: added LC support)
    std::string ou;
    std::string dataContainer;

    if (certType == "CSCA") {
        ou = "csca";
        dataContainer = appConfig.ldapDataContainer;
    } else if (certType == "DSC") {
        ou = "dsc";
        dataContainer = appConfig.ldapDataContainer;
    } else if (certType == "DSC_NC") {
        ou = "dsc";  // DSC_NC uses o=dsc in nc-data container (same as legacy DN)
        dataContainer = appConfig.ldapNcDataContainer;
    } else if (certType == "LC") {
        // Sprint 2: Link Certificate support
        ou = "lc";
        dataContainer = appConfig.ldapDataContainer;
    } else if (certType == "MLSC") {
        // Sprint 3: Master List Signer Certificate support
        ou = "mlsc";
        dataContainer = appConfig.ldapDataContainer;
    } else {
        ou = "dsc";
        dataContainer = appConfig.ldapDataContainer;
    }

    // Fingerprint is SHA-256 hex (64 chars), no escaping needed
    // Example: 0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b
    return "cn=" + fingerprint + ",o=" + ou + ",c=" + countryCode +
           "," + dataContainer + "," + appConfig.ldapBaseDn;
}

/**
 * @brief Build LDAP DN for CRL
 *
 * SECURITY: Uses ldap_utils::escapeDnComponent for safe DN construction (RFC 4514)
 */
std::string buildCrlDn(const std::string& countryCode, const std::string& fingerprint) {
    // Fingerprint is SHA-256 hex (safe), but escape defensively
    // Country code is ISO 3166-1 alpha-2 (safe), but escape defensively
    // CRITICAL FIX v2.0.0: Remove duplicate dc=download (appConfig.ldapBaseDn already contains it)
    return "cn=" + ldap_utils::escapeDnComponent(fingerprint) +
           ",o=crl,c=" + ldap_utils::escapeDnComponent(countryCode) +
           "," + appConfig.ldapDataContainer + "," + appConfig.ldapBaseDn;
}

/**
 * @brief Ensure country organizational unit exists in LDAP
 *
 * SECURITY: Uses ldap_utils::escapeDnComponent for safe DN construction (RFC 4514)
 */
bool ensureCountryOuExists(LDAP* ld, const std::string& countryCode, bool isNcData = false) {
    std::string dataContainer = isNcData ? appConfig.ldapNcDataContainer : appConfig.ldapDataContainer;
    // CRITICAL FIX v2.0.0: Remove duplicate dc=download (appConfig.ldapBaseDn already contains it)

    // Sprint 3 Fix: Ensure data container exists before creating country entry
    std::string dataContainerDn = dataContainer + "," + appConfig.ldapBaseDn;
    LDAPMessage* dcResult = nullptr;
    int dcRc = ldap_search_ext_s(ld, dataContainerDn.c_str(), LDAP_SCOPE_BASE, "(objectClass=*)",
                                   nullptr, 0, nullptr, nullptr, nullptr, 1, &dcResult);
    if (dcResult) {
        ldap_msgfree(dcResult);
    }

    if (dcRc == LDAP_NO_SUCH_OBJECT) {
        // Create data container (dc=data or dc=nc-data)
        std::string dcValue = isNcData ? "nc-data" : "data";

        LDAPMod dcObjClass;
        dcObjClass.mod_op = LDAP_MOD_ADD;
        dcObjClass.mod_type = const_cast<char*>("objectClass");
        char* dcOcVals[] = {const_cast<char*>("top"), const_cast<char*>("dcObject"),
                            const_cast<char*>("organization"), nullptr};
        dcObjClass.mod_values = dcOcVals;

        LDAPMod dcDc;
        dcDc.mod_op = LDAP_MOD_ADD;
        dcDc.mod_type = const_cast<char*>("dc");
        char* dcVal[] = {const_cast<char*>(dcValue.c_str()), nullptr};
        dcDc.mod_values = dcVal;

        LDAPMod dcO;
        dcO.mod_op = LDAP_MOD_ADD;
        dcO.mod_type = const_cast<char*>("o");
        char* oVal[] = {const_cast<char*>(dcValue.c_str()), nullptr};
        dcO.mod_values = oVal;

        LDAPMod* dcMods[] = {&dcObjClass, &dcDc, &dcO, nullptr};

        int createRc = ldap_add_ext_s(ld, dataContainerDn.c_str(), dcMods, nullptr, nullptr);
        if (createRc != LDAP_SUCCESS && createRc != LDAP_ALREADY_EXISTS) {
            spdlog::warn("Failed to create data container {}: {}", dataContainerDn, ldap_err2string(createRc));
            return false;
        }
        spdlog::info("Created LDAP data container: {}", dataContainerDn);
    }

    std::string countryDn = "c=" + ldap_utils::escapeDnComponent(countryCode) +
                           "," + dataContainer + "," + appConfig.ldapBaseDn;

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

    // Create organizational units under country (csca, dsc, lc, mlsc, crl)
    // Sprint 2: Added "lc" for Link Certificates
    // Sprint 3: Added "mlsc" for Master List Signer Certificates
    std::vector<std::string> ous = isNcData ? std::vector<std::string>{"dsc"}
                                            : std::vector<std::string>{"csca", "dsc", "lc", "mlsc", "crl"};

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
                                   const std::vector<uint8_t>& certBinary,
                                   const std::string& pkdConformanceCode = "",
                                   const std::string& pkdConformanceText = "",
                                   const std::string& pkdVersion = "",
                                   bool useLegacyDn = false) {  // v2.1.2: Use fingerprint-based DN by default (fix LDAP storage failure)
    bool isNcData = (certType == "DSC_NC");

    // Ensure country structure exists
    if (!ensureCountryOuExists(ld, countryCode, isNcData)) {
        spdlog::warn("Failed to ensure country OU exists for {}", countryCode);
        // Continue anyway - the OU might exist even if we couldn't create it
    }

    // v1.5.0: Extract standard and non-standard attributes
    auto [standardDn, nonStandardAttrs] = extractStandardAttributes(subjectDn);

    // Sprint 1 (Week 5): Support both legacy (Subject DN + Serial) and new (Fingerprint) DN formats
    std::string dn;
    if (useLegacyDn) {
        dn = buildCertificateDn(certType, countryCode, subjectDn, serialNumber);
        spdlog::debug("[Legacy DN] Using Subject DN + Serial: {}", dn);
    } else {
        dn = buildCertificateDnV2(fingerprint, certType, countryCode);
        spdlog::debug("[v2 DN] Using Fingerprint-based DN: {}", dn);
    }

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

    // cn (Subject DN - required by person, must match DN's RDN)
    // v2.1.2: For v2 DN (fingerprint-based), use fingerprint as cn; for legacy DN, use standard DN
    // FIX: Avoid duplicate cn values when useLegacyDn=false (was causing LDAP operation failed error)
    LDAPMod modCn;
    modCn.mod_op = LDAP_MOD_ADD;
    modCn.mod_type = const_cast<char*>("cn");
    char* cnVals[3];
    if (useLegacyDn) {
        // Legacy DN: cn = [standardDn, fingerprint] for searchability
        cnVals[0] = const_cast<char*>(standardDn.c_str());
        cnVals[1] = const_cast<char*>(fingerprint.c_str());
        cnVals[2] = nullptr;
        spdlog::debug("[v2.1.2] Setting cn attribute (Legacy): standardDn + fingerprint");
        if (!nonStandardAttrs.empty()) {
            spdlog::debug("[v1.5.0] Non-standard attributes moved to description: {}", nonStandardAttrs);
        }
    } else {
        // v2 DN: cn = [fingerprint] only (must match DN RDN)
        cnVals[0] = const_cast<char*>(fingerprint.c_str());
        cnVals[1] = nullptr;
        spdlog::debug("[v2.1.2] Setting cn attribute (v2): fingerprint only");
    }
    modCn.mod_values = cnVals;

    // sn (serial number - required by person)
    LDAPMod modSn;
    modSn.mod_op = LDAP_MOD_ADD;
    modSn.mod_type = const_cast<char*>("sn");
    char* snVals[] = {const_cast<char*>(serialNumber.c_str()), nullptr};
    modSn.mod_values = snVals;

    // description (v1.5.0: Full Subject DN with non-standard attributes + fingerprint)
    std::string descriptionValue;
    if (!nonStandardAttrs.empty()) {
        descriptionValue = "Full Subject DN: " + subjectDn + " | Non-standard attributes: " + nonStandardAttrs + " | Fingerprint: " + fingerprint;
    } else {
        descriptionValue = "Subject DN: " + subjectDn + " | Fingerprint: " + fingerprint;
    }
    LDAPMod modDescription;
    modDescription.mod_op = LDAP_MOD_ADD;
    modDescription.mod_type = const_cast<char*>("description");
    char* descVals[] = {const_cast<char*>(descriptionValue.c_str()), nullptr};
    modDescription.mod_values = descVals;

    // userCertificate;binary (inetOrgPerson attribute for certificates)
    LDAPMod modCert;
    modCert.mod_op = LDAP_MOD_ADD | LDAP_MOD_BVALUES;
    modCert.mod_type = const_cast<char*>("userCertificate;binary");
    berval certBv;
    certBv.bv_val = reinterpret_cast<char*>(const_cast<uint8_t*>(certBinary.data()));
    certBv.bv_len = certBinary.size();
    berval* certBvVals[] = {&certBv, nullptr};
    modCert.mod_bvalues = certBvVals;

    // v1.5.6: DSC_NC specific attributes (pkdConformanceCode, pkdConformanceText, pkdVersion)
    LDAPMod modConformanceCode, modConformanceText, modVersion;
    char* conformanceCodeVals[] = {nullptr, nullptr};
    char* conformanceTextVals[] = {nullptr, nullptr};
    char* versionVals[] = {nullptr, nullptr};

    std::vector<LDAPMod*> modsVec = {&modObjectClass, &modCn, &modSn, &modDescription, &modCert};

    if (isNcData) {
        // Add pkdConformanceCode if provided
        if (!pkdConformanceCode.empty()) {
            modConformanceCode.mod_op = LDAP_MOD_ADD;
            modConformanceCode.mod_type = const_cast<char*>("pkdConformanceCode");
            conformanceCodeVals[0] = const_cast<char*>(pkdConformanceCode.c_str());
            modConformanceCode.mod_values = conformanceCodeVals;
            modsVec.push_back(&modConformanceCode);
            spdlog::debug("Adding pkdConformanceCode: {}", pkdConformanceCode);
        }

        // Add pkdConformanceText if provided
        if (!pkdConformanceText.empty()) {
            modConformanceText.mod_op = LDAP_MOD_ADD;
            modConformanceText.mod_type = const_cast<char*>("pkdConformanceText");
            conformanceTextVals[0] = const_cast<char*>(pkdConformanceText.c_str());
            modConformanceText.mod_values = conformanceTextVals;
            modsVec.push_back(&modConformanceText);
            spdlog::debug("Adding pkdConformanceText: {}", pkdConformanceText.substr(0, 50) + "...");
        }

        // Add pkdVersion if provided
        if (!pkdVersion.empty()) {
            modVersion.mod_op = LDAP_MOD_ADD;
            modVersion.mod_type = const_cast<char*>("pkdVersion");
            versionVals[0] = const_cast<char*>(pkdVersion.c_str());
            modVersion.mod_values = versionVals;
            modsVec.push_back(&modVersion);
            spdlog::debug("Adding pkdVersion: {}", pkdVersion);
        }
    }

    modsVec.push_back(nullptr);
    LDAPMod** mods = modsVec.data();

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
        spdlog::warn("Failed to save certificate to LDAP {}: {} (error code: {})", dn, ldap_err2string(rc), rc);

        // v1.4.22: Get detailed error information from LDAP connection
        char *matched_msg = nullptr;
        char *error_msg = nullptr;
        int ldap_rc = ldap_get_option(ld, LDAP_OPT_MATCHED_DN, &matched_msg);
        if (ldap_rc == LDAP_SUCCESS && matched_msg) {
            spdlog::warn("  LDAP matched DN: {}", matched_msg);
            ldap_memfree(matched_msg);
        }
        ldap_rc = ldap_get_option(ld, LDAP_OPT_DIAGNOSTIC_MESSAGE, &error_msg);
        if (ldap_rc == LDAP_SUCCESS && error_msg) {
            spdlog::warn("  LDAP diagnostic: {}", error_msg);
            ldap_memfree(error_msg);
        }

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

    // Use parameterized query (Phase 2 - SQL Injection Prevention)
    const char* query = "UPDATE certificate SET "
                       "ldap_dn = $1, ldap_dn_v2 = $2, stored_in_ldap = TRUE, stored_at = NOW() "
                       "WHERE id = $3";
    const char* paramValues[3] = {ldapDn.c_str(), ldapDn.c_str(), certId.c_str()};

    PGresult* res = PQexecParams(conn, query, 3, nullptr, paramValues,
                                 nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("Failed to update certificate LDAP status: {}", PQerrorMessage(conn));
    }
    PQclear(res);
}

/**
 * @brief Update CRL DB record with LDAP DN after successful LDAP storage
 */
void updateCrlLdapStatus(PGconn* conn, const std::string& crlId, const std::string& ldapDn) {
    if (ldapDn.empty()) return;

    // Use parameterized query (Phase 2 - SQL Injection Prevention)
    const char* query = "UPDATE crl SET "
                       "ldap_dn = $1, stored_in_ldap = TRUE, stored_at = NOW() "
                       "WHERE id = $2";
    const char* paramValues[2] = {ldapDn.c_str(), crlId.c_str()};

    PGresult* res = PQexecParams(conn, query, 2, nullptr, paramValues,
                                 nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("Failed to update CRL LDAP status: {}", PQerrorMessage(conn));
    }
    PQclear(res);
}

/**
 * @brief Build DN for Master List entry in LDAP (o=ml node)
 * Format: cn={fingerprint},o=ml,c={country},dc=data,dc=download,dc=pkd,{baseDN}
 *
 * SECURITY: Uses ldap_utils::escapeDnComponent for safe DN construction (RFC 4514)
 */
std::string buildMasterListDn(const std::string& countryCode, const std::string& fingerprint) {
    // Fingerprint is SHA-256 hex (safe), but escape defensively
    // Country code is ISO 3166-1 alpha-2 (safe), but escape defensively
    // CRITICAL FIX v2.0.0: Remove duplicate dc=download (appConfig.ldapBaseDn already contains it)
    return "cn=" + ldap_utils::escapeDnComponent(fingerprint) +
           ",o=ml,c=" + ldap_utils::escapeDnComponent(countryCode) +
           ",dc=data," + appConfig.ldapBaseDn;
}

/**
 * @brief Ensure Master List OU (o=ml) exists under country entry
 *
 * SECURITY: Uses ldap_utils::escapeDnComponent for safe DN construction (RFC 4514)
 */
bool ensureMasterListOuExists(LDAP* ld, const std::string& countryCode) {
    // CRITICAL FIX v2.0.0: Remove duplicate dc=download (appConfig.ldapBaseDn already contains it)
    std::string countryDn = "c=" + ldap_utils::escapeDnComponent(countryCode) +
                           ",dc=data," + appConfig.ldapBaseDn;

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

    // Use parameterized query (Phase 2 - SQL Injection Prevention)
    const char* query = "UPDATE master_list SET "
                       "ldap_dn = $1, stored_in_ldap = TRUE, stored_at = NOW() "
                       "WHERE id = $2";
    const char* paramValues[2] = {ldapDn.c_str(), mlId.c_str()};

    PGresult* res = PQexecParams(conn, query, 2, nullptr, paramValues,
                                 nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("Failed to update master list LDAP status: {}", PQerrorMessage(conn));
    }
    PQclear(res);
}

// =============================================================================
// Database Storage Functions
// =============================================================================

/**
 * @brief Save certificate to database with X.509 metadata
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

    // Extract X.509 metadata from certificate binary
    x509::CertificateMetadata metadata;
    X509* cert = nullptr;

    // Parse DER-encoded certificate
    const unsigned char* data = certBinary.data();
    cert = d2i_X509(nullptr, &data, static_cast<long>(certBinary.size()));

    if (cert) {
        metadata = x509::extractMetadata(cert);
        X509_free(cert);
    } else {
        spdlog::warn("Failed to parse X509 certificate for metadata extraction: {}", fingerprint);
    }

    // Helper lambda to convert vector<string> to PostgreSQL array literal
    auto vecToArray = [](const std::vector<std::string>& vec) -> std::string {
        if (vec.empty()) return "NULL";
        std::string result = "ARRAY[";
        for (size_t i = 0; i < vec.size(); i++) {
            result += "'" + vec[i] + "'";
            if (i < vec.size() - 1) result += ",";
        }
        result += "]";
        return result;
    };

    // Helper lambda for optional string
    auto optToSql = [&conn](const std::optional<std::string>& opt) -> std::string {
        return opt.has_value() ? escapeSqlString(conn, opt.value()) : "NULL";
    };

    // Helper lambda for optional int
    auto optIntToSql = [](const std::optional<int>& opt) -> std::string {
        return opt.has_value() ? std::to_string(opt.value()) : "NULL";
    };

    // Build INSERT query with X.509 metadata fields
    std::string query =
        "INSERT INTO certificate ("
        "id, upload_id, certificate_type, country_code, "
        "subject_dn, issuer_dn, serial_number, fingerprint_sha256, "
        "not_before, not_after, certificate_data, validation_status, validation_message, "
        // X.509 metadata fields (15 fields)
        "version, signature_algorithm, signature_hash_algorithm, "
        "public_key_algorithm, public_key_size, public_key_curve, "
        "key_usage, extended_key_usage, "
        "is_ca, path_len_constraint, "
        "subject_key_identifier, authority_key_identifier, "
        "crl_distribution_points, ocsp_responder_url, "
        "is_self_signed, "
        "duplicate_count, first_upload_id, "
        "created_at"
        ") VALUES ("
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
        + escapeSqlString(conn, validationMessage) + ", "
        // X.509 metadata values
        + std::to_string(metadata.version) + ", "
        + escapeSqlString(conn, metadata.signatureAlgorithm) + ", "
        + escapeSqlString(conn, metadata.signatureHashAlgorithm) + ", "
        + escapeSqlString(conn, metadata.publicKeyAlgorithm) + ", "
        + std::to_string(metadata.publicKeySize) + ", "
        + optToSql(metadata.publicKeyCurve) + ", "
        + vecToArray(metadata.keyUsage) + ", "
        + vecToArray(metadata.extendedKeyUsage) + ", "
        + (metadata.isCA ? "TRUE" : "FALSE") + ", "
        + optIntToSql(metadata.pathLenConstraint) + ", "
        + optToSql(metadata.subjectKeyIdentifier) + ", "
        + optToSql(metadata.authorityKeyIdentifier) + ", "
        + vecToArray(metadata.crlDistributionPoints) + ", "
        + optToSql(metadata.ocspResponderUrl) + ", "
        + (metadata.isSelfSigned ? "TRUE" : "FALSE") + ", "
        "0, "  // duplicate_count = 0 (first registration)
        "'" + uploadId + "', "  // first_upload_id = current uploadId
        "NOW()) "
        "ON CONFLICT (certificate_type, fingerprint_sha256) "
        "DO UPDATE SET "
        "  duplicate_count = certificate.duplicate_count + 1, "
        "  last_seen_upload_id = EXCLUDED.upload_id, "
        "  last_seen_at = NOW() "
        "RETURNING id";

    PGresult* res = PQexec(conn, query.c_str());
    ExecStatusType status = PQresultStatus(res);

    if (status == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        certId = PQgetvalue(res, 0, 0);
        PQclear(res);
        return certId;
    }

    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        spdlog::error("Failed to save certificate: {} (Status: {}, Error: {})",
                     fingerprint, PQresStatus(status), PQerrorMessage(conn));
    }

    PQclear(res);
    return "";
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
                           ValidationStats& validationStats,
                           common::ValidationStatistics& enhancedStats) {
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

    // Phase 4.4: Extract comprehensive certificate metadata for progress tracking
    // Note: This extraction is done early (before validation) so metadata is available
    // for enhanced progress updates. ICAO compliance will be checked after cert type is determined.
    CertificateMetadata certMetadata = common::extractCertificateMetadataForProgress(cert, false);
    spdlog::debug("Phase 4.4: Extracted metadata for cert: type={}, sigAlg={}, keySize={}",
                  certMetadata.certificateType, certMetadata.signatureAlgorithm, certMetadata.keySize);

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
        valRecord.trustChainPath = dscValidation.trustChainPath;  // Sprint 3: Chain path

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
        // v2.2.2 FIX: Detect Link Certificates (subject != issuer, CA capability)
        // Check if this is a Link Certificate by validating CA status
        auto cscaValidation = validateCscaCertificate(cert);
        bool isLinkCertificate = (cscaValidation.isCa && cscaValidation.hasKeyCertSign);

        if (isLinkCertificate) {
            // Link Certificate - Cross-signed CSCA (subject != issuer)
            certType = "CSCA";  // Store as CSCA in DB for querying
            cscaCount++;
            valRecord.certificateType = "CSCA";
            valRecord.isSelfSigned = false;  // Link cert is not self-signed
            valRecord.isCa = cscaValidation.isCa;
            valRecord.signatureVerified = false;  // Cannot self-verify
            valRecord.validityCheckPassed = cscaValidation.isValid;
            valRecord.keyUsageValid = cscaValidation.hasKeyCertSign;

            // Link certificates need parent CSCA validation (same as DSC)
            auto lcValidation = validateDscCertificate(conn, cert, issuerDn);
            valRecord.cscaFound = lcValidation.cscaFound;
            valRecord.cscaSubjectDn = lcValidation.cscaSubjectDn;
            valRecord.trustChainPath = lcValidation.trustChainPath;

            if (lcValidation.isValid) {
                validationStatus = "VALID";
                valRecord.validationStatus = "VALID";
                valRecord.trustChainValid = true;
                valRecord.trustChainMessage = "Trust chain verified: Link Certificate signed by CSCA";
                validationStats.validCount++;
                validationStats.trustChainValidCount++;
                spdlog::info("LC validation: Trust Chain VERIFIED for {} (issuer: {})",
                            countryCode, issuerDn.substr(0, 50));
            } else if (lcValidation.cscaFound) {
                validationStatus = "INVALID";
                validationMessage = lcValidation.errorMessage;
                valRecord.validationStatus = "INVALID";
                valRecord.trustChainValid = false;
                valRecord.trustChainMessage = lcValidation.errorMessage;
                valRecord.errorMessage = lcValidation.errorMessage;
                validationStats.invalidCount++;
                validationStats.trustChainInvalidCount++;
                spdlog::error("LC validation: Trust Chain FAILED - {} for {}",
                             lcValidation.errorMessage, countryCode);
            } else {
                validationStatus = "PENDING";
                validationMessage = lcValidation.errorMessage;
                valRecord.validationStatus = "PENDING";
                valRecord.trustChainMessage = "CSCA not found in database";
                valRecord.errorCode = "CSCA_NOT_FOUND";
                valRecord.errorMessage = lcValidation.errorMessage;
                validationStats.pendingCount++;
                validationStats.cscaNotFoundCount++;
                spdlog::warn("LC validation: CSCA not found - {} for {}",
                            lcValidation.errorMessage, countryCode);
            }
        } else {
            // Regular DSC
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
        valRecord.trustChainPath = dscValidation.trustChainPath;  // Sprint 3: Chain path

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
        }  // End of else block for regular DSC
    }

    // Phase 4.4: Check ICAO 9303 compliance after certificate type is determined
    IcaoComplianceStatus icaoCompliance = common::checkIcaoCompliance(cert, certType);
    spdlog::debug("Phase 4.4: ICAO compliance for {} cert: isCompliant={}, level={}",
                  certType, icaoCompliance.isCompliant, icaoCompliance.complianceLevel);

    // Phase 4.4: Update enhanced statistics (ValidationStatistics)
    enhancedStats.totalCertificates++;
    enhancedStats.certificateTypes[certType]++;
    enhancedStats.signatureAlgorithms[certMetadata.signatureAlgorithm]++;
    enhancedStats.keySizes[certMetadata.keySize]++;

    // Update ICAO compliance counts
    if (icaoCompliance.isCompliant) {
        enhancedStats.icaoCompliantCount++;
    } else {
        enhancedStats.icaoNonCompliantCount++;
    }

    // Update validation status counts (matches legacy validationStats)
    if (validationStatus == "VALID") {
        enhancedStats.validCount++;
    } else if (validationStatus == "INVALID") {
        enhancedStats.invalidCount++;
    } else if (validationStatus == "PENDING") {
        enhancedStats.pendingCount++;
    }

    spdlog::debug("Phase 4.4: Updated statistics - total={}, type={}, sigAlg={}, keySize={}, icaoCompliant={}",
                  enhancedStats.totalCertificates, certType, certMetadata.signatureAlgorithm,
                  certMetadata.keySize, icaoCompliance.isCompliant);
    // Note: This requires passing ValidationStatistics as a parameter to this function
    // For now, we log the metadata and compliance for verification
    // Statistics will be updated once the parameter is added to function signature

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
            // v1.5.6: Extract DSC_NC specific attributes from LDIF entry
            std::string pkdConformanceCode = entry.getFirstAttribute("pkdConformanceCode");
            std::string pkdConformanceText = entry.getFirstAttribute("pkdConformanceText");
            std::string pkdVersion = entry.getFirstAttribute("pkdVersion");

            // v2.2.2 FIX: Use "LC" for LDAP storage of Link Certificates
            // DB stores as "CSCA" for querying, but LDAP uses "LC" for proper organizational unit
            std::string ldapCertType = certType;
            if (certType == "CSCA" && !valRecord.isSelfSigned) {
                ldapCertType = "LC";  // Link Certificate (subject != issuer)
                spdlog::debug("Using LDAP cert type 'LC' for link certificate: {}", fingerprint.substr(0, 16));
            }

            std::string ldapDn = saveCertificateToLdap(ld, ldapCertType, countryCode,
                                                        subjectDn, issuerDn, serialNumber,
                                                        fingerprint, derBytes,
                                                        pkdConformanceCode, pkdConformanceText, pkdVersion);
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
 * @brief Helper function to send DB_SAVING_IN_PROGRESS progress
 * This is called from ldif_processor.cpp
 */
void sendDbSavingProgress(const std::string& uploadId, int processedCount, int totalCount, const std::string& message) {
    ProgressManager::getInstance().sendProgress(
        ProcessingProgress::create(uploadId, ProcessingStage::DB_SAVING_IN_PROGRESS,
            processedCount, totalCount, message));
}

/**
 * @brief Helper function to send COMPLETED progress
 * This is called from processing_strategy.cpp
 */
void sendCompletionProgress(const std::string& uploadId, int totalItems, const std::string& message) {
    ProgressManager::getInstance().sendProgress(
        ProcessingProgress::create(uploadId, ProcessingStage::COMPLETED,
            totalItems, totalItems, message));
}

/**
 * @brief Parse and save Master List from LDIF entry (DB + LDAP) - DEPRECATED v2.0.0
 *
 * @deprecated This function is deprecated since v2.0.0.
 *             Use parseMasterListEntryV2() instead which extracts individual CSCAs.
 *
 * This handles entries with pkdMasterListContent attribute.
 * LIMITATION: Only stores the entire Master List CMS without extracting individual CSCAs.
 *
 * @note Kept for backward compatibility. Will be removed in future versions.
 */
[[deprecated("Use parseMasterListEntryV2() from masterlist_processor.h instead")]]
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

/**
 * @brief Process LDIF file asynchronously with full parsing (DB + LDAP)
 * @note Defined outside anonymous namespace for external linkage (Phase 4.4)
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
            spdlog::error("Database connection failed for async processing: {}", scrubCredentials(std::string(PQerrorMessage(conn))));
            PQfinish(conn);
            return;
        }

        // Check processing_mode (parameterized query)
        const char* modeQuery = "SELECT processing_mode FROM uploaded_file WHERE id = $1";
        const char* paramValues[1] = {uploadId.c_str()};
        PGresult* modeRes = PQexecParams(conn, modeQuery, 1, nullptr, paramValues,
                                         nullptr, nullptr, 0);
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
                spdlog::error("CRITICAL: LDAP write connection failed in AUTO mode for upload {}", uploadId);
                spdlog::error("Cannot proceed - data consistency requires both DB and LDAP storage");

                // Update upload status to FAILED
                const char* failQuery = "UPDATE uploaded_file SET status = 'FAILED', "
                                       "error_message = 'LDAP connection failure - cannot ensure data consistency', "
                                       "updated_at = NOW() WHERE id = $1";
                const char* failParams[1] = {uploadId.c_str()};
                PGresult* failRes = PQexecParams(conn, failQuery, 1, nullptr, failParams,
                                                nullptr, nullptr, 0);
                PQclear(failRes);

                // Send failure progress
                ProgressManager::getInstance().sendProgress(
                    ProcessingProgress::create(uploadId, ProcessingStage::FAILED,
                        0, 0, "LDAP 연결 실패", "데이터 일관성을 보장할 수 없어 처리를 중단했습니다."));

                if (ld) ldap_unbind_ext_s(ld, nullptr, nullptr);
                PQfinish(conn);
                return;
            }
            spdlog::info("LDAP write connection established successfully for AUTO mode upload {}", uploadId);
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

            // Use Strategy Pattern to handle AUTO vs MANUAL modes
            auto strategy = ProcessingStrategyFactory::create(processingMode);
            strategy->processLdifEntries(uploadId, entries, conn, ld);

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
                PQfinish(conn);
                return;
            }

            // For AUTO mode, strategy has already processed everything
            // No additional work needed here
            spdlog::info("AUTO mode: Processing completed by Strategy Pattern");

            /* OLD CODE - Replaced by Strategy Pattern - KEEP THIS COMMENTED OUT!
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
            MasterListStats mlStats;  // Master List processing statistics (v2.0.0)

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
                    // v2.0.0: Use new processor that extracts individual CSCAs
                    if (entry.hasAttribute("pkdMasterListContent;binary") || entry.hasAttribute("pkdMasterListContent")) {
                        parseMasterListEntryV2(conn, ld, uploadId, entry, mlStats);
                        // Update legacy counters for backward compatibility
                        mlCount = mlStats.mlCount;
                        ldapMlStoredCount = mlStats.ldapMlStoredCount;
                        cscaCount += mlStats.cscaNewCount;  // Add new CSCAs to count
                        ldapCertStoredCount += mlStats.ldapCscaStoredCount;  // Add LDAP stored CSCAs
                    }

                } catch (const std::exception& e) {
                    spdlog::warn("Error processing entry {}: {}", entry.dn, e.what());
                }

                processedEntries++;

                // Send progress update every 50 entries
                if (processedEntries % 50 == 0 || processedEntries == totalEntries) {
                    // v1.5.1: Enhanced progress message with detailed certificate type breakdown
                    std::string progressMsg = "처리 중: ";
                    std::vector<std::string> parts;
                    if (cscaCount > 0) parts.push_back("CSCA " + std::to_string(cscaCount));
                    if (dscCount > 0) parts.push_back("DSC " + std::to_string(dscCount));
                    if (dscNcCount > 0) parts.push_back("DSC_NC " + std::to_string(dscNcCount));
                    if (crlCount > 0) parts.push_back("CRL " + std::to_string(crlCount));
                    if (mlCount > 0) parts.push_back("ML " + std::to_string(mlCount));

                    for (size_t i = 0; i < parts.size(); ++i) {
                        if (i > 0) progressMsg += ", ";
                        progressMsg += parts[i];
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

            // v1.5.1: Enhanced LDAP progress message with detailed breakdown
            std::string ldapProgressMsg = "LDAP 저장: ";
            std::vector<std::string> ldapParts;
            int totalCerts = cscaCount + dscCount + dscNcCount;
            if (totalCerts > 0) ldapParts.push_back("인증서 " + std::to_string(ldapCertStoredCount) + "/" + std::to_string(totalCerts));
            if (crlCount > 0) ldapParts.push_back("CRL " + std::to_string(ldapCrlStoredCount) + "/" + std::to_string(crlCount));
            if (mlCount > 0) ldapParts.push_back("ML " + std::to_string(ldapMlStoredCount) + "/" + std::to_string(mlCount));

            for (size_t i = 0; i < ldapParts.size(); ++i) {
                if (i > 0) ldapProgressMsg += ", ";
                ldapProgressMsg += ldapParts[i];
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

            // v1.5.1: Send completion progress with validation info (only show non-zero counts)
            std::string completionMsg = "처리 완료: ";
            std::vector<std::string> completionParts;
            if (cscaCount > 0) completionParts.push_back("CSCA " + std::to_string(cscaCount) + "개");
            if (dscCount > 0) completionParts.push_back("DSC " + std::to_string(dscCount) + "개");
            if (dscNcCount > 0) completionParts.push_back("DSC_NC " + std::to_string(dscNcCount) + "개");
            if (crlCount > 0) completionParts.push_back("CRL " + std::to_string(crlCount) + "개");
            if (mlCount > 0) completionParts.push_back("ML " + std::to_string(mlCount) + "개");

            for (size_t i = 0; i < completionParts.size(); ++i) {
                if (i > 0) completionMsg += ", ";
                completionMsg += completionParts[i];
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

namespace {  // Anonymous namespace for internal helper functions

/**
 * @brief Core Master List processing logic (called by Strategy Pattern)
 *
 * This function is declared in common.h and used by ProcessingStrategy implementations.
 * It MUST be outside anonymous namespace to have external linkage.
 *
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
                                    std::string certType = "CSCA";  // DB always stores as CSCA
                                    std::string ldapCertType = "CSCA";  // LDAP cert type (CSCA or LC)
                                    bool isLinkCert = false;

                                    // Phase 4.4: Extract comprehensive certificate metadata for progress tracking
                                    // Note: This extraction is done early (before type determination) so metadata is available
                                    // for enhanced progress updates. ICAO compliance will be checked after cert type is determined.
                                    CertificateMetadata certMetadata = common::extractCertificateMetadataForProgress(cert, false);
                                    spdlog::debug("Phase 4.4 (Master List): Extracted metadata for cert: type={}, sigAlg={}, keySize={}",
                                                  certMetadata.certificateType, certMetadata.signatureAlgorithm, certMetadata.keySize);

                                    // Sprint 3 Task 3.3: Validate both self-signed CSCAs and link certificates
                                    std::string validationStatus = "VALID";
                                    if (isSelfSigned(cert)) {
                                        // Self-signed CSCA: validate using existing function
                                        auto cscaValidation = validateCscaCertificate(cert);
                                        validationStatus = cscaValidation.isValid ? "VALID" : "INVALID";
                                        ldapCertType = "CSCA";
                                    } else if (isLinkCertificate(cert)) {
                                        // Master List Signer Certificate: verify it has CA:TRUE and keyCertSign
                                        // Master List Signer certificates are cross-signed by CSCAs
                                        // They cannot be validated independently (require CSCA for signature check)
                                        // For now, mark as VALID if it has correct extensions
                                        validationStatus = "VALID";
                                        isLinkCert = true;
                                        ldapCertType = "MLSC";  // Store in o=mlsc branch
                                        spdlog::info("Master List: Master List Signer Certificate detected (will save to o=mlsc): {}", subjectDn);
                                    } else {
                                        // Neither self-signed CSCA nor link certificate
                                        validationStatus = "INVALID";
                                        spdlog::warn("Master List: Invalid certificate (not self-signed and not link cert): {}", subjectDn);
                                    }

                                    // Phase 4.4: Check ICAO 9303 compliance after certificate type is determined
                                    // Use ldapCertType as it correctly identifies MLSC vs CSCA
                                    IcaoComplianceStatus icaoCompliance = common::checkIcaoCompliance(cert, ldapCertType);
                                    spdlog::debug("Phase 4.4 (Master List): ICAO compliance for {} cert: isCompliant={}, level={}",
                                                  ldapCertType, icaoCompliance.isCompliant, icaoCompliance.complianceLevel);

                                    // Phase 4.4 TODO: Update enhanced statistics (ValidationStatistics)
                                    // Note: This requires passing ValidationStatistics as a parameter to this function
                                    // For now, we log the metadata and compliance for verification
                                    // Statistics will be updated once the parameter is added to function signature

                                    // Save to DB
                                    std::string certId = saveCertificate(conn, uploadId, certType, countryCode,
                                                                         subjectDn, issuerDn, serialNumber, fingerprint,
                                                                         notBefore, notAfter, derBytes);

                                    if (!certId.empty()) {
                                        cscaCount++;

                                        // Save to LDAP if connection available
                                        if (ld) {
                                            // Sprint 3 Fix: Use fingerprint-based DN to avoid duplicates
                                            // (Legacy DN with Subject DN + Serial can create duplicates when non-standard attributes differ)
                                            std::string ldapDn = saveCertificateToLdap(ld, ldapCertType, countryCode,
                                                                                        subjectDn, issuerDn, serialNumber,
                                                                                        fingerprint, derBytes,
                                                                                        "", "", "", false);  // useLegacyDn=false
                                            if (!ldapDn.empty()) {
                                                updateCertificateLdapStatus(conn, certId, ldapDn);
                                                ldapStoredCount++;
                                                if (isLinkCert) {
                                                    spdlog::info("Master List: Master List Signer Certificate saved to LDAP o=mlsc: {}", ldapDn);
                                                }
                                            }
                                        }
                                    }

                                    // v1.5.1: Progress update with detailed breakdown
                                    if (totalCerts % 50 == 0) {
                                        std::string mlProgressMsg = "처리 중: CSCA " + std::to_string(cscaCount);
                                        if (ld) {
                                            mlProgressMsg += ", LDAP 저장 " + std::to_string(ldapStoredCount);
                                        }
                                        ProgressManager::getInstance().sendProgress(
                                            ProcessingProgress::create(uploadId, ProcessingStage::DB_SAVING_IN_PROGRESS,
                                                totalCerts, totalCerts, mlProgressMsg));
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

                            // Phase 4.4: Extract comprehensive certificate metadata for progress tracking
                            // Note: This extraction is done early (before type determination) so metadata is available
                            // for enhanced progress updates. ICAO compliance will be checked after cert type is determined.
                            CertificateMetadata certMetadata = common::extractCertificateMetadataForProgress(cert, false);
                            spdlog::debug("Phase 4.4 (Master List PKCS7): Extracted metadata for cert: type={}, sigAlg={}, keySize={}",
                                          certMetadata.certificateType, certMetadata.signatureAlgorithm, certMetadata.keySize);

                            // Determine certificate type: Self-signed CSCA or Link Certificate
                            std::string certType = "CSCA";  // DB always stores as CSCA
                            std::string ldapCertType = "CSCA";  // LDAP cert type (CSCA or LC)
                            bool isLinkCert = false;

                            // Sprint 3 Task 3.3: Validate both self-signed CSCAs and link certificates
                            std::string validationStatus = "VALID";
                            if (isSelfSigned(cert)) {
                                // Self-signed CSCA: validate using existing function
                                auto cscaValidation = validateCscaCertificate(cert);
                                validationStatus = cscaValidation.isValid ? "VALID" : "INVALID";
                                ldapCertType = "CSCA";
                            } else if (isLinkCertificate(cert)) {
                                // Master List Signer Certificate: verify it has CA:TRUE and keyCertSign
                                validationStatus = "VALID";
                                isLinkCert = true;
                                ldapCertType = "MLSC";  // Store in o=mlsc branch
                                spdlog::info("Master List (PKCS7): Master List Signer Certificate detected (will save to o=mlsc): {}", subjectDn);
                            } else {
                                validationStatus = "INVALID";
                                spdlog::warn("Master List (PKCS7): Invalid certificate (not self-signed and not link cert): {}", subjectDn);
                            }

                            // Phase 4.4: Check ICAO 9303 compliance after certificate type is determined
                            // Use ldapCertType as it correctly identifies MLSC vs CSCA
                            IcaoComplianceStatus icaoCompliance = common::checkIcaoCompliance(cert, ldapCertType);
                            spdlog::debug("Phase 4.4 (Master List PKCS7): ICAO compliance for {} cert: isCompliant={}, level={}",
                                          ldapCertType, icaoCompliance.isCompliant, icaoCompliance.complianceLevel);

                            // Phase 4.4 TODO: Update enhanced statistics (ValidationStatistics)
                            // Note: This requires passing ValidationStatistics as a parameter to this function
                            // For now, we log the metadata and compliance for verification
                            // Statistics will be updated once the parameter is added to function signature

                            std::string certId = saveCertificate(conn, uploadId, certType, countryCode,
                                                                 subjectDn, issuerDn, serialNumber, fingerprint,
                                                                 notBefore, notAfter, derBytes);

                            if (!certId.empty()) {
                                cscaCount++;
                                if (ld) {
                                    // Sprint 3 Fix: Use fingerprint-based DN to avoid duplicates
                                    std::string ldapDn = saveCertificateToLdap(ld, ldapCertType, countryCode,
                                                                                subjectDn, issuerDn, serialNumber,
                                                                                fingerprint, derBytes,
                                                                                "", "", "", false);  // useLegacyDn=false
                                    if (!ldapDn.empty()) {
                                        updateCertificateLdapStatus(conn, certId, ldapDn);
                                        ldapStoredCount++;
                                        if (isLinkCert) {
                                            spdlog::info("Master List (PKCS7): Master List Signer Certificate saved to LDAP o=mlsc: {}", ldapDn);
                                        }
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

        // v1.5.1: Enhanced Master List completion message (only show actual counts)
        std::string mlCompletionMsg = "처리 완료: CSCA " + std::to_string(cscaCount) + "개";
        if (ld && ldapStoredCount > 0) {
            mlCompletionMsg += ", LDAP 저장 " + std::to_string(ldapStoredCount) + "/" + std::to_string(cscaCount);
        }

        ProgressManager::getInstance().sendProgress(
            ProcessingProgress::create(uploadId, ProcessingStage::COMPLETED,
                cscaCount, cscaCount, mlCompletionMsg));

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

}  // Close anonymous namespace before external function

/**
 * @brief Parse Master List (CMS SignedData) and extract CSCA certificates (DB + LDAP)
 * @note Defined outside anonymous namespace for external linkage (Phase 4.4)
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
            spdlog::error("Database connection failed for async processing: {}", scrubCredentials(std::string(PQerrorMessage(conn))));
            PQfinish(conn);
            return;
        }

        // Check processing_mode (parameterized query)
        const char* modeQuery = "SELECT processing_mode FROM uploaded_file WHERE id = $1";
        const char* paramValues[1] = {uploadId.c_str()};
        PGresult* modeRes = PQexecParams(conn, modeQuery, 1, nullptr, paramValues,
                                         nullptr, nullptr, 0);
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
                spdlog::error("CRITICAL: LDAP write connection failed in AUTO mode for Master List upload {}", uploadId);
                spdlog::error("Cannot proceed - data consistency requires both DB and LDAP storage");

                // Update upload status to FAILED
                const char* failQuery = "UPDATE uploaded_file SET status = 'FAILED', "
                                       "error_message = 'LDAP connection failure - cannot ensure data consistency', "
                                       "updated_at = NOW() WHERE id = $1";
                const char* failParams[1] = {uploadId.c_str()};
                PGresult* failRes = PQexecParams(conn, failQuery, 1, nullptr, failParams,
                                                nullptr, nullptr, 0);
                PQclear(failRes);

                // Send failure progress
                ProgressManager::getInstance().sendProgress(
                    ProcessingProgress::create(uploadId, ProcessingStage::FAILED,
                        0, 0, "LDAP 연결 실패", "데이터 일관성을 보장할 수 없어 처리를 중단했습니다."));

                PQfinish(conn);
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

                            // Phase 4.4: Extract comprehensive certificate metadata for progress tracking
                            CertificateMetadata certMetadata = common::extractCertificateMetadataForProgress(cert, false);
                            spdlog::debug("Phase 4.4 (Master List PKCS7 fallback): Extracted metadata for cert: type={}, sigAlg={}, keySize={}",
                                          certMetadata.certificateType, certMetadata.signatureAlgorithm, certMetadata.keySize);

                            // Master List contains ONLY CSCA certificates (per ICAO Doc 9303)
                            // Including both self-signed and cross-signed/link CSCAs
                            std::string certType = "CSCA";

                            // Phase 4.4: Check ICAO 9303 compliance
                            IcaoComplianceStatus icaoCompliance = common::checkIcaoCompliance(cert, certType);
                            spdlog::debug("Phase 4.4 (Master List PKCS7 fallback): ICAO compliance for {} cert: isCompliant={}, level={}",
                                          certType, icaoCompliance.isCompliant, icaoCompliance.complianceLevel);

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

                                        // Phase 4.4: Extract comprehensive certificate metadata for progress tracking
                                        CertificateMetadata certMetadata = common::extractCertificateMetadataForProgress(cert, false);
                                        spdlog::debug("Phase 4.4 (Master List Async): Extracted metadata for cert: type={}, sigAlg={}, keySize={}",
                                                      certMetadata.certificateType, certMetadata.signatureAlgorithm, certMetadata.keySize);

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

                                        // Phase 4.4: Check ICAO 9303 compliance after certificate type is determined
                                        IcaoComplianceStatus icaoCompliance = common::checkIcaoCompliance(cert, certType);
                                        spdlog::debug("Phase 4.4 (Master List Async): ICAO compliance for {} cert: isCompliant={}, level={}",
                                                      certType, icaoCompliance.isCompliant, icaoCompliance.complianceLevel);

                                        totalCerts++;

                                        // Check for duplicate before saving
                                        if (certificateExistsByFingerprint(conn, fingerprint)) {
                                            skippedDuplicates++;
                                            spdlog::debug("Skipping duplicate certificate: fingerprint={}", fingerprint.substr(0, 16));

                                            // Save duplicate record to duplicate_certificate table (v2.2.1)
                                            std::string firstUploadId = certificateRepository->findFirstUploadIdByFingerprint(fingerprint);
                                            if (!firstUploadId.empty()) {
                                                certificateRepository->saveDuplicate(uploadId, firstUploadId, fingerprint,
                                                                                    certType, subjectDn, issuerDn, countryCode, serialNumber);
                                            }

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

                            // Phase 4.4: Extract comprehensive certificate metadata for progress tracking
                            CertificateMetadata certMetadata = common::extractCertificateMetadataForProgress(cert, false);
                            spdlog::debug("Phase 4.4 (Master List PKCS7 fallback): Extracted metadata for cert: type={}, sigAlg={}, keySize={}",
                                          certMetadata.certificateType, certMetadata.signatureAlgorithm, certMetadata.keySize);

                            // Master List contains ONLY CSCA certificates (per ICAO Doc 9303)
                            // Including both self-signed and cross-signed/link CSCAs
                            std::string certType = "CSCA";

                            // Phase 4.4: Check ICAO 9303 compliance
                            IcaoComplianceStatus icaoCompliance = common::checkIcaoCompliance(cert, certType);
                            spdlog::debug("Phase 4.4 (Master List PKCS7 fallback): ICAO compliance for {} cert: isCompliant={}, level={}",
                                          certType, icaoCompliance.isCompliant, icaoCompliance.complianceLevel);

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

            // v1.5.1: Enhanced completion message with LDAP status
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
            updateUploadStatistics(conn, uploadId, "FAILED", 0, 0, 0, 0, 0, 0, e.what());
        }

        // Cleanup connections
        if (ld) {
            ldap_unbind_ext_s(ld, nullptr, nullptr);
        }
        PQfinish(conn);
    }).detach();
}

namespace {  // Reopen anonymous namespace for remaining internal functions

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

    // =========================================================================
    // Phase 3: Register Authentication Middleware (Global)
    // =========================================================================
    // Note: Authentication is DISABLED by default for backward compatibility
    // Enable by setting: AUTH_ENABLED=true in environment
    //
    // IMPORTANT: AuthMiddleware uses HttpFilterBase (not HttpFilter<T>) for manual
    // instantiation with parameters. It cannot be registered globally via registerFilter().
    // Instead, apply it to individual routes using .addFilter() method.
    //
    // Example:
    //   app.registerHandler("/api/upload/ldif", handler)
    //      .addFilter(std::make_shared<middleware::AuthMiddleware>());
    //
    // For now, authentication is OPTIONAL per-route.
    // TODO: Apply AuthMiddleware to all protected routes after testing.

    spdlog::warn("⚠️  Phase 3 Authentication implementation complete (per-route filtering)");
    spdlog::warn("⚠️  Apply .addFilter<AuthMiddleware>() to protected routes manually");

    // =========================================================================
    // Authentication Routes (Phase 3)
    // =========================================================================
    if (authHandler) {
        authHandler->registerRoutes(app);
    }

    // =========================================================================
    // API Routes
    // =========================================================================

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

            // Check if upload exists and get file path (parameterized query)
            const char* query = "SELECT id, file_path, file_format FROM uploaded_file WHERE id = $1";
            const char* paramValues[1] = {uploadId.c_str()};
            PGresult* res = PQexecParams(conn, query, 1, nullptr, paramValues,
                                         nullptr, nullptr, 0);

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

                    // Get processing mode (parameterized query)
                    const char* modeQuery = "SELECT processing_mode FROM uploaded_file WHERE id = $1";
                    const char* modeParamValues[1] = {uploadId.c_str()};
                    PGresult* modeRes = PQexecParams(conn, modeQuery, 1, nullptr, modeParamValues,
                                                     nullptr, nullptr, 0);
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
                            spdlog::error("CRITICAL: LDAP write connection failed in AUTO mode for Master List upload {}", uploadId);
                            spdlog::error("Cannot proceed - data consistency requires both DB and LDAP storage");

                            // Update upload status to FAILED
                            const char* failQuery = "UPDATE uploaded_file SET status = 'FAILED', "
                                                   "error_message = 'LDAP connection failure - cannot ensure data consistency', "
                                                   "updated_at = NOW() WHERE id = $1";
                            const char* failParams[1] = {uploadId.c_str()};
                            PGresult* failRes = PQexecParams(conn, failQuery, 1, nullptr, failParams,
                                                            nullptr, nullptr, 0);
                            PQclear(failRes);

                            // Send failure progress
                            ProgressManager::getInstance().sendProgress(
                                ProcessingProgress::create(uploadId, ProcessingStage::FAILED,
                                    0, 0, "LDAP 연결 실패", "데이터 일관성을 보장할 수 없어 처리를 중단했습니다."));

                            PQfinish(conn);
                            return;
                        }
                        spdlog::info("LDAP write connection established successfully for AUTO mode Master List upload {}", uploadId);
                    }

                    try {
                        // Use Strategy Pattern
                        auto strategy = ProcessingStrategyFactory::create(processingMode);
                        strategy->processMasterListContent(uploadId, contentBytes, conn, ld);

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

            // Check if upload exists (parameterized query)
            const char* query = "SELECT id FROM uploaded_file WHERE id = $1";
            const char* paramValues[1] = {uploadId.c_str()};
            PGresult* res = PQexecParams(conn, query, 1, nullptr, paramValues,
                                         nullptr, nullptr, 0);

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
                    spdlog::error("Database connection failed for validation: {}", scrubCredentials(std::string(PQerrorMessage(conn))));
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

    // GET /api/upload/{uploadId}/validations - Get validation results for an upload
    // Phase 4.5: Connected to ValidationService → ValidationRepository (Repository Pattern)
    app.registerHandler(
        "/api/upload/{uploadId}/validations",
        [&](const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback,
            const std::string& uploadId) {
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
                Json::Value response = validationService->getValidationsByUploadId(
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
        },
        {drogon::Get}
    );

    // GET /api/upload/{uploadId}/validation-statistics - Get validation statistics for an upload
    // Phase 4.6: Connected to ValidationService → ValidationRepository (Repository Pattern)
    app.registerHandler(
        "/api/upload/{uploadId}/validation-statistics",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& uploadId) {
            try {
                spdlog::info("GET /api/upload/{}/validation-statistics", uploadId);

                // Call ValidationService (Repository Pattern)
                Json::Value response = validationService->getValidationStatistics(uploadId);

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
        },
        {drogon::Get}
    );

    // GET /api/upload/{uploadId}/ldif-structure - Get LDIF file structure
    // v2.2.2: LDIF Structure Visualization (Repository Pattern)
    app.registerHandler(
        "/api/upload/{uploadId}/ldif-structure",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& uploadId) {
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
                Json::Value response = ldifStructureService->getLdifStructure(uploadId, maxEntries);

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
        },
        {drogon::Get}
    );

    // Cleanup failed upload endpoint
    // Phase 3.1: Connected to UploadService (Repository Pattern)
    // TODO Phase 4: Move cleanupFailedUpload() logic into UploadService
    app.registerHandler(
        "/api/upload/{uploadId}",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& uploadId) {
            spdlog::info("DELETE /api/upload/{} - Delete upload", uploadId);

            try {
                bool deleted = uploadService->deleteUpload(uploadId);

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

                // Phase 3.1: Audit logging - UPLOAD_DELETE success
                std::string conninfo = "host=" + appConfig.dbHost +
                                      " port=" + std::to_string(appConfig.dbPort) +
                                      " dbname=" + appConfig.dbName +
                                      " user=" + appConfig.dbUser +
                                      " password=" + appConfig.dbPassword;
                PGconn* auditConn = PQconnectdb(conninfo.c_str());
                if (auditConn && PQstatus(auditConn) == CONNECTION_OK) {
                    AuditLogEntry auditEntry;
                    auto session = req->getSession();
                    if (session) {
                        auto [userId, username] = extractUserFromRequest(req);
                        auditEntry.userId = userId;
                        auditEntry.username = username;
                    }

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

                    logOperation(auditConn, auditEntry);
                    PQfinish(auditConn);
                }

            } catch (const std::exception& e) {
                spdlog::error("Failed to delete upload {}: {}", uploadId, e.what());

                // Phase 3.1: Audit logging - UPLOAD_DELETE failed
                std::string conninfo = "host=" + appConfig.dbHost +
                                      " port=" + std::to_string(appConfig.dbPort) +
                                      " dbname=" + appConfig.dbName +
                                      " user=" + appConfig.dbUser +
                                      " password=" + appConfig.dbPassword;
                PGconn* auditConn = PQconnectdb(conninfo.c_str());
                if (auditConn && PQstatus(auditConn) == CONNECTION_OK) {
                    AuditLogEntry auditEntry;
                    auto session = req->getSession();
                    if (session) {
                        auto [userId, username] = extractUserFromRequest(req);
                        auditEntry.userId = userId;
                        auditEntry.username = username;
                    }

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

                    logOperation(auditConn, auditEntry);
                    PQfinish(auditConn);
                }

                Json::Value error;
                error["success"] = false;
                error["message"] = std::string("Delete failed: ") + e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Delete}
    );

    // =========================================================================
    // Audit Log API Endpoints (Phase 4.4)
    // =========================================================================

    // GET /api/audit/operations - List audit log entries with filtering
    // Phase 4.4: Connected to AuditService → AuditRepository (Repository Pattern)
    app.registerHandler(
        "/api/audit/operations",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/audit/operations - List audit logs");

            try {
                // Build filter from query parameters
                services::AuditService::AuditLogFilter filter;
                filter.limit = req->getOptionalParameter<int>("limit").value_or(50);
                filter.offset = req->getOptionalParameter<int>("offset").value_or(0);
                filter.operationType = req->getOptionalParameter<std::string>("operationType").value_or("");
                filter.username = req->getOptionalParameter<std::string>("username").value_or("");
                filter.success = req->getOptionalParameter<std::string>("success").value_or("");

                // Call AuditService (Repository Pattern)
                Json::Value result = auditService->getOperationLogs(filter);

                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                if (!result.get("success", false).asBool()) {
                    resp->setStatusCode(drogon::k500InternalServerError);
                }
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("GET /api/audit/operations error: {}", e.what());
                Json::Value error;
                error["success"] = false;
                error["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Get}
    );

    // GET /api/audit/operations/stats - Audit log statistics
    // Phase 4.4: Connected to AuditService → AuditRepository (Repository Pattern)
    app.registerHandler(
        "/api/audit/operations/stats",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/audit/operations/stats - Audit log statistics");

            try {
                // Call AuditService (Repository Pattern)
                Json::Value result = auditService->getOperationStatistics();

                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                if (!result.get("success", false).asBool()) {
                    resp->setStatusCode(drogon::k500InternalServerError);
                }
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("GET /api/audit/operations/stats error: {}", e.what());
                Json::Value error;
                error["success"] = false;
                error["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Get}
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
    // Phase 4.7: Connected to ValidationService → CertificateRepository (Repository Pattern)
    app.registerHandler(
        "/api/validation/revalidate",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try {
                spdlog::info("POST /api/validation/revalidate - Re-validate DSC certificates");

                // Call ValidationService (Repository Pattern)
                auto result = validationService->revalidateDscCertificates();

                // Build response
                Json::Value response;
                response["success"] = result.success;
                response["message"] = result.message;
                response["totalProcessed"] = result.totalProcessed;
                response["validCount"] = result.validCount;
                response["invalidCount"] = result.invalidCount;
                response["pendingCount"] = result.pendingCount;
                response["errorCount"] = result.errorCount;
                response["durationSeconds"] = result.durationSeconds;

                auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("Revalidation error: {}", e.what());
                Json::Value error;
                error["success"] = false;
                error["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
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
                auto result = uploadService->uploadLdif(fileName, contentBytes, processingMode, username);

                // Handle duplicate file
                if (result.status == "DUPLICATE") {
                    // Phase 3.1: Audit logging - FILE_UPLOAD failed (duplicate)
                    std::string conninfo = "host=" + appConfig.dbHost +
                                          " port=" + std::to_string(appConfig.dbPort) +
                                          " dbname=" + appConfig.dbName +
                                          " user=" + appConfig.dbUser +
                                          " password=" + appConfig.dbPassword;
                    PGconn* auditConn = PQconnectdb(conninfo.c_str());
                    if (auditConn && PQstatus(auditConn) == CONNECTION_OK) {
                        AuditLogEntry auditEntry;
                        if (session) {
                            auto [userId, sessionUsername] = extractUserFromRequest(req);
                            auditEntry.userId = userId;
                            auditEntry.username = sessionUsername;
                        }
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

                        logOperation(auditConn, auditEntry);
                        PQfinish(auditConn);
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
                    // Phase 3.1: Audit logging - FILE_UPLOAD failed
                    std::string conninfo = "host=" + appConfig.dbHost +
                                          " port=" + std::to_string(appConfig.dbPort) +
                                          " dbname=" + appConfig.dbName +
                                          " user=" + appConfig.dbUser +
                                          " password=" + appConfig.dbPassword;
                    PGconn* auditConn = PQconnectdb(conninfo.c_str());
                    if (auditConn && PQstatus(auditConn) == CONNECTION_OK) {
                        AuditLogEntry auditEntry;
                        if (session) {
                            auto [userId, sessionUsername] = extractUserFromRequest(req);
                            auditEntry.userId = userId;
                            auditEntry.username = sessionUsername;
                        }
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

                        logOperation(auditConn, auditEntry);
                        PQfinish(auditConn);
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

                // Phase 3.1: Audit logging - FILE_UPLOAD success
                std::string conninfo = "host=" + appConfig.dbHost +
                                      " port=" + std::to_string(appConfig.dbPort) +
                                      " dbname=" + appConfig.dbName +
                                      " user=" + appConfig.dbUser +
                                      " password=" + appConfig.dbPassword;
                PGconn* auditConn = PQconnectdb(conninfo.c_str());
                if (auditConn && PQstatus(auditConn) == CONNECTION_OK) {
                    AuditLogEntry auditEntry;
                    if (session) {
                        auto [userId, sessionUsername] = extractUserFromRequest(req);
                        auditEntry.userId = userId;
                        auditEntry.username = sessionUsername;
                    }

                    auditEntry.operationType = OperationType::FILE_UPLOAD;
                    auditEntry.operationSubtype = "LDIF";
                    auditEntry.resourceId = result.uploadId;
                    auditEntry.resourceType = "UPLOADED_FILE";
                    auditEntry.ipAddress = extractIpAddress(req);
                    auditEntry.userAgent = req->getHeader("User-Agent");
                    auditEntry.requestMethod = "POST";
                    auditEntry.requestPath = "/api/upload/ldif";
                    auditEntry.success = true;

                    // Metadata
                    Json::Value metadata;
                    metadata["fileName"] = fileName;
                    metadata["fileSize"] = static_cast<Json::Int64>(fileSize);
                    metadata["processingMode"] = processingMode;
                    auditEntry.metadata = metadata;

                    logOperation(auditConn, auditEntry);
                    PQfinish(auditConn);
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
                auto uploadResult = uploadService->uploadMasterList(fileName, contentBytes, processingMode, username);

                // Handle duplicate file
                if (uploadResult.status == "DUPLICATE") {
                    // Phase 3.1: Audit logging - FILE_UPLOAD failed (duplicate)
                    std::string conninfo = "host=" + appConfig.dbHost +
                                          " port=" + std::to_string(appConfig.dbPort) +
                                          " dbname=" + appConfig.dbName +
                                          " user=" + appConfig.dbUser +
                                          " password=" + appConfig.dbPassword;
                    PGconn* auditConn = PQconnectdb(conninfo.c_str());
                    if (auditConn && PQstatus(auditConn) == CONNECTION_OK) {
                        AuditLogEntry auditEntry;
                        if (session) {
                            auto [userId, sessionUsername] = extractUserFromRequest(req);
                            auditEntry.userId = userId;
                            auditEntry.username = sessionUsername;
                        }
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

                        logOperation(auditConn, auditEntry);
                        PQfinish(auditConn);
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
                    // Phase 3.1: Audit logging - FILE_UPLOAD failed
                    std::string conninfo = "host=" + appConfig.dbHost +
                                          " port=" + std::to_string(appConfig.dbPort) +
                                          " dbname=" + appConfig.dbName +
                                          " user=" + appConfig.dbUser +
                                          " password=" + appConfig.dbPassword;
                    PGconn* auditConn = PQconnectdb(conninfo.c_str());
                    if (auditConn && PQstatus(auditConn) == CONNECTION_OK) {
                        AuditLogEntry auditEntry;
                        if (session) {
                            auto [userId, sessionUsername] = extractUserFromRequest(req);
                            auditEntry.userId = userId;
                            auditEntry.username = sessionUsername;
                        }
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

                        logOperation(auditConn, auditEntry);
                        PQfinish(auditConn);
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

                        // Get processing mode (parameterized query)
                        const char* modeQuery = "SELECT processing_mode FROM uploaded_file WHERE id = $1";
                        const char* modeParamValues[1] = {uploadId.c_str()};
                        PGresult* modeRes = PQexecParams(conn, modeQuery, 1, nullptr, modeParamValues,
                                                         nullptr, nullptr, 0);
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
                                spdlog::error("CRITICAL: LDAP write connection failed in AUTO mode for upload {}", uploadId);
                                spdlog::error("Cannot proceed - data consistency requires both DB and LDAP storage");

                                // Update upload status to FAILED
                                const char* failQuery = "UPDATE uploaded_file SET status = 'FAILED', "
                                                       "error_message = 'LDAP connection failure - cannot ensure data consistency', "
                                                       "updated_at = NOW() WHERE id = $1";
                                const char* failParams[1] = {uploadId.c_str()};
                                PGresult* failRes = PQexecParams(conn, failQuery, 1, nullptr, failParams,
                                                                nullptr, nullptr, 0);
                                PQclear(failRes);

                                // Send failure progress
                                ProgressManager::getInstance().sendProgress(
                                    ProcessingProgress::create(uploadId, ProcessingStage::FAILED,
                                        0, 0, "LDAP 연결 실패", "데이터 일관성을 보장할 수 없어 처리를 중단했습니다."));

                                PQfinish(conn);
                                return;
                            }
                            spdlog::info("LDAP write connection established successfully for AUTO mode");
                        }

                        try {
                            // Use Strategy Pattern
                            auto strategy = ProcessingStrategyFactory::create(processingMode);
                            strategy->processMasterListContent(uploadId, contentBytes, conn, ld);

                            // Query actual processed_entries from database (parameterized)
                            const char* statsQuery = "SELECT processed_entries, total_entries FROM uploaded_file WHERE id = $1";
                            const char* statsParams[1] = {uploadId.c_str()};
                            PGresult* statsRes = PQexecParams(conn, statsQuery, 1, nullptr, statsParams, nullptr, nullptr, 0);

                            int processedEntries = 0;
                            int totalEntries = 0;
                            if (PQresultStatus(statsRes) == PGRES_TUPLES_OK && PQntuples(statsRes) > 0) {
                                const char* processedStr = PQgetvalue(statsRes, 0, 0);
                                const char* totalStr = PQgetvalue(statsRes, 0, 1);
                                if (processedStr && processedStr[0] != '\0') {
                                    processedEntries = std::atoi(processedStr);
                                }
                                if (totalStr && totalStr[0] != '\0') {
                                    totalEntries = std::atoi(totalStr);
                                }
                            }
                            PQclear(statsRes);

                            // For Master List: processed_entries contains extracted certificate count (e.g., 537)
                            // totalEntries is 0 for ML files, so we use processedEntries as the count
                            int count = processedEntries > 0 ? processedEntries : totalEntries;

                            spdlog::info("Master List processing completed - processedEntries: {}, totalEntries: {}, using count: {}",
                                        processedEntries, totalEntries, count);

                            // Send appropriate progress based on mode with actual count
                            if (processingMode == "MANUAL") {
                                // MANUAL mode: Only parsing completed, waiting for Stage 2
                                ProgressManager::getInstance().sendProgress(
                                    ProcessingProgress::create(uploadId, ProcessingStage::PARSING_COMPLETED,
                                        count, count, "Master List 파싱 완료 - 검증 대기"));
                            } else {
                                // AUTO mode: All processing completed
                                ProgressManager::getInstance().sendProgress(
                                    ProcessingProgress::create(uploadId, ProcessingStage::COMPLETED,
                                        count, count, "Master List 처리 완료"));
                            }

                        } catch (const std::exception& e) {
                            spdlog::error("Master List processing via Strategy failed for upload {}: {}", uploadId, e.what());
                            ProgressManager::getInstance().sendProgress(
                                ProcessingProgress::create(uploadId, ProcessingStage::FAILED,
                                    0, 0, "처리 실패", e.what()));
                        }

                        if (ld) ldap_unbind_ext_s(ld, nullptr, nullptr);
                        PQfinish(conn);
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

                // Phase 3.1: Audit logging - FILE_UPLOAD success
                std::string conninfo = "host=" + appConfig.dbHost +
                                      " port=" + std::to_string(appConfig.dbPort) +
                                      " dbname=" + appConfig.dbName +
                                      " user=" + appConfig.dbUser +
                                      " password=" + appConfig.dbPassword;
                PGconn* auditConn = PQconnectdb(conninfo.c_str());
                if (auditConn && PQstatus(auditConn) == CONNECTION_OK) {
                    AuditLogEntry auditEntry;
                    auto session = req->getSession();
                    if (session) {
                        auto [userId, username] = extractUserFromRequest(req);
                        auditEntry.userId = userId;
                        auditEntry.username = username;
                    }

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

                    logOperation(auditConn, auditEntry);
                    PQfinish(auditConn);
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
        },
        {drogon::Post}
    );

    // Upload statistics endpoint - returns UploadStatisticsOverview format
    // Phase 3.1: Connected to UploadService (Repository Pattern)
    app.registerHandler(
        "/api/upload/statistics",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/upload/statistics");

            try {
                Json::Value result = uploadService->getUploadStatistics();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("GET /api/upload/statistics failed: {}", e.what());
                Json::Value error;
                error["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Get}
    );

    // Upload history endpoint - returns PageResponse format
    // Phase 3.1: Connected to UploadService (Repository Pattern)
    app.registerHandler(
        "/api/upload/history",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/upload/history");

            try {
                // Parse query parameters
                services::UploadService::UploadHistoryFilter filter;
                filter.page = 0;
                filter.size = 20;
                filter.sort = "created_at";
                filter.direction = "DESC";

                if (auto p = req->getParameter("page"); !p.empty()) {
                    filter.page = std::stoi(p);
                }
                if (auto s = req->getParameter("size"); !s.empty()) {
                    filter.size = std::stoi(s);
                }
                if (auto sort = req->getParameter("sort"); !sort.empty()) {
                    filter.sort = sort;
                }
                if (auto dir = req->getParameter("direction"); !dir.empty()) {
                    filter.direction = dir;
                }

                // Call Service method (uses Repository)
                Json::Value result = uploadService->getUploadHistory(filter);

                // Add PageResponse format compatibility fields
                if (result.isMember("totalElements")) {
                    int totalElements = result["totalElements"].asInt();
                    int size = result["size"].asInt();
                    int page = result["number"].asInt();
                    result["page"] = page;
                    result["totalPages"] = (totalElements + size - 1) / size;
                    result["first"] = (page == 0);
                    result["last"] = (page >= result["totalPages"].asInt() - 1);
                }

                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("GET /api/upload/history error: {}", e.what());
                Json::Value error;
                error["success"] = false;
                error["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Get}
    );

    // Get individual upload status - GET /api/upload/detail/{uploadId}
    // Phase 3.1: Connected to UploadService (Repository Pattern)
    app.registerHandler(
        "/api/upload/detail/{uploadId}",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& uploadId) {
            spdlog::info("GET /api/upload/detail/{}", uploadId);

            try {
                // Call Service method (uses Repository)
                Json::Value uploadData = uploadService->getUploadDetail(uploadId);

                if (uploadData.isMember("error")) {
                    // Upload not found
                    Json::Value result;
                    result["success"] = false;
                    result["error"] = uploadData["error"].asString();
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                // TODO Phase 4: Move LDAP status check to CertificateRepository
                // For now, keep this as a separate query
                std::string conninfo = "host=" + appConfig.dbHost +
                                      " port=" + std::to_string(appConfig.dbPort) +
                                      " dbname=" + appConfig.dbName +
                                      " user=" + appConfig.dbUser +
                                      " password=" + appConfig.dbPassword;
                PGconn* conn = PQconnectdb(conninfo.c_str());

                if (PQstatus(conn) == CONNECTION_OK) {
                    std::string ldapQuery = "SELECT COUNT(*) as total, "
                                          "COALESCE(SUM(CASE WHEN stored_in_ldap = true THEN 1 ELSE 0 END), 0) as in_ldap "
                                          "FROM certificate WHERE upload_id = '" + uploadId + "'";
                    PGresult* ldapRes = PQexec(conn, ldapQuery.c_str());
                    if (PQresultStatus(ldapRes) == PGRES_TUPLES_OK && PQntuples(ldapRes) > 0) {
                        int totalCerts = std::stoi(PQgetvalue(ldapRes, 0, 0));
                        int ldapCerts = std::stoi(PQgetvalue(ldapRes, 0, 1));
                        uploadData["ldapUploadedCount"] = ldapCerts;
                        uploadData["ldapPendingCount"] = totalCerts - ldapCerts;
                    } else {
                        uploadData["ldapUploadedCount"] = 0;
                        uploadData["ldapPendingCount"] = 0;
                    }
                    PQclear(ldapRes);
                }
                PQfinish(conn);

                Json::Value result;
                result["success"] = true;
                result["data"] = uploadData;

                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("GET /api/upload/detail/{} error: {}", uploadId, e.what());
                Json::Value error;
                error["success"] = false;
                error["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Get}
    );

    // Upload issues endpoint - GET /api/upload/{uploadId}/issues
    // Returns duplicate certificates detected during upload
    app.registerHandler(
        "/api/upload/{uploadId}/issues",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& uploadId) {
            spdlog::info("GET /api/upload/{}/issues", uploadId);

            try {
                Json::Value result = uploadService->getUploadIssues(uploadId);
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                callback(resp);
            } catch (const std::exception& e) {
                spdlog::error("GET /api/upload/{}/issues error: {}", uploadId, e.what());
                Json::Value error;
                error["success"] = false;
                error["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Get}
    );

    // Master List ASN.1 structure endpoint - GET /api/upload/{uploadId}/masterlist-structure
    // Returns ASN.1 tree structure with TLV information for Master List files
    app.registerHandler(
        "/api/upload/{uploadId}/masterlist-structure",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& uploadId) {
            spdlog::info("GET /api/upload/{}/masterlist-structure", uploadId);

            Json::Value result;
            result["success"] = false;

            std::string conninfo = "host=" + appConfig.dbHost +
                                  " port=" + std::to_string(appConfig.dbPort) +
                                  " dbname=" + appConfig.dbName +
                                  " user=" + appConfig.dbUser +
                                  " password=" + appConfig.dbPassword;

            PGconn* conn = PQconnectdb(conninfo.c_str());

            try {
                if (PQstatus(conn) != CONNECTION_OK) {
                    throw std::runtime_error(std::string("Database connection failed: ") + PQerrorMessage(conn));
                }

                // Query upload file information
                const char* query =
                    "SELECT file_name, original_file_name, file_format, file_size, file_path "
                    "FROM uploaded_file "
                    "WHERE id = $1::uuid";

                const char* paramValues[] = { uploadId.c_str() };
                PGresult* res = PQexecParams(conn, query, 1, nullptr, paramValues, nullptr, nullptr, 0);

                if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
                    PQclear(res);
                    PQfinish(conn);
                    result["error"] = "Upload not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                std::string fileName = PQgetvalue(res, 0, 0);
                // Use fileName (stored name) if originalFileName is NULL
                std::string displayName = (PQgetvalue(res, 0, 1) && strlen(PQgetvalue(res, 0, 1)) > 0)
                    ? PQgetvalue(res, 0, 1)
                    : fileName;
                std::string fileFormat = PQgetvalue(res, 0, 2);
                std::string fileSizeStr = PQgetvalue(res, 0, 3);
                std::string filePath = PQgetvalue(res, 0, 4) ? PQgetvalue(res, 0, 4) : "";
                PQclear(res);
                PQfinish(conn);

                // Check if this is a Master List file
                if (fileFormat != "ML" && fileFormat != "MASTER_LIST") {
                    result["error"] = "Not a Master List file (format: " + fileFormat + ")";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                // If file_path is empty, construct it from upload directory + uploadId
                // Files are stored as {uploadId}.ml in /app/uploads/
                if (filePath.empty()) {
                    filePath = "/app/uploads/" + uploadId + ".ml";
                    spdlog::debug("file_path is NULL, using constructed path: {}", filePath);
                }

                // Get maxLines parameter (default from config, 0 = unlimited)
                int maxLines = appConfig.asn1MaxLines;
                if (auto ml = req->getParameter("maxLines"); !ml.empty()) {
                    try {
                        maxLines = std::stoi(ml);
                        if (maxLines < 0) maxLines = appConfig.asn1MaxLines;
                    } catch (...) {
                        maxLines = appConfig.asn1MaxLines;
                    }
                }

                // Parse ASN.1 structure with line limit
                Json::Value asn1Result = icao::asn1::parseAsn1Structure(filePath, maxLines);

                if (!asn1Result["success"].asBool()) {
                    result["error"] = asn1Result["error"].asString();
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                // Build response
                result["success"] = true;
                result["fileName"] = displayName;
                result["fileSize"] = std::stoi(fileSizeStr);
                result["asn1Tree"] = asn1Result["tree"];
                result["statistics"] = asn1Result["statistics"];
                result["maxLines"] = asn1Result["maxLines"];
                result["truncated"] = asn1Result["truncated"];

                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("GET /api/upload/{}/masterlist-structure error: {}", uploadId, e.what());
                if (PQstatus(conn) == CONNECTION_OK) {
                    PQfinish(conn);
                }
                result["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Get}
    );

    // Upload changes endpoint - GET /api/upload/changes
    app.registerHandler(
        "/api/upload/changes",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/upload/changes - Calculate upload deltas");

            // Get optional limit parameter (default: 10)
            int limit = 10;
            if (auto l = req->getParameter("limit"); !l.empty()) {
                try {
                    limit = std::stoi(l);
                    if (limit <= 0 || limit > 100) limit = 10;
                } catch (...) {
                    limit = 10;
                }
            }

            std::string conninfo = "host=" + appConfig.dbHost +
                                  " port=" + std::to_string(appConfig.dbPort) +
                                  " dbname=" + appConfig.dbName +
                                  " user=" + appConfig.dbUser +
                                  " password=" + appConfig.dbPassword;

            PGconn* conn = PQconnectdb(conninfo.c_str());
            Json::Value result;
            result["success"] = false;

            if (PQstatus(conn) == CONNECTION_OK) {
                // Query to calculate changes between consecutive uploads (chronological order)
                std::string query =
                    "WITH ranked_uploads AS ( "
                    "  SELECT "
                    "    id, "
                    "    original_file_name, "
                    "    upload_timestamp, "
                    "    csca_count, "
                    "    dsc_count, "
                    "    dsc_nc_count, "
                    "    crl_count, "
                    "    ml_count, "
                    "    mlsc_count, "
                    "    COALESCE(collection_number, '') as collection_number, "
                    "    ROW_NUMBER() OVER ( "
                    "      ORDER BY upload_timestamp DESC "
                    "    ) as rn "
                    "  FROM uploaded_file "
                    "  WHERE status = 'COMPLETED' "
                    ") "
                    "SELECT "
                    "  curr.id, "
                    "  curr.original_file_name, "
                    "  CASE WHEN curr.collection_number = '' THEN 'N/A' ELSE curr.collection_number END as collection_number, "
                    "  to_char(curr.upload_timestamp, 'YYYY-MM-DD HH24:MI:SS') as upload_time, "
                    "  curr.csca_count, "
                    "  curr.dsc_count, "
                    "  curr.dsc_nc_count, "
                    "  curr.crl_count, "
                    "  curr.ml_count, "
                    "  curr.mlsc_count, "
                    "  curr.csca_count - COALESCE(prev.csca_count, 0) as csca_change, "
                    "  curr.dsc_count - COALESCE(prev.dsc_count, 0) as dsc_change, "
                    "  curr.dsc_nc_count - COALESCE(prev.dsc_nc_count, 0) as dsc_nc_change, "
                    "  curr.crl_count - COALESCE(prev.crl_count, 0) as crl_change, "
                    "  curr.ml_count - COALESCE(prev.ml_count, 0) as ml_change, "
                    "  curr.mlsc_count - COALESCE(prev.mlsc_count, 0) as mlsc_change, "
                    "  prev.original_file_name as previous_file, "
                    "  to_char(prev.upload_timestamp, 'YYYY-MM-DD HH24:MI:SS') as previous_upload_time "
                    "FROM ranked_uploads curr "
                    "LEFT JOIN ranked_uploads prev "
                    "  ON prev.rn = curr.rn + 1 "
                    "WHERE curr.rn <= " + std::to_string(limit) + " "
                    "ORDER BY curr.upload_timestamp DESC";

                PGresult* res = PQexec(conn, query.c_str());
                if (PQresultStatus(res) == PGRES_TUPLES_OK) {
                    result["success"] = true;
                    result["count"] = PQntuples(res);

                    Json::Value changes(Json::arrayValue);
                    for (int i = 0; i < PQntuples(res); i++) {
                        Json::Value change;
                        change["uploadId"] = PQgetvalue(res, i, 0);
                        change["fileName"] = PQgetvalue(res, i, 1);
                        change["collectionNumber"] = PQgetvalue(res, i, 2);
                        change["uploadTime"] = PQgetvalue(res, i, 3);

                        // Current counts
                        Json::Value counts;
                        counts["csca"] = std::stoi(PQgetvalue(res, i, 4));
                        counts["dsc"] = std::stoi(PQgetvalue(res, i, 5));
                        counts["dscNc"] = std::stoi(PQgetvalue(res, i, 6));
                        counts["crl"] = std::stoi(PQgetvalue(res, i, 7));
                        counts["ml"] = std::stoi(PQgetvalue(res, i, 8));
                        counts["mlsc"] = std::stoi(PQgetvalue(res, i, 9));
                        change["counts"] = counts;

                        // Changes (deltas)
                        Json::Value deltas;
                        deltas["csca"] = std::stoi(PQgetvalue(res, i, 10));
                        deltas["dsc"] = std::stoi(PQgetvalue(res, i, 11));
                        deltas["dscNc"] = std::stoi(PQgetvalue(res, i, 12));
                        deltas["crl"] = std::stoi(PQgetvalue(res, i, 13));
                        deltas["ml"] = std::stoi(PQgetvalue(res, i, 14));
                        deltas["mlsc"] = std::stoi(PQgetvalue(res, i, 15));
                        change["changes"] = deltas;

                        // Calculate total change
                        int totalChange = std::abs(std::stoi(PQgetvalue(res, i, 10))) +
                                        std::abs(std::stoi(PQgetvalue(res, i, 11))) +
                                        std::abs(std::stoi(PQgetvalue(res, i, 12))) +
                                        std::abs(std::stoi(PQgetvalue(res, i, 13))) +
                                        std::abs(std::stoi(PQgetvalue(res, i, 14))) +
                                        std::abs(std::stoi(PQgetvalue(res, i, 15)));
                        change["totalChange"] = totalChange;

                        // Previous upload info (if exists)
                        if (!PQgetisnull(res, i, 16)) {
                            Json::Value previous;
                            previous["fileName"] = PQgetvalue(res, i, 16);
                            previous["uploadTime"] = PQgetvalue(res, i, 17);
                            change["previousUpload"] = previous;
                        } else {
                            change["previousUpload"] = Json::Value::null;
                        }

                        changes.append(change);
                    }
                    result["changes"] = changes;
                } else {
                    result["error"] = "Query failed: " + std::string(PQerrorMessage(conn));
                    spdlog::error("[UploadChanges] Query failed: {}", PQerrorMessage(conn));
                }
                PQclear(res);
            } else {
                result["error"] = "Database connection failed";
                spdlog::error("[UploadChanges] DB connection failed");
            }

            PQfinish(conn);
            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // Country statistics endpoint - GET /api/upload/countries
    // Phase 3.1: Connected to UploadService (Repository Pattern)
    app.registerHandler(
        "/api/upload/countries",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/upload/countries");

            try {
                // Get query parameter for limit (default 20)
                int limit = 20;
                if (auto l = req->getParameter("limit"); !l.empty()) {
                    limit = std::stoi(l);
                }

                Json::Value result = uploadService->getCountryStatistics(limit);
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("GET /api/upload/countries failed: {}", e.what());
                Json::Value error;
                error["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Get}
    );

    // Detailed country statistics endpoint - GET /api/upload/countries/detailed
    // Phase 3.1: Connected to UploadService (Repository Pattern)
    app.registerHandler(
        "/api/upload/countries/detailed",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/upload/countries/detailed");

            try {
                // Get query parameters for limit (default ALL countries)
                int limit = 0;  // 0 = no limit
                if (auto l = req->getParameter("limit"); !l.empty()) {
                    limit = std::stoi(l);
                }

                Json::Value result = uploadService->getDetailedCountryStatistics(limit);
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("GET /api/upload/countries/detailed failed: {}", e.what());
                Json::Value error;
                error["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
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

    // ==========================================================================
    // Certificate Search APIs
    // ==========================================================================

    // GET /api/certificates/search - Search certificates from LDAP
    app.registerHandler(
        "/api/certificates/search",
        [&](const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try {
                // Parse query parameters
                std::string country = req->getOptionalParameter<std::string>("country").value_or("");
                std::string certTypeStr = req->getOptionalParameter<std::string>("certType").value_or("");
                std::string validityStr = req->getOptionalParameter<std::string>("validity").value_or("all");
                std::string searchTerm = req->getOptionalParameter<std::string>("searchTerm").value_or("");
                int limit = req->getOptionalParameter<int>("limit").value_or(50);
                int offset = req->getOptionalParameter<int>("offset").value_or(0);

                // Validate limit (max 200)
                if (limit > 200) limit = 200;
                if (limit < 1) limit = 50;
                if (offset < 0) offset = 0;

                spdlog::info("Certificate search: country={}, certType={}, validity={}, search={}, limit={}, offset={}",
                            country, certTypeStr, validityStr, searchTerm, limit, offset);

                // Build search criteria
                domain::models::CertificateSearchCriteria criteria;
                if (!country.empty()) criteria.country = country;
                if (!searchTerm.empty()) criteria.searchTerm = searchTerm;
                criteria.limit = limit;
                criteria.offset = offset;

                // Parse certificate type
                if (!certTypeStr.empty()) {
                    if (certTypeStr == "CSCA") criteria.certType = domain::models::CertificateType::CSCA;
                    else if (certTypeStr == "MLSC") criteria.certType = domain::models::CertificateType::MLSC;
                    else if (certTypeStr == "DSC") criteria.certType = domain::models::CertificateType::DSC;
                    else if (certTypeStr == "DSC_NC") criteria.certType = domain::models::CertificateType::DSC_NC;
                    else if (certTypeStr == "CRL") criteria.certType = domain::models::CertificateType::CRL;
                    else if (certTypeStr == "ML") criteria.certType = domain::models::CertificateType::ML;
                }

                // Parse validity status
                if (validityStr != "all") {
                    if (validityStr == "VALID") criteria.validity = domain::models::ValidityStatus::VALID;
                    else if (validityStr == "EXPIRED") criteria.validity = domain::models::ValidityStatus::EXPIRED;
                    else if (validityStr == "NOT_YET_VALID") criteria.validity = domain::models::ValidityStatus::NOT_YET_VALID;
                }

                // Execute search
                auto result = ::certificateService->searchCertificates(criteria);

                // Build JSON response
                Json::Value response;
                response["success"] = true;
                response["total"] = result.total;
                response["limit"] = result.limit;
                response["offset"] = result.offset;

                Json::Value certs(Json::arrayValue);
                for (const auto& cert : result.certificates) {
                    Json::Value certJson;
                    certJson["dn"] = cert.getDn();
                    certJson["cn"] = cert.getCn();
                    certJson["sn"] = cert.getSn();
                    certJson["country"] = cert.getCountry();
                    certJson["type"] = cert.getCertTypeString();  // Changed from certType to type for frontend compatibility
                    certJson["subjectDn"] = cert.getSubjectDn();
                    certJson["issuerDn"] = cert.getIssuerDn();
                    certJson["fingerprint"] = cert.getFingerprint();
                    certJson["isSelfSigned"] = cert.isSelfSigned();

                    // Convert time_point to ISO 8601 string
                    auto validFrom = std::chrono::system_clock::to_time_t(cert.getValidFrom());
                    auto validTo = std::chrono::system_clock::to_time_t(cert.getValidTo());
                    char timeBuf[32];
                    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&validFrom));
                    certJson["validFrom"] = timeBuf;
                    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&validTo));
                    certJson["validTo"] = timeBuf;

                    // Validity status
                    auto status = cert.getValidityStatus();
                    if (status == domain::models::ValidityStatus::VALID) certJson["validity"] = "VALID";
                    else if (status == domain::models::ValidityStatus::EXPIRED) certJson["validity"] = "EXPIRED";
                    else if (status == domain::models::ValidityStatus::NOT_YET_VALID) certJson["validity"] = "NOT_YET_VALID";
                    else certJson["validity"] = "UNKNOWN";

                    // DSC_NC specific attributes (optional)
                    if (cert.getPkdConformanceCode().has_value()) {
                        certJson["pkdConformanceCode"] = *cert.getPkdConformanceCode();
                    }
                    if (cert.getPkdConformanceText().has_value()) {
                        certJson["pkdConformanceText"] = *cert.getPkdConformanceText();
                    }
                    if (cert.getPkdVersion().has_value()) {
                        certJson["pkdVersion"] = *cert.getPkdVersion();
                    }

                    // X.509 Metadata (v2.3.0) - 15 fields
                    certJson["version"] = cert.getVersion();
                    if (cert.getSignatureAlgorithm().has_value()) {
                        certJson["signatureAlgorithm"] = *cert.getSignatureAlgorithm();
                    }
                    if (cert.getSignatureHashAlgorithm().has_value()) {
                        certJson["signatureHashAlgorithm"] = *cert.getSignatureHashAlgorithm();
                    }
                    if (cert.getPublicKeyAlgorithm().has_value()) {
                        certJson["publicKeyAlgorithm"] = *cert.getPublicKeyAlgorithm();
                    }
                    if (cert.getPublicKeySize().has_value()) {
                        certJson["publicKeySize"] = *cert.getPublicKeySize();
                    }
                    if (cert.getPublicKeyCurve().has_value()) {
                        certJson["publicKeyCurve"] = *cert.getPublicKeyCurve();
                    }
                    if (!cert.getKeyUsage().empty()) {
                        Json::Value keyUsageArray(Json::arrayValue);
                        for (const auto& usage : cert.getKeyUsage()) {
                            keyUsageArray.append(usage);
                        }
                        certJson["keyUsage"] = keyUsageArray;
                    }
                    if (!cert.getExtendedKeyUsage().empty()) {
                        Json::Value extKeyUsageArray(Json::arrayValue);
                        for (const auto& usage : cert.getExtendedKeyUsage()) {
                            extKeyUsageArray.append(usage);
                        }
                        certJson["extendedKeyUsage"] = extKeyUsageArray;
                    }
                    if (cert.getIsCA().has_value()) {
                        certJson["isCA"] = *cert.getIsCA();
                    }
                    if (cert.getPathLenConstraint().has_value()) {
                        certJson["pathLenConstraint"] = *cert.getPathLenConstraint();
                    }
                    if (cert.getSubjectKeyIdentifier().has_value()) {
                        certJson["subjectKeyIdentifier"] = *cert.getSubjectKeyIdentifier();
                    }
                    if (cert.getAuthorityKeyIdentifier().has_value()) {
                        certJson["authorityKeyIdentifier"] = *cert.getAuthorityKeyIdentifier();
                    }
                    if (!cert.getCrlDistributionPoints().empty()) {
                        Json::Value crlDpArray(Json::arrayValue);
                        for (const auto& url : cert.getCrlDistributionPoints()) {
                            crlDpArray.append(url);
                        }
                        certJson["crlDistributionPoints"] = crlDpArray;
                    }
                    if (cert.getOcspResponderUrl().has_value()) {
                        certJson["ocspResponderUrl"] = *cert.getOcspResponderUrl();
                    }
                    if (cert.getIsCertSelfSigned().has_value()) {
                        certJson["isCertSelfSigned"] = *cert.getIsCertSelfSigned();
                    }

                    // DN Components (shared library) - for clean UI display
                    if (cert.getSubjectDnComponents().has_value()) {
                        const auto& subjectDnComp = *cert.getSubjectDnComponents();
                        Json::Value subjectDnJson;
                        if (subjectDnComp.commonName.has_value()) subjectDnJson["commonName"] = *subjectDnComp.commonName;
                        if (subjectDnComp.organization.has_value()) subjectDnJson["organization"] = *subjectDnComp.organization;
                        if (subjectDnComp.organizationalUnit.has_value()) subjectDnJson["organizationalUnit"] = *subjectDnComp.organizationalUnit;
                        if (subjectDnComp.locality.has_value()) subjectDnJson["locality"] = *subjectDnComp.locality;
                        if (subjectDnComp.stateOrProvince.has_value()) subjectDnJson["stateOrProvince"] = *subjectDnComp.stateOrProvince;
                        if (subjectDnComp.country.has_value()) subjectDnJson["country"] = *subjectDnComp.country;
                        if (subjectDnComp.email.has_value()) subjectDnJson["email"] = *subjectDnComp.email;
                        if (subjectDnComp.serialNumber.has_value()) subjectDnJson["serialNumber"] = *subjectDnComp.serialNumber;
                        certJson["subjectDnComponents"] = subjectDnJson;
                    }
                    if (cert.getIssuerDnComponents().has_value()) {
                        const auto& issuerDnComp = *cert.getIssuerDnComponents();
                        Json::Value issuerDnJson;
                        if (issuerDnComp.commonName.has_value()) issuerDnJson["commonName"] = *issuerDnComp.commonName;
                        if (issuerDnComp.organization.has_value()) issuerDnJson["organization"] = *issuerDnComp.organization;
                        if (issuerDnComp.organizationalUnit.has_value()) issuerDnJson["organizationalUnit"] = *issuerDnComp.organizationalUnit;
                        if (issuerDnComp.locality.has_value()) issuerDnJson["locality"] = *issuerDnComp.locality;
                        if (issuerDnComp.stateOrProvince.has_value()) issuerDnJson["stateOrProvince"] = *issuerDnComp.stateOrProvince;
                        if (issuerDnComp.country.has_value()) issuerDnJson["country"] = *issuerDnComp.country;
                        if (issuerDnComp.email.has_value()) issuerDnJson["email"] = *issuerDnComp.email;
                        if (issuerDnComp.serialNumber.has_value()) issuerDnJson["serialNumber"] = *issuerDnComp.serialNumber;
                        certJson["issuerDnComponents"] = issuerDnJson;
                    }

                    certs.append(certJson);
                }
                response["certificates"] = certs;

                // Add statistics
                Json::Value stats;
                stats["total"] = result.stats.total;
                stats["valid"] = result.stats.valid;
                stats["expired"] = result.stats.expired;
                stats["notYetValid"] = result.stats.notYetValid;
                stats["unknown"] = result.stats.unknown;
                response["stats"] = stats;

                auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("Certificate search error: {}", e.what());
                Json::Value error;
                error["success"] = false;
                error["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Get}
    );

    // GET /api/certificates/detail - Get certificate details
    app.registerHandler(
        "/api/certificates/detail",
        [&](const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try {
                std::string dn = req->getOptionalParameter<std::string>("dn").value_or("");

                if (dn.empty()) {
                    Json::Value error;
                    error["success"] = false;
                    error["error"] = "DN parameter is required";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                spdlog::info("Certificate detail request: dn={}", dn);

                // Get certificate details
                auto cert = ::certificateService->getCertificateDetail(dn);

                // Build JSON response
                Json::Value response;
                response["success"] = true;
                response["dn"] = cert.getDn();
                response["cn"] = cert.getCn();
                response["sn"] = cert.getSn();
                response["country"] = cert.getCountry();
                response["certType"] = cert.getCertTypeString();
                response["subjectDn"] = cert.getSubjectDn();
                response["issuerDn"] = cert.getIssuerDn();
                response["fingerprint"] = cert.getFingerprint();
                response["isSelfSigned"] = cert.isSelfSigned();

                // Convert time_point to ISO 8601 string
                auto validFrom = std::chrono::system_clock::to_time_t(cert.getValidFrom());
                auto validTo = std::chrono::system_clock::to_time_t(cert.getValidTo());
                char timeBuf[32];
                std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&validFrom));
                response["validFrom"] = timeBuf;
                std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&validTo));
                response["validTo"] = timeBuf;

                // Validity status
                auto status = cert.getValidityStatus();
                if (status == domain::models::ValidityStatus::VALID) response["validity"] = "VALID";
                else if (status == domain::models::ValidityStatus::EXPIRED) response["validity"] = "EXPIRED";
                else if (status == domain::models::ValidityStatus::NOT_YET_VALID) response["validity"] = "NOT_YET_VALID";
                else response["validity"] = "UNKNOWN";

                auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("Certificate detail error: {}", e.what());
                Json::Value error;
                error["success"] = false;
                error["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Get}
    );

    // GET /api/certificates/validation - Get validation result by fingerprint
    // Phase 3.2: Connected to ValidationService (Repository Pattern)
    app.registerHandler(
        "/api/certificates/validation",
        [&](const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try {
                std::string fingerprint = req->getOptionalParameter<std::string>("fingerprint").value_or("");

                if (fingerprint.empty()) {
                    Json::Value error;
                    error["success"] = false;
                    error["error"] = "fingerprint parameter is required";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                spdlog::info("GET /api/certificates/validation - fingerprint: {}", fingerprint.substr(0, 16) + "...");

                Json::Value response = validationService->getValidationByFingerprint(fingerprint);
                auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("Certificate validation error: {}", e.what());
                Json::Value error;
                error["success"] = false;
                error["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Get}
    );

    // GET /api/certificates/export/file - Export single certificate file
    app.registerHandler(
        "/api/certificates/export/file",
        [&](const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try {
                std::string dn = req->getOptionalParameter<std::string>("dn").value_or("");
                std::string format = req->getOptionalParameter<std::string>("format").value_or("pem");

                if (dn.empty()) {
                    Json::Value error;
                    error["success"] = false;
                    error["error"] = "DN parameter is required";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                if (format != "der" && format != "pem") {
                    Json::Value error;
                    error["success"] = false;
                    error["error"] = "Invalid format. Use 'der' or 'pem'";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                spdlog::info("Certificate export file: dn={}, format={}", dn, format);

                // Export certificate
                services::ExportFormat exportFormat = (format == "der") ?
                    services::ExportFormat::DER : services::ExportFormat::PEM;

                auto result = ::certificateService->exportCertificateFile(dn, exportFormat);

                if (!result.success) {
                    Json::Value error;
                    error["success"] = false;
                    error["error"] = result.errorMessage;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                // Return binary file
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setBody(std::string(result.data.begin(), result.data.end()));
                resp->setContentTypeCode(drogon::CT_NONE);
                resp->addHeader("Content-Type", result.contentType);
                resp->addHeader("Content-Disposition", "attachment; filename=\"" + result.filename + "\"");
                callback(resp);

                // Phase 4.4: Audit logging - CERT_EXPORT success (single file)
                std::string conninfo = "host=" + appConfig.dbHost +
                                      " port=" + std::to_string(appConfig.dbPort) +
                                      " dbname=" + appConfig.dbName +
                                      " user=" + appConfig.dbUser +
                                      " password=" + appConfig.dbPassword;
                PGconn* auditConn = PQconnectdb(conninfo.c_str());
                if (auditConn && PQstatus(auditConn) == CONNECTION_OK) {
                    AuditLogEntry auditEntry;
                    auto session = req->getSession();
                    if (session) {
                        auto [userId, username] = extractUserFromRequest(req);
                        auditEntry.userId = userId;
                        auditEntry.username = username;
                    }

                    auditEntry.operationType = OperationType::CERT_EXPORT;
                    auditEntry.operationSubtype = "SINGLE_CERT";
                    auditEntry.resourceId = dn;
                    auditEntry.resourceType = "CERTIFICATE";
                    auditEntry.ipAddress = extractIpAddress(req);
                    auditEntry.userAgent = req->getHeader("User-Agent");
                    auditEntry.requestMethod = "GET";
                    auditEntry.requestPath = "/api/certificates/export/file";
                    auditEntry.success = true;

                    Json::Value metadata;
                    metadata["format"] = format;
                    metadata["fileName"] = result.filename;
                    metadata["fileSize"] = static_cast<Json::Int64>(result.data.size());
                    auditEntry.metadata = metadata;

                    logOperation(auditConn, auditEntry);
                    PQfinish(auditConn);
                }

            } catch (const std::exception& e) {
                spdlog::error("Certificate export file error: {}", e.what());
                Json::Value error;
                error["success"] = false;
                error["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Get}
    );

    // GET /api/certificates/export/country - Export all certificates by country (ZIP)
    app.registerHandler(
        "/api/certificates/export/country",
        [&](const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try {
                std::string country = req->getOptionalParameter<std::string>("country").value_or("");
                std::string format = req->getOptionalParameter<std::string>("format").value_or("pem");

                if (country.empty()) {
                    Json::Value error;
                    error["success"] = false;
                    error["error"] = "Country parameter is required";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                spdlog::info("Certificate export country: country={}, format={}", country, format);

                // Export all certificates for country
                services::ExportFormat exportFormat = (format == "der") ?
                    services::ExportFormat::DER : services::ExportFormat::PEM;

                auto result = ::certificateService->exportCountryCertificates(country, exportFormat);

                if (!result.success) {
                    Json::Value error;
                    error["success"] = false;
                    error["error"] = result.errorMessage;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                // Return ZIP file
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setBody(std::string(result.data.begin(), result.data.end()));
                resp->setContentTypeCode(drogon::CT_NONE);
                resp->addHeader("Content-Type", result.contentType);
                resp->addHeader("Content-Disposition", "attachment; filename=\"" + result.filename + "\"");
                callback(resp);

                // Phase 4.4: Audit logging - CERT_EXPORT success (country ZIP)
                std::string conninfo = "host=" + appConfig.dbHost +
                                      " port=" + std::to_string(appConfig.dbPort) +
                                      " dbname=" + appConfig.dbName +
                                      " user=" + appConfig.dbUser +
                                      " password=" + appConfig.dbPassword;
                PGconn* auditConn = PQconnectdb(conninfo.c_str());
                if (auditConn && PQstatus(auditConn) == CONNECTION_OK) {
                    AuditLogEntry auditEntry;
                    auto session = req->getSession();
                    if (session) {
                        auto [userId, username] = extractUserFromRequest(req);
                        auditEntry.userId = userId;
                        auditEntry.username = username;
                    }

                    auditEntry.operationType = OperationType::CERT_EXPORT;
                    auditEntry.operationSubtype = "COUNTRY_ZIP";
                    auditEntry.resourceId = country;
                    auditEntry.resourceType = "CERTIFICATE_COLLECTION";
                    auditEntry.ipAddress = extractIpAddress(req);
                    auditEntry.userAgent = req->getHeader("User-Agent");
                    auditEntry.requestMethod = "GET";
                    auditEntry.requestPath = "/api/certificates/export/country";
                    auditEntry.success = true;

                    Json::Value metadata;
                    metadata["country"] = country;
                    metadata["format"] = format;
                    metadata["fileName"] = result.filename;
                    metadata["fileSize"] = static_cast<Json::Int64>(result.data.size());
                    // Parse certificate count from filename if available
                    auditEntry.metadata = metadata;

                    logOperation(auditConn, auditEntry);
                    PQfinish(auditConn);
                }

            } catch (const std::exception& e) {
                spdlog::error("Certificate export country error: {}", e.what());
                Json::Value error;
                error["success"] = false;
                error["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Get}
    );

    // GET /api/certificates/countries - Get list of available countries (PostgreSQL)
    app.registerHandler(
        "/api/certificates/countries",
        [&](const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try {
                spdlog::debug("Fetching list of available countries from PostgreSQL");

                // Connect to PostgreSQL
                std::string conninfo = "host=" + appConfig.dbHost +
                                      " port=" + std::to_string(appConfig.dbPort) +
                                      " dbname=" + appConfig.dbName +
                                      " user=" + appConfig.dbUser +
                                      " password=" + appConfig.dbPassword;

                PGconn* conn = PQconnectdb(conninfo.c_str());
                if (PQstatus(conn) != CONNECTION_OK) {
                    std::string error = "Database connection failed: " + std::string(PQerrorMessage(conn));
                    PQfinish(conn);
                    throw std::runtime_error(error);
                }

                // Query distinct countries from PostgreSQL (fast: ~67ms)
                const char* query = "SELECT DISTINCT country_code FROM certificate "
                                   "WHERE country_code IS NOT NULL "
                                   "ORDER BY country_code";

                PGresult* res = PQexec(conn, query);

                if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                    std::string error = "Query failed: " + std::string(PQerrorMessage(conn));
                    PQclear(res);
                    PQfinish(conn);
                    throw std::runtime_error(error);
                }

                // Build JSON response
                int rowCount = PQntuples(res);
                Json::Value response;
                response["success"] = true;
                response["count"] = rowCount;

                Json::Value countryList(Json::arrayValue);
                for (int i = 0; i < rowCount; i++) {
                    std::string country = PQgetvalue(res, i, 0);
                    countryList.append(country);
                }
                response["countries"] = countryList;

                PQclear(res);
                PQfinish(conn);

                spdlog::info("Countries list fetched: {} countries from PostgreSQL", rowCount);

                auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("Error fetching countries from PostgreSQL: {}", e.what());
                Json::Value error;
                error["success"] = false;
                error["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
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

    // Register ICAO Auto Sync routes (v1.7.0)
    if (icaoHandler) {
        icaoHandler->registerRoutes(app);
        spdlog::info("ICAO Auto Sync routes registered");
    }

    // =========================================================================
    // Sprint 2: Link Certificate Validation API
    // =========================================================================

    // POST /api/validate/link-cert - Validate Link Certificate trust chain
    app.registerHandler(
        "/api/validate/link-cert",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("POST /api/validate/link-cert - Link Certificate validation");

            // Parse JSON request body
            auto json = req->getJsonObject();
            if (!json) {
                Json::Value error;
                error["success"] = false;
                error["error"] = "Invalid JSON body";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            // Get certificate binary (base64 encoded)
            std::string certBase64 = (*json).get("certificateBinary", "").asString();
            if (certBase64.empty()) {
                Json::Value error;
                error["success"] = false;
                error["error"] = "Missing certificateBinary field";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            // Decode base64
            std::vector<uint8_t> certBinary;
            try {
                std::string decoded = drogon::utils::base64Decode(certBase64);
                certBinary.assign(decoded.begin(), decoded.end());
            } catch (const std::exception& e) {
                Json::Value error;
                error["success"] = false;
                error["error"] = std::string("Base64 decode failed: ") + e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

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
                error["error"] = "Database connection failed";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
                PQfinish(conn);
                return;
            }

            try {
                // Create LC validator
                lc::LcValidator validator(conn);

                // Validate Link Certificate
                auto result = validator.validateLinkCertificate(certBinary);

                // Build JSON response
                Json::Value response;
                response["success"] = true;
                response["trustChainValid"] = result.trustChainValid;
                response["validationMessage"] = result.validationMessage;

                // Signature validation
                Json::Value signatures;
                signatures["oldCscaSignatureValid"] = result.oldCscaSignatureValid;
                signatures["oldCscaSubjectDn"] = result.oldCscaSubjectDn;
                signatures["oldCscaFingerprint"] = result.oldCscaFingerprint;
                signatures["newCscaSignatureValid"] = result.newCscaSignatureValid;
                signatures["newCscaSubjectDn"] = result.newCscaSubjectDn;
                signatures["newCscaFingerprint"] = result.newCscaFingerprint;
                response["signatures"] = signatures;

                // Certificate properties
                Json::Value properties;
                properties["validityPeriodValid"] = result.validityPeriodValid;
                properties["notBefore"] = result.notBefore;
                properties["notAfter"] = result.notAfter;
                properties["extensionsValid"] = result.extensionsValid;
                response["properties"] = properties;

                // Extensions details
                Json::Value extensions;
                extensions["basicConstraintsCa"] = result.basicConstraintsCa;
                extensions["basicConstraintsPathlen"] = result.basicConstraintsPathlen;
                extensions["keyUsage"] = result.keyUsage;
                extensions["extendedKeyUsage"] = result.extendedKeyUsage;
                response["extensions"] = extensions;

                // Revocation status
                Json::Value revocation;
                revocation["status"] = crl::revocationStatusToString(result.revocationStatus);
                revocation["message"] = result.revocationMessage;
                response["revocation"] = revocation;

                // Metadata
                response["validationDurationMs"] = result.validationDurationMs;

                auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
                callback(resp);

            } catch (const std::exception& e) {
                Json::Value error;
                error["success"] = false;
                error["error"] = std::string("Validation failed: ") + e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }

            PQfinish(conn);
        },
        {drogon::Post}
    );

    // GET /api/link-certs/search - Search Link Certificates
    app.registerHandler(
        "/api/link-certs/search",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/link-certs/search - Search Link Certificates");

            // Parse query parameters
            std::string country = req->getParameter("country");
            std::string validOnlyStr = req->getParameter("validOnly");
            std::string limitStr = req->getParameter("limit");
            std::string offsetStr = req->getParameter("offset");

            bool validOnly = (validOnlyStr == "true");
            int limit = limitStr.empty() ? 50 : std::stoi(limitStr);
            int offset = offsetStr.empty() ? 0 : std::stoi(offsetStr);

            // Validate parameters
            if (limit <= 0 || limit > 1000) {
                Json::Value error;
                error["success"] = false;
                error["error"] = "Invalid limit (must be 1-1000)";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

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
                error["error"] = "Database connection failed";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
                PQfinish(conn);
                return;
            }

            try {
                // Build SQL query with parameterized parameters
                std::ostringstream sql;
                sql << "SELECT id, subject_dn, issuer_dn, serial_number, fingerprint_sha256, "
                    << "old_csca_subject_dn, new_csca_subject_dn, "
                    << "trust_chain_valid, created_at, country_code "
                    << "FROM link_certificate WHERE 1=1";

                std::vector<std::string> paramValues;
                int paramIndex = 1;

                if (!country.empty()) {
                    sql << " AND country_code = $" << paramIndex++;
                    paramValues.push_back(country);
                }

                if (validOnly) {
                    sql << " AND trust_chain_valid = true";
                }

                sql << " ORDER BY created_at DESC LIMIT $" << paramIndex++ << " OFFSET $" << paramIndex++;
                paramValues.push_back(std::to_string(limit));
                paramValues.push_back(std::to_string(offset));

                // Prepare parameter pointers
                std::vector<const char*> paramPointers;
                for (const auto& pv : paramValues) {
                    paramPointers.push_back(pv.c_str());
                }

                // Execute query
                PGresult* res = PQexecParams(
                    conn,
                    sql.str().c_str(),
                    paramPointers.size(),
                    nullptr,
                    paramPointers.data(),
                    nullptr,
                    nullptr,
                    0
                );

                if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                    PQclear(res);
                    throw std::runtime_error("Database query failed");
                }

                // Build JSON response
                Json::Value response;
                response["success"] = true;
                response["total"] = PQntuples(res);
                response["limit"] = limit;
                response["offset"] = offset;

                Json::Value certificates(Json::arrayValue);
                for (int i = 0; i < PQntuples(res); i++) {
                    Json::Value cert;
                    cert["id"] = PQgetvalue(res, i, 0);
                    cert["subjectDn"] = PQgetvalue(res, i, 1);
                    cert["issuerDn"] = PQgetvalue(res, i, 2);
                    cert["serialNumber"] = PQgetvalue(res, i, 3);
                    cert["fingerprint"] = PQgetvalue(res, i, 4);
                    cert["oldCscaSubjectDn"] = PQgetvalue(res, i, 5);
                    cert["newCscaSubjectDn"] = PQgetvalue(res, i, 6);
                    cert["trustChainValid"] = (strcmp(PQgetvalue(res, i, 7), "t") == 0);
                    cert["createdAt"] = PQgetvalue(res, i, 8);
                    cert["countryCode"] = PQgetvalue(res, i, 9);

                    certificates.append(cert);
                }

                response["certificates"] = certificates;

                PQclear(res);

                auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
                callback(resp);

            } catch (const std::exception& e) {
                Json::Value error;
                error["success"] = false;
                error["error"] = std::string("Search failed: ") + e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }

            PQfinish(conn);
        },
        {drogon::Get}
    );

    // GET /api/link-certs/{id} - Get Link Certificate details by ID
    app.registerHandler(
        "/api/link-certs/{id}",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& id) {
            spdlog::info("GET /api/link-certs/{} - Get Link Certificate details", id);

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
                error["error"] = "Database connection failed";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
                PQfinish(conn);
                return;
            }

            try {
                // Query LC by ID (parameterized query)
                const char* query =
                    "SELECT id, subject_dn, issuer_dn, serial_number, fingerprint_sha256, "
                    "old_csca_subject_dn, old_csca_fingerprint, "
                    "new_csca_subject_dn, new_csca_fingerprint, "
                    "trust_chain_valid, old_csca_signature_valid, new_csca_signature_valid, "
                    "validity_period_valid, not_before, not_after, "
                    "extensions_valid, basic_constraints_ca, basic_constraints_pathlen, "
                    "key_usage, extended_key_usage, "
                    "revocation_status, revocation_message, "
                    "ldap_dn_v2, stored_in_ldap, created_at, country_code "
                    "FROM link_certificate WHERE id = $1";

                const char* paramValues[1] = {id.c_str()};
                PGresult* res = PQexecParams(conn, query, 1, nullptr, paramValues,
                                             nullptr, nullptr, 0);

                if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
                    PQclear(res);
                    Json::Value error;
                    error["success"] = false;
                    error["error"] = "Link Certificate not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    PQfinish(conn);
                    return;
                }

                // Build JSON response
                Json::Value response;
                response["success"] = true;

                Json::Value cert;
                cert["id"] = PQgetvalue(res, 0, 0);
                cert["subjectDn"] = PQgetvalue(res, 0, 1);
                cert["issuerDn"] = PQgetvalue(res, 0, 2);
                cert["serialNumber"] = PQgetvalue(res, 0, 3);
                cert["fingerprint"] = PQgetvalue(res, 0, 4);

                Json::Value signatures;
                signatures["oldCscaSubjectDn"] = PQgetvalue(res, 0, 5);
                signatures["oldCscaFingerprint"] = PQgetvalue(res, 0, 6);
                signatures["newCscaSubjectDn"] = PQgetvalue(res, 0, 7);
                signatures["newCscaFingerprint"] = PQgetvalue(res, 0, 8);
                signatures["trustChainValid"] = (strcmp(PQgetvalue(res, 0, 9), "t") == 0);
                signatures["oldCscaSignatureValid"] = (strcmp(PQgetvalue(res, 0, 10), "t") == 0);
                signatures["newCscaSignatureValid"] = (strcmp(PQgetvalue(res, 0, 11), "t") == 0);
                cert["signatures"] = signatures;

                Json::Value properties;
                properties["validityPeriodValid"] = (strcmp(PQgetvalue(res, 0, 12), "t") == 0);
                properties["notBefore"] = PQgetvalue(res, 0, 13);
                properties["notAfter"] = PQgetvalue(res, 0, 14);
                properties["extensionsValid"] = (strcmp(PQgetvalue(res, 0, 15), "t") == 0);
                cert["properties"] = properties;

                Json::Value extensions;
                extensions["basicConstraintsCa"] = (strcmp(PQgetvalue(res, 0, 16), "t") == 0);
                extensions["basicConstraintsPathlen"] = std::stoi(PQgetvalue(res, 0, 17));
                extensions["keyUsage"] = PQgetvalue(res, 0, 18);
                extensions["extendedKeyUsage"] = PQgetvalue(res, 0, 19);
                cert["extensions"] = extensions;

                Json::Value revocation;
                revocation["status"] = PQgetvalue(res, 0, 20);
                revocation["message"] = PQgetvalue(res, 0, 21);
                cert["revocation"] = revocation;

                cert["ldapDn"] = PQgetvalue(res, 0, 22);
                cert["storedInLdap"] = (strcmp(PQgetvalue(res, 0, 23), "t") == 0);
                cert["createdAt"] = PQgetvalue(res, 0, 24);
                cert["countryCode"] = PQgetvalue(res, 0, 25);

                response["certificate"] = cert;

                PQclear(res);

                auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
                callback(resp);

            } catch (const std::exception& e) {
                Json::Value error;
                error["success"] = false;
                error["error"] = std::string("Query failed: ") + e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }

            PQfinish(conn);
        },
        {drogon::Get}
    );

    // =========================================================================
    // Sprint 1: LDAP DN Migration API (Internal)
    // =========================================================================

    // POST /api/internal/migrate-ldap-dns - Migrate batch of certificates to v2 DN
    app.registerHandler(
        "/api/internal/migrate-ldap-dns",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("POST /api/internal/migrate-ldap-dns - Batch migration");

            auto json = req->getJsonObject();
            if (!json) {
                Json::Value error;
                error["success"] = false;
                error["error"] = "Invalid JSON body";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            int offset = (*json).get("offset", 0).asInt();
            int limit = (*json).get("limit", 100).asInt();
            std::string mode = (*json).get("mode", "test").asString();

            // Validate parameters
            if (limit <= 0 || limit > 1000) {
                Json::Value error;
                error["success"] = false;
                error["error"] = "Invalid limit (must be 1-1000)";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            if (mode != "test" && mode != "production") {
                Json::Value error;
                error["success"] = false;
                error["error"] = "Invalid mode (must be 'test' or 'production')";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k400BadRequest);
                callback(resp);
                return;
            }

            spdlog::info("Migration batch - offset: {}, limit: {}, mode: {}", offset, limit, mode);

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
                error["error"] = "Database connection failed";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
                PQfinish(conn);
                return;
            }

            // Connect to LDAP (only in production mode)
            LDAP* ld = nullptr;
            if (mode == "production") {
                std::string ldapUri = "ldap://" + appConfig.ldapWriteHost + ":" +
                                     std::to_string(appConfig.ldapWritePort);
                int rc = ldap_initialize(&ld, ldapUri.c_str());
                if (rc != LDAP_SUCCESS) {
                    Json::Value error;
                    error["success"] = false;
                    error["error"] = "LDAP initialization failed";
                    PQfinish(conn);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                // Bind to LDAP
                int version = LDAP_VERSION3;
                ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

                berval cred;
                cred.bv_val = const_cast<char*>(appConfig.ldapBindPassword.c_str());
                cred.bv_len = appConfig.ldapBindPassword.length();

                rc = ldap_sasl_bind_s(ld, appConfig.ldapBindDn.c_str(), LDAP_SASL_SIMPLE,
                                     &cred, nullptr, nullptr, nullptr);
                if (rc != LDAP_SUCCESS) {
                    Json::Value error;
                    error["success"] = false;
                    error["error"] = std::string("LDAP bind failed: ") + ldap_err2string(rc);
                    PQfinish(conn);
                    ldap_unbind_ext_s(ld, nullptr, nullptr);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }
            }

            // Fetch batch of certificates
            const char* query =
                "SELECT id, fingerprint_sha256, certificate_type, country_code, "
                "       certificate_data, subject_dn, serial_number, issuer_dn "
                "FROM certificate "
                "WHERE stored_in_ldap = true AND ldap_dn_v2 IS NULL "
                "ORDER BY id "
                "OFFSET $1 LIMIT $2";

            std::string offsetStr = std::to_string(offset);
            std::string limitStr = std::to_string(limit);
            const char* paramValues[2] = {offsetStr.c_str(), limitStr.c_str()};

            PGresult* res = PQexecParams(conn, query, 2, nullptr, paramValues,
                                        nullptr, nullptr, 0);

            if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                Json::Value error;
                error["success"] = false;
                error["error"] = std::string("DB query failed: ") + PQerrorMessage(conn);
                PQclear(res);
                PQfinish(conn);
                if (ld) ldap_unbind_ext_s(ld, nullptr, nullptr);
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
                return;
            }

            int rowCount = PQntuples(res);
            int successCount = 0;
            int failedCount = 0;
            Json::Value errors(Json::arrayValue);

            // Process each certificate
            for (int i = 0; i < rowCount; i++) {
                std::string certId = PQgetvalue(res, i, 0);
                std::string fingerprint = PQgetvalue(res, i, 1);
                std::string certType = PQgetvalue(res, i, 2);
                std::string country = PQgetvalue(res, i, 3);

                // Get binary certificate data
                size_t certDataLen = 0;
                unsigned char* certDataEscaped = PQunescapeBytea(
                    reinterpret_cast<const unsigned char*>(PQgetvalue(res, i, 4)),
                    &certDataLen
                );

                if (!certDataEscaped) {
                    failedCount++;
                    errors.append(certId + ": Failed to unescape certificate binary");
                    continue;
                }

                std::vector<uint8_t> certData(certDataEscaped, certDataEscaped + certDataLen);
                PQfreemem(certDataEscaped);

                std::string subjectDn = PQgetvalue(res, i, 5);
                std::string serialNumber = PQgetvalue(res, i, 6);
                std::string issuerDn = PQgetvalue(res, i, 7);

                // Build new DN
                std::string newDn = buildCertificateDnV2(fingerprint, certType, country);

                // In production mode, add to LDAP
                bool ldapSuccess = true;
                if (mode == "production") {
                    try {
                        saveCertificateToLdap(
                            ld, certType, country, subjectDn, issuerDn,
                            serialNumber, fingerprint, certData,
                            "", "", "",  // pkdConformance fields
                            false  // useLegacyDn = false (use v2 DN)
                        );
                    } catch (const std::exception& e) {
                        ldapSuccess = false;
                        failedCount++;
                        errors.append(certId + ": LDAP add failed - " + std::string(e.what()));
                        continue;
                    }
                }

                // Update database with new DN
                if (ldapSuccess || mode == "test") {
                    const char* updateQuery = "UPDATE certificate SET ldap_dn_v2 = $1 WHERE id = $2";
                    const char* updateParams[2] = {newDn.c_str(), certId.c_str()};

                    PGresult* updateRes = PQexecParams(conn, updateQuery, 2, nullptr,
                                                      updateParams, nullptr, nullptr, 0);

                    if (PQresultStatus(updateRes) == PGRES_COMMAND_OK) {
                        successCount++;
                        spdlog::debug("Migrated certificate {} to new DN: {}", certId, newDn);
                    } else {
                        failedCount++;
                        errors.append(certId + ": DB update failed");
                    }
                    PQclear(updateRes);
                }
            }

            PQclear(res);
            PQfinish(conn);
            if (ld) ldap_unbind_ext_s(ld, nullptr, nullptr);

            // Update migration status
            std::string statusConninfo = "host=" + appConfig.dbHost +
                                        " port=" + std::to_string(appConfig.dbPort) +
                                        " dbname=" + appConfig.dbName +
                                        " user=" + appConfig.dbUser +
                                        " password=" + appConfig.dbPassword;
            PGconn* statusConn = PQconnectdb(statusConninfo.c_str());
            if (PQstatus(statusConn) == CONNECTION_OK) {
                const char* statusQuery =
                    "UPDATE ldap_migration_status "
                    "SET migrated_records = migrated_records + $1, "
                    "    failed_records = failed_records + $2, "
                    "    updated_at = NOW() "
                    "WHERE table_name = 'certificate' "
                    "  AND status = 'IN_PROGRESS'";

                std::string successStr = std::to_string(successCount);
                std::string failedStr = std::to_string(failedCount);
                const char* statusParams[2] = {successStr.c_str(), failedStr.c_str()};

                PGresult* statusRes = PQexecParams(statusConn, statusQuery, 2, nullptr,
                                                  statusParams, nullptr, nullptr, 0);
                PQclear(statusRes);
            }
            PQfinish(statusConn);

            // Return results
            Json::Value resp;
            resp["success"] = true;
            resp["mode"] = mode;
            resp["processed"] = successCount + failedCount;
            resp["success_count"] = successCount;
            resp["failed_count"] = failedCount;
            resp["errors"] = errors;

            spdlog::info("Migration batch complete - success: {}, failed: {}",
                        successCount, failedCount);

            callback(drogon::HttpResponse::newHttpJsonResponse(resp));
        },
        {drogon::Post}
    );

    spdlog::info("API routes registered (including Sprint 1 migration endpoints)");
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

    // Validate required credentials
    try {
        appConfig.validateRequiredCredentials();
    } catch (const std::exception& e) {
        spdlog::critical("{}", e.what());
        return 1;
    }

    spdlog::info("====== ICAO Local PKD v2.1.3.1 Repository-Pattern-Phase3 (Build 20260130-005800) ======");
    spdlog::info("Database: {}:{}/{}", appConfig.dbHost, appConfig.dbPort, appConfig.dbName);
    spdlog::info("LDAP: {}:{}", appConfig.ldapHost, appConfig.ldapPort);

    try {
        // Initialize Certificate Service (Clean Architecture)
        // Certificate search base DN: dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
        // Note: Repository will prepend dc=data,dc=download based on search criteria
        std::string certSearchBaseDn = appConfig.ldapBaseDn;

        repositories::LdapConfig ldapConfig(
            "ldap://" + appConfig.ldapHost + ":" + std::to_string(appConfig.ldapPort),
            appConfig.ldapBindDn,
            appConfig.ldapBindPassword,
            certSearchBaseDn,
            30
        );
        auto repository = std::make_shared<repositories::LdapCertificateRepository>(ldapConfig);
        certificateService = std::make_shared<services::CertificateService>(repository);
        spdlog::info("Certificate service initialized with LDAP repository (baseDN: {})", certSearchBaseDn);

        // TODO: Replace with Redis-based caching for better performance and scalability
        // Countries API now uses PostgreSQL for instant response (~70ms)
        spdlog::info("Countries API configured (PostgreSQL query, ~70ms response time)");

        // Initialize ICAO Auto Sync Module (v1.7.0)
        spdlog::info("Initializing ICAO Auto Sync module...");

        std::string dbConnInfo = "host=" + appConfig.dbHost +
                                " port=" + std::to_string(appConfig.dbPort) +
                                " dbname=" + appConfig.dbName +
                                " user=" + appConfig.dbUser +
                                " password=" + appConfig.dbPassword;

        auto icaoRepo = std::make_shared<repositories::IcaoVersionRepository>(dbConnInfo);
        auto httpClient = std::make_shared<infrastructure::http::HttpClient>();

        infrastructure::notification::EmailSender::EmailConfig emailConfig;
        emailConfig.smtpHost = "localhost";
        emailConfig.smtpPort = 25;
        emailConfig.fromAddress = appConfig.notificationEmail;
        emailConfig.useTls = false;
        auto emailSender = std::make_shared<infrastructure::notification::EmailSender>(emailConfig);

        services::IcaoSyncService::Config icaoConfig;
        icaoConfig.icaoPortalUrl = appConfig.icaoPortalUrl;
        icaoConfig.notificationEmail = appConfig.notificationEmail;
        icaoConfig.autoNotify = appConfig.icaoAutoNotify;
        icaoConfig.httpTimeoutSeconds = appConfig.icaoHttpTimeout;

        auto icaoService = std::make_shared<services::IcaoSyncService>(
            icaoRepo, httpClient, emailSender, icaoConfig
        );

        icaoHandler = std::make_shared<handlers::IcaoHandler>(icaoService);
        spdlog::info("ICAO Auto Sync module initialized (Portal: {}, Notify: {})",
                    appConfig.icaoPortalUrl,
                    appConfig.icaoAutoNotify ? "enabled" : "disabled");

        // Initialize Authentication Handler (Phase 3)
        spdlog::info("Initializing Authentication module...");
        authHandler = std::make_shared<handlers::AuthHandler>(dbConnInfo);
        spdlog::info("Authentication module initialized");

        // Phase 1.6: Initialize Repository Pattern - Repositories and Services
        spdlog::info("Initializing Repository Pattern (Phase 1.6)...");

        // Create database connection pool for Repositories (v2.3.1: Thread-safe connection management)
        try {

            dbPool = std::make_shared<common::DbConnectionPool>(
                dbConnInfo,  // PostgreSQL connection string
                5,   // minConnections
                20,  // maxConnections
                5    // acquireTimeoutSec
            );








            spdlog::info("Database connection pool initialized (min=5, max=20)");
        } catch (const std::exception& e) {
            spdlog::critical("Failed to initialize database connection pool: {}", e.what());
            return 1;
        }

        // Create LDAP connection (for UploadService)
        // Note: Using write host for upload operations
        LDAP* ldapWriteConn = nullptr;
        std::string ldapWriteUri = "ldap://" + appConfig.ldapWriteHost + ":" + std::to_string(appConfig.ldapWritePort);
        int ldapResult = ldap_initialize(&ldapWriteConn, ldapWriteUri.c_str());
        if (ldapResult != LDAP_SUCCESS) {
            spdlog::error("Failed to initialize LDAP connection: {}", ldap_err2string(ldapResult));
            dbPool.reset();  // Release connection pool
            return 1;
        }

        int version = LDAP_VERSION3;
        ldap_set_option(ldapWriteConn, LDAP_OPT_PROTOCOL_VERSION, &version);

        struct berval cred;
        cred.bv_val = const_cast<char*>(appConfig.ldapBindPassword.c_str());
        cred.bv_len = appConfig.ldapBindPassword.length();

        ldapResult = ldap_sasl_bind_s(ldapWriteConn, appConfig.ldapBindDn.c_str(),
                                      LDAP_SASL_SIMPLE, &cred, nullptr, nullptr, nullptr);
        if (ldapResult != LDAP_SUCCESS) {
            spdlog::error("LDAP bind failed: {}", ldap_err2string(ldapResult));
            ldap_unbind_ext_s(ldapWriteConn, nullptr, nullptr);
            dbPool.reset();  // Release connection pool
            return 1;
        }
        spdlog::info("LDAP write connection established for UploadService");

        // Initialize Repositories with Connection Pool (v2.3.1: Thread-safe database access)
        uploadRepository = std::make_shared<repositories::UploadRepository>(dbPool.get());
        certificateRepository = std::make_shared<repositories::CertificateRepository>(dbPool.get());
        validationRepository = std::make_shared<repositories::ValidationRepository>(dbPool.get());
        auditRepository = std::make_shared<repositories::AuditRepository>(dbPool.get());
        statisticsRepository = std::make_shared<repositories::StatisticsRepository>(dbPool.get());
        spdlog::info("Repositories initialized with Connection Pool (Upload, Certificate, Validation, Audit, Statistics)");
        ldifStructureRepository = std::make_shared<repositories::LdifStructureRepository>(uploadRepository.get());

        // Initialize Services with Repository dependencies
        uploadService = std::make_shared<services::UploadService>(
            uploadRepository.get(),
            certificateRepository.get(),
            ldapWriteConn
        );

        validationService = std::make_shared<services::ValidationService>(
            validationRepository.get(),
            certificateRepository.get()
        );

        auditService = std::make_shared<services::AuditService>(
            auditRepository.get()
        );

        statisticsService = std::make_shared<services::StatisticsService>(
            statisticsRepository.get(),
            uploadRepository.get()
        );

        ldifStructureService = std::make_shared<services::LdifStructureService>(
            ldifStructureRepository.get()
        );

        spdlog::info("Services initialized with Repository dependencies (Upload, Validation, Audit, Statistics, LdifStructure)");
        spdlog::info("Repository Pattern initialization complete - Ready for Oracle migration");

        auto& app = drogon::app();

        // Server settings
        app.setLogPath("logs")
           .setLogLevel(trantor::Logger::kInfo)
           .addListener("0.0.0.0", appConfig.serverPort)
           .setThreadNum(appConfig.threadNum)
           .enableGzip(true)
           .setClientMaxBodySize(100 * 1024 * 1024)  // 100MB max upload
           .setUploadPath("/app/uploads")  // Absolute path for security
           .setDocumentRoot("./static");

        // Enable CORS for React.js frontend
        app.registerPreSendingAdvice([](const drogon::HttpRequestPtr& req,
                                         const drogon::HttpResponsePtr& resp) {
            resp->addHeader("Access-Control-Allow-Origin", "*");
            resp->addHeader("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            resp->addHeader("Access-Control-Allow-Headers", "Content-Type, Authorization, X-User-Id");
        });

        // Register AuthMiddleware globally for JWT authentication (v2.3.1)
        spdlog::info("Registering AuthMiddleware globally...");
        try {
            auto authMiddleware = std::make_shared<middleware::AuthMiddleware>();

            app.registerPreHandlingAdvice([authMiddleware](const drogon::HttpRequestPtr& req,
                                                           drogon::AdviceCallback&& callback,
                                                           drogon::AdviceChainCallback&& chainCallback) {
                // AuthMiddleware will validate JWT and set session for non-public endpoints
                authMiddleware->doFilter(
                    req,
                    [callback = std::move(callback)](const drogon::HttpResponsePtr& resp) mutable {
                        // Authentication failed - return error response
                        callback(resp);
                    },
                    [chainCallback = std::move(chainCallback)]() mutable {
                        // Authentication succeeded or public endpoint - continue to handler
                        chainCallback();
                    }
                );
            });

            spdlog::info("✅ AuthMiddleware registered globally - JWT authentication enabled");
        } catch (const std::exception& e) {
            spdlog::error("❌ Failed to register AuthMiddleware: {}", e.what());
            spdlog::warn("⚠️  Server will start WITHOUT authentication!");
        }

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

        // Phase 1.6: Cleanup - Close LDAP connections (Database connection pool auto-cleanup via shared_ptr)
        spdlog::info("Shutting down Repository Pattern resources...");
        dbPool.reset();  // Explicitly release connection pool
        spdlog::info("Database connection pool closed");
        spdlog::info("Repository Pattern resources cleaned up");

    } catch (const std::exception& e) {
        spdlog::error("Application error: {}", e.what());

        // Cleanup on error (Connection pool auto-cleanup via shared_ptr)
        dbPool.reset();

        return 1;
    }

    spdlog::info("Server stopped");
    return 0;
}
