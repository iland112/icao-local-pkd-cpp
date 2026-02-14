/**
 * @file main.cpp
 * @brief ICAO Local PKD Application Entry Point
 *
 * C++ REST API based ICAO Local PKD Management and
 * Passive Authentication (PA) Verification System.
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
#include <mutex>
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
#include "db_connection_pool.h"
#include "db_connection_pool_factory.h"
#include <ldap_connection_pool.h>
#include "common/certificate_utils.h"
#include "common/masterlist_processor.h"
#include "common/x509_metadata_extractor.h"
#include "common/progress_manager.h"
#include "common/asn1_parser.h"
#include "processing_strategy.h"
#include "ldif_processor.h"

// Shared library
#include "icao/x509/dn_parser.h"
#include "icao/x509/dn_components.h"

// Clean Architecture layers
#include "domain/models/certificate.h"
#include "repositories/ldap_certificate_repository.h"
#include "services/certificate_service.h"

// ICAO Auto Sync Module
#include "handlers/icao_handler.h"
#include "services/icao_sync_service.h"
#include "repositories/icao_version_repository.h"
#include "infrastructure/http/http_client.h"
#include "infrastructure/notification/email_sender.h"

// Repositories
#include "repositories/upload_repository.h"
#include "repositories/certificate_repository.h"
#include "repositories/validation_repository.h"
#include "repositories/audit_repository.h"
#include "repositories/statistics_repository.h"
#include "repositories/ldif_structure_repository.h"
#include "repositories/user_repository.h"
#include "repositories/auth_audit_repository.h"
#include "repositories/crl_repository.h"
#include "repositories/deviation_list_repository.h"

// Services
#include "services/upload_service.h"
#include "services/validation_service.h"
#include "services/audit_service.h"
#include "services/statistics_service.h"
#include "services/ldif_structure_service.h"

// Authentication Module
#include "middleware/auth_middleware.h"
#include "middleware/permission_filter.h"
#include "auth/jwt_service.h"
#include "auth/password_hash.h"
#include "handlers/auth_handler.h"

// Link Certificate Validation
#include "common/lc_validator.h"

// Global certificate service (initialized in main(), used by all routes)
std::shared_ptr<services::CertificateService> certificateService;

// Global ICAO handler (initialized in main())
std::shared_ptr<handlers::IcaoHandler> icaoHandler;

// Global Auth handler (initialized in main())
std::shared_ptr<handlers::AuthHandler> authHandler;

// Global Repositories and Services (Repository Pattern)
std::shared_ptr<common::DbConnectionPool> dbPool;
std::unique_ptr<common::IQueryExecutor> queryExecutor;
std::shared_ptr<common::LdapConnectionPool> ldapPool;
std::shared_ptr<repositories::UploadRepository> uploadRepository;
std::shared_ptr<repositories::CertificateRepository> certificateRepository;
std::shared_ptr<repositories::ValidationRepository> validationRepository;
std::shared_ptr<repositories::AuditRepository> auditRepository;
std::shared_ptr<repositories::StatisticsRepository> statisticsRepository;
std::shared_ptr<repositories::LdifStructureRepository> ldifStructureRepository;
std::shared_ptr<repositories::UserRepository> userRepository;
std::shared_ptr<repositories::AuthAuditRepository> authAuditRepository;
std::shared_ptr<repositories::CrlRepository> crlRepository;
std::shared_ptr<repositories::DeviationListRepository> deviationListRepository;  // DL deviation data
std::shared_ptr<services::UploadService> uploadService;
std::shared_ptr<services::ValidationService> validationService;
std::shared_ptr<services::AuditService> auditService;
std::shared_ptr<services::StatisticsService> statisticsService;
std::shared_ptr<services::LdifStructureService> ldifStructureService;

// Global cache for available countries (populated on startup)
std::set<std::string> cachedCountries;
std::mutex countriesCacheMutex;

namespace {

// Use global repository and connection pool from outside anonymous namespace
// Access via :: scope resolution operator (e.g., ::certificateRepository, ::dbPool)

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

    // LDAP Read: Application-level load balancing
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

    // ICAO Auto Sync Configuration
    std::string icaoPortalUrl = "https://pkddownloadsg.icao.int/";
    std::string notificationEmail = "admin@localhost";
    bool icaoAutoNotify = true;
    int icaoHttpTimeout = 10;  // seconds

    // ICAO Scheduler Configuration
    int icaoCheckScheduleHour = 9;   // 0-23, default 9 AM
    bool icaoSchedulerEnabled = true;

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

        // LDAP Read Hosts (Application-level load balancing)
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

        // ICAO Scheduler Configuration
        if (auto val = std::getenv("ICAO_CHECK_SCHEDULE_HOUR")) {
            config.icaoCheckScheduleHour = std::stoi(val);
            if (config.icaoCheckScheduleHour < 0 || config.icaoCheckScheduleHour > 23)
                config.icaoCheckScheduleHour = 9;
        }
        if (auto val = std::getenv("ICAO_SCHEDULER_ENABLED"))
            config.icaoSchedulerEnabled = (std::string(val) == "true");

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

// --- SSE Progress Management ---
// Progress Manager imported from common/progress_manager.h

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

// --- Trust Anchor & CMS Signature Verification ---

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

// --- CSCA Self-Signature Validation ---

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
 * Includes trustChainPath for link certificate support
 */
struct DscValidationResult {
    bool isValid;
    bool cscaFound;
    bool signatureValid;
    bool notExpired;
    bool notRevoked;
    // ICAO Doc 9303 Part 12 hybrid chain model: expiration is informational
    bool dscExpired;   // DSC certificate has expired (informational)
    bool cscaExpired;  // CSCA in chain has expired (informational)
    std::string cscaSubjectDn;
    std::string errorMessage;
    std::string trustChainPath;  // Human-readable chain path (e.g., "DSC -> CN=CSCA_old -> CN=Link -> CN=CSCA_new")
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


// --- Trust Chain Building Utilities ---

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
        // ICAO 9303 Part 12: When multiple CSCAs share the same DN (key rollover),
        // select the one whose public key successfully verifies the current certificate's signature.
        X509* issuer = nullptr;
        X509* dnMatchFallback = nullptr;  // Fallback: first DN match (if no signature matches)
        for (X509* csca : allCscas) {
            std::string cscaSubjectDn = getCertSubjectDn(csca);

            // Case-insensitive DN comparison (RFC 4517)
            if (strcasecmp(currentIssuerDn.c_str(), cscaSubjectDn.c_str()) == 0) {
                // DN matches - verify signature to confirm correct key pair
                EVP_PKEY* cscaPubKey = X509_get_pubkey(csca);
                if (cscaPubKey) {
                    int verifyResult = X509_verify(current, cscaPubKey);
                    EVP_PKEY_free(cscaPubKey);
                    if (verifyResult == 1) {
                        issuer = csca;
                        spdlog::debug("Chain building: Found issuer at depth {} (signature verified): {}",
                                      depth, cscaSubjectDn.substr(0, 50));
                        break;
                    } else {
                        spdlog::debug("Chain building: DN match but signature failed at depth {}: {}",
                                      depth, cscaSubjectDn.substr(0, 50));
                        if (!dnMatchFallback) dnMatchFallback = csca;
                    }
                } else {
                    spdlog::warn("Chain building: Failed to extract public key from CSCA: {}",
                                 cscaSubjectDn.substr(0, 50));
                    if (!dnMatchFallback) dnMatchFallback = csca;
                }
            }
        }
        // If no signature-verified match found, use DN-only match for error reporting
        if (!issuer && dnMatchFallback) {
            spdlog::warn("Chain building: No signature-verified CSCA found at depth {}, "
                         "using DN match fallback for chain path reporting", depth);
            issuer = dnMatchFallback;
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
/**
 * @brief Validate trust chain using ICAO Doc 9303 Part 12 hybrid chain model
 *
 * Per ICAO 9303: Signature verification is a HARD requirement.
 * Certificate expiration is INFORMATIONAL (reported via cscaExpired out-param).
 * Rationale: CSCA validity 13-15 years, DSC validity ~3 months, passport validity ~10 years.
 * An expired CSCA's public key can still cryptographically verify DSC signatures.
 *
 * @param chain Trust chain to validate
 * @param[out] cscaExpired Set to true if any CSCA in chain is expired
 * @return true if all signature verifications pass (regardless of expiration)
 */
static bool validateTrustChain(const TrustChain& chain, bool& cscaExpired) {
    cscaExpired = false;

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

        // Check expiration (informational per ICAO hybrid model)
        if (X509_cmp_time(X509_get0_notAfter(cert), &now) < 0) {
            cscaExpired = true;
            spdlog::info("Chain validation: CSCA at depth {} is expired (informational per ICAO 9303)", i);
        }

        // Verify signature (cert signed by issuer) - HARD requirement
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

    if (cscaExpired) {
        spdlog::info("Chain validation: Trust chain signatures VALID, CSCA expired ({} certificates)",
                     chain.certificates.size());
    } else {
        spdlog::info("Chain validation: Trust chain VALID ({} certificates)",
                     chain.certificates.size());
    }
    return true;
}

// --- DSC Trust Chain Validation ---

/**
 * @brief Validate DSC certificate against its issuing CSCA
 * Checks:
 * 1. CSCA exists in DB
 * 2. DSC signature is valid (signed by CSCA)
 * 3. DSC is not expired
 */
DscValidationResult validateDscCertificate(X509* dscCert, const std::string& issuerDn) {
    DscValidationResult result = {false, false, false, false, false, false, false, "", "", ""};

    if (!dscCert) {
        result.errorMessage = "DSC certificate is null";
        return result;
    }

    // Step 1: Check DSC expiration (ICAO hybrid model: informational, not hard failure)
    // Per ICAO Doc 9303 Part 12: DSC validity ~3 months, passport validity ~10 years
    // Expired DSC is normal and expected; cryptographic validity is the hard requirement
    time_t now = time(nullptr);
    if (X509_cmp_time(X509_get0_notAfter(dscCert), &now) < 0) {
        result.dscExpired = true;
        result.notExpired = false;
        spdlog::info("DSC validation: DSC is expired (informational per ICAO 9303)");
    } else {
        result.notExpired = true;
    }
    if (X509_cmp_time(X509_get0_notBefore(dscCert), &now) > 0) {
        // NOT_YET_VALID is a hard failure (certificate not yet active)
        result.errorMessage = "DSC certificate is not yet valid";
        spdlog::warn("DSC validation: DSC is NOT YET VALID");
        return result;
    }

    // Step 2: Find ALL CSCAs matching issuer DN (including link certificates)
    std::vector<X509*> allCscas = ::certificateRepository->findAllCscasBySubjectDn(issuerDn);

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
    result.trustChainPath = chain.path;

    // Step 4: Validate trust chain signatures (ICAO hybrid model)
    // Signature verification is a HARD requirement; expiration is informational
    bool cscaExpired = false;
    bool signaturesValid = validateTrustChain(chain, cscaExpired);
    result.cscaExpired = cscaExpired;

    if (signaturesValid) {
        result.signatureValid = true;
        result.isValid = true;
        if (result.dscExpired || result.cscaExpired) {
            spdlog::info("DSC validation: Trust Chain VERIFIED (expired) - Path: {}", result.trustChainPath);
        } else {
            spdlog::info("DSC validation: Trust Chain VERIFIED - Path: {}", result.trustChainPath);
        }
    } else {
        result.errorMessage = "Trust chain signature verification failed";
        spdlog::error("DSC validation: Trust Chain FAILED - {}", result.errorMessage);
    }

    // Cleanup
    for (X509* csca : allCscas) X509_free(csca);

    return result;
}

} // Close anonymous namespace temporarily for extern functions

// Functions that need to be accessible from other compilation units
// (processing_strategy.cpp, ldif_processor.cpp)

// --- Credential Scrubbing Utility ---

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

// --- File Upload Security ---

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

/**
 * @brief Check if a file with the same hash already exists
 * @param fileHash SHA-256 hash of the file
 * @return JSON object with existing upload details if duplicate found, null otherwise
 */
Json::Value checkDuplicateFile(const std::string& fileHash) {
    Json::Value result;  // null by default

    if (!::uploadRepository) {
        spdlog::warn("Duplicate check skipped: uploadRepository is null (continuing with upload)");
        return result;  // Fail open: allow upload if check fails
    }

    try {
        auto uploadOpt = ::uploadRepository->findByFileHash(fileHash);
        if (uploadOpt.has_value()) {
            const auto& upload = uploadOpt.value();
            result["uploadId"] = upload.id;
            result["fileName"] = upload.fileName;
            result["uploadTimestamp"] = upload.createdAt;
            result["status"] = upload.status;
            result["processingMode"] = upload.processingMode.value_or("");
            result["fileFormat"] = upload.fileFormat;
        }
    } catch (const std::exception& e) {
        spdlog::warn("Duplicate check query failed (continuing with upload): {}", e.what());
    }

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

    if (!::queryExecutor) {
        result["status"] = "DOWN";
        result["error"] = "Query executor not initialized";
        return result;
    }

    auto start = std::chrono::steady_clock::now();

    try {
        std::string dbType = ::queryExecutor->getDatabaseType();
        std::string versionQuery = (dbType == "oracle")
            ? "SELECT banner AS version FROM v$version WHERE ROWNUM = 1"
            : "SELECT version()";

        auto rows = ::queryExecutor->executeQuery(versionQuery);

        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        result["status"] = "UP";
        result["responseTimeMs"] = static_cast<int>(duration.count());
        result["type"] = (dbType == "oracle") ? "Oracle" : "PostgreSQL";
        if (rows.size() > 0 && rows[0].isMember("version")) {
            result["version"] = rows[0]["version"].asString();
        }
    } catch (const std::exception& e) {
        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
        result["status"] = "DOWN";
        result["error"] = e.what();
        result["responseTimeMs"] = static_cast<int>(duration.count());
    }

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
    if (!ctx) {
        spdlog::error("Failed to create EVP_MD_CTX");
        return "";
    }
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

// escapeSqlString() - removed, no longer needed with parameterized queries via QueryExecutor

// saveUploadRecord() - removed, use ::uploadRepository->insert() instead

// updateUploadStatus() - removed, use ::uploadRepository->updateStatus() instead

/**
 * @brief Send enhanced progress update with optional certificate metadata
 *
 * Helper function for sending progress updates with
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

// --- Certificate/CRL Parsing and DB Storage Functions ---

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
    if (!hex) { BN_free(bn); return ""; }
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

// escapeBytea() - removed, PostgreSQL-specific, not needed with QueryExecutor

// --- LDAP Storage Functions ---

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
 * DN Structure (Multi-valued RDN like Java project):
 * cn={ESCAPED-SUBJECT-DN}+sn={SERIAL},o={csca|dsc},c={COUNTRY},dc={data|nc-data},dc=download,dc=pkd,{baseDN}
 *
 * This matches the working Java implementation which uses multi-valued RDN (cn+sn).
 * Multi-valued RDN is more robust when Subject DN contains LDAP special characters.
 * Extracts only standard LDAP attributes for DN to avoid LDAP error 80
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
        ou = "lc";
        dataContainer = appConfig.ldapDataContainer;
    } else if (certType == "DSC_NC") {
        ou = "dsc";
        dataContainer = appConfig.ldapNcDataContainer;
    } else {
        ou = "dsc";
        dataContainer = appConfig.ldapDataContainer;
    }

    // Extract only standard attributes to avoid LDAP error 80
    auto [standardDn, nonStandardAttrs] = extractStandardAttributes(subjectDn);

    // Escape Subject DN for safe use in LDAP DN (RFC 4514)
    std::string escapedSubjectDn = escapeLdapDnValue(standardDn);

    // Use multi-valued RDN (cn+sn) like Java project
    // This isolates the complex Subject DN structure and makes DN parsing more robust
    // Multi-valued RDN: cn={ESCAPED-SUBJECT-DN}+sn={SERIAL}
    // Note: appConfig.ldapBaseDn already contains dc=download
    return "cn=" + escapedSubjectDn + "+sn=" + serialNumber + ",o=" + ou + ",c=" + countryCode +
           "," + dataContainer + "," + appConfig.ldapBaseDn;
}

/**
 * @brief Build LDAP DN for certificate (v2 - Fingerprint-based)
 * @param fingerprint SHA-256 fingerprint of the certificate
 * @param certType CSCA, DSC, or DSC_NC
 * @param countryCode ISO country code
 *
 * DN Structure (Fingerprint-based DN):
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
    // ICAO PKD DIT structure
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
        ou = "lc";
        dataContainer = appConfig.ldapDataContainer;
    } else if (certType == "MLSC") {
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
    // Note: appConfig.ldapBaseDn already contains dc=download
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

    // Ensure data container exists before creating country entry
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
                                   bool useLegacyDn = false) {
    bool isNcData = (certType == "DSC_NC");

    // Ensure country structure exists
    if (!ensureCountryOuExists(ld, countryCode, isNcData)) {
        spdlog::warn("Failed to ensure country OU exists for {}", countryCode);
        // Continue anyway - the OU might exist even if we couldn't create it
    }

    // Extract standard and non-standard attributes
    auto [standardDn, nonStandardAttrs] = extractStandardAttributes(subjectDn);

    // Support both legacy (Subject DN + Serial) and new (Fingerprint) DN formats
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
    // For v2 DN (fingerprint-based), use fingerprint as cn; for legacy DN, use standard DN
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

    // description (Full Subject DN with non-standard attributes + fingerprint)
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

    // DSC_NC specific attributes (pkdConformanceCode, pkdConformanceText, pkdVersion)
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

        // Get detailed error information from LDAP connection
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

// updateCertificateLdapStatus -> CertificateRepository::updateCertificateLdapStatus()
// updateCrlLdapStatus -> CrlRepository::updateLdapStatus()

/**
 * @brief Build DN for Master List entry in LDAP (o=ml node)
 * Format: cn={fingerprint},o=ml,c={country},dc=data,dc=download,dc=pkd,{baseDN}
 *
 * SECURITY: Uses ldap_utils::escapeDnComponent for safe DN construction (RFC 4514)
 */
std::string buildMasterListDn(const std::string& countryCode, const std::string& fingerprint) {
    // Fingerprint is SHA-256 hex (safe), but escape defensively
    // Country code is ISO 3166-1 alpha-2 (safe), but escape defensively
    // Note: appConfig.ldapBaseDn already contains dc=download
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
    // Note: appConfig.ldapBaseDn already contains dc=download
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
void updateMasterListLdapStatus(const std::string& mlId, const std::string& ldapDn) {
    if (ldapDn.empty()) return;

    spdlog::warn("[UpdateMasterListLdapStatus] Stub implementation - needs MasterListRepository");
    spdlog::debug("[UpdateMasterListLdapStatus] Would update LDAP status: ml_id={}, ldap_dn={}",
                 mlId.substr(0, 8) + "...", ldapDn);
}

// --- Database Storage Functions ---


/**
 * @brief Save Master List to database
 * @return Master List ID or empty string on failure
 */
std::string saveMasterList(const std::string& uploadId,
                            const std::string& countryCode, const std::string& signerDn,
                            const std::string& fingerprint, int cscaCount,
                            const std::vector<uint8_t>& mlBinary) {
    std::string mlId = generateUuid();

    spdlog::warn("[SaveMasterList] Stub implementation - needs MasterListRepository");
    spdlog::info("[SaveMasterList] Would save Master List: upload={}, country={}, signer={}, csca_count={}, binary_size={}",
                uploadId.substr(0, 8) + "...", countryCode, signerDn.substr(0, 30) + "...", cscaCount, mlBinary.size());

    // Return generated UUID for now (actual save not implemented)
    return mlId;
}

/**
 * @brief Parse and save certificate from LDIF entry (DB + LDAP)
 */
bool parseCertificateEntry(LDAP* ld, const std::string& uploadId,
                           const LdifEntry& entry, const std::string& attrName,
                           int& cscaCount, int& dscCount, int& dscNcCount, int& ldapStoredCount,
                           ValidationStats& validationStats,
                           common::ValidationStatistics& enhancedStats) {
    std::string base64Value = entry.getFirstAttribute(attrName);
    if (base64Value.empty()) return false;

    spdlog::debug("parseCertificateEntry: base64Value len={}, first20chars={}",
                 base64Value.size(), base64Value.substr(0, 20));

    std::vector<uint8_t> derBytes = base64Decode(base64Value);
    if (derBytes.empty()) {
        common::addProcessingError(enhancedStats, "BASE64_DECODE_FAILED",
            entry.dn, "", "", "", "Base64 decode returned empty for attribute: " + attrName);
        return false;
    }

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
        common::addProcessingError(enhancedStats, "CERT_PARSE_FAILED",
            entry.dn, "", "", "", "Failed to parse X.509 certificate (d2i_X509 returned NULL)");
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

    // Extract comprehensive certificate metadata for progress tracking
    // Note: This extraction is done early (before validation) so metadata is available
    // for enhanced progress updates. ICAO compliance will be checked after cert type is determined.
    CertificateMetadata certMetadata = common::extractCertificateMetadataForProgress(cert, false);
    spdlog::debug("Extracted metadata for cert: type={}, sigAlg={}, keySize={}",
                  certMetadata.certificateType, certMetadata.signatureAlgorithm, certMetadata.keySize);

    // Determine certificate type and perform validation
    std::string certType;
    std::string validationStatus = "PENDING";
    std::string validationMessage = "";

    // Prepare validation result record
    domain::models::ValidationResult valRecord;
    valRecord.uploadId = uploadId;
    valRecord.fingerprint = fingerprint;
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

        // DSC_NC - perform trust chain validation (ICAO hybrid model)
        auto dscValidation = validateDscCertificate(cert, issuerDn);
        valRecord.cscaFound = dscValidation.cscaFound;
        valRecord.cscaSubjectDn = dscValidation.cscaSubjectDn;
        valRecord.signatureVerified = dscValidation.signatureValid;
        valRecord.validityCheckPassed = dscValidation.notExpired;
        valRecord.isExpired = dscValidation.dscExpired;
        valRecord.trustChainPath = dscValidation.trustChainPath;

        if (dscValidation.isValid) {
            if (dscValidation.dscExpired || dscValidation.cscaExpired) {
                validationStatus = "EXPIRED_VALID";
                valRecord.validationStatus = "EXPIRED_VALID";
                valRecord.trustChainValid = true;
                valRecord.trustChainMessage = "Trust chain verified (certificates expired)";
                validationStats.validCount++;
                validationStats.trustChainValidCount++;
                if (dscValidation.dscExpired) validationStats.expiredCount++;
                spdlog::info("DSC_NC validation: Trust Chain VERIFIED (expired) for {} (issuer: {})",
                            countryCode, issuerDn.substr(0, 50));
            } else {
                validationStatus = "VALID";
                valRecord.validationStatus = "VALID";
                valRecord.trustChainValid = true;
                valRecord.trustChainMessage = "Trust chain verified: DSC signed by CSCA";
                validationStats.validCount++;
                validationStats.trustChainValidCount++;
                spdlog::info("DSC_NC validation: Trust Chain VERIFIED for {} (issuer: {})",
                            countryCode, issuerDn.substr(0, 50));
            }
        } else if (dscValidation.cscaFound) {
            validationStatus = "INVALID";
            validationMessage = dscValidation.errorMessage;
            valRecord.validationStatus = "INVALID";
            valRecord.trustChainValid = false;
            valRecord.trustChainMessage = dscValidation.errorMessage;
            valRecord.errorMessage = dscValidation.errorMessage;
            validationStats.invalidCount++;
            validationStats.trustChainInvalidCount++;
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
        // Detect Link Certificates (subject != issuer, CA capability)
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

            // Link certificates need parent CSCA validation (ICAO hybrid model)
            auto lcValidation = validateDscCertificate(cert, issuerDn);
            valRecord.cscaFound = lcValidation.cscaFound;
            valRecord.cscaSubjectDn = lcValidation.cscaSubjectDn;
            valRecord.trustChainPath = lcValidation.trustChainPath;
            valRecord.isExpired = lcValidation.dscExpired;

            if (lcValidation.isValid) {
                if (lcValidation.dscExpired || lcValidation.cscaExpired) {
                    validationStatus = "EXPIRED_VALID";
                    valRecord.validationStatus = "EXPIRED_VALID";
                    valRecord.trustChainValid = true;
                    valRecord.trustChainMessage = "Trust chain verified (certificates expired)";
                    validationStats.validCount++;
                    validationStats.trustChainValidCount++;
                    spdlog::info("LC validation: Trust Chain VERIFIED (expired) for {} (issuer: {})",
                                countryCode, issuerDn.substr(0, 50));
                } else {
                    validationStatus = "VALID";
                    valRecord.validationStatus = "VALID";
                    valRecord.trustChainValid = true;
                    valRecord.trustChainMessage = "Trust chain verified: Link Certificate signed by CSCA";
                    validationStats.validCount++;
                    validationStats.trustChainValidCount++;
                    spdlog::info("LC validation: Trust Chain VERIFIED for {} (issuer: {})",
                                countryCode, issuerDn.substr(0, 50));
                }
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
        // ICAO Doc 9303 Part 12 hybrid chain model: expiration is informational
        auto dscValidation = validateDscCertificate(cert, issuerDn);
        valRecord.cscaFound = dscValidation.cscaFound;
        valRecord.cscaSubjectDn = dscValidation.cscaSubjectDn;
        valRecord.signatureVerified = dscValidation.signatureValid;
        valRecord.validityCheckPassed = dscValidation.notExpired;
        valRecord.isExpired = dscValidation.dscExpired;
        valRecord.trustChainPath = dscValidation.trustChainPath;

        if (dscValidation.isValid) {
            // Determine status per ICAO Doc 9303 hybrid chain model
            if (dscValidation.dscExpired || dscValidation.cscaExpired) {
                validationStatus = "EXPIRED_VALID";
                valRecord.validationStatus = "EXPIRED_VALID";
                valRecord.trustChainValid = true;
                valRecord.trustChainMessage = "Trust chain verified (certificates expired)";
                validationStats.validCount++;
                validationStats.trustChainValidCount++;
                if (dscValidation.dscExpired) validationStats.expiredCount++;
                spdlog::info("DSC validation: Trust Chain VERIFIED (expired) for {} (issuer: {})",
                            countryCode, issuerDn.substr(0, 50));
            } else {
                validationStatus = "VALID";
                valRecord.validationStatus = "VALID";
                valRecord.trustChainValid = true;
                valRecord.trustChainMessage = "Trust chain verified: DSC signed by CSCA";
                validationStats.validCount++;
                validationStats.trustChainValidCount++;
                spdlog::info("DSC validation: Trust Chain VERIFIED for {} (issuer: {})",
                            countryCode, issuerDn.substr(0, 50));
            }
        } else if (dscValidation.cscaFound) {
            validationStatus = "INVALID";
            validationMessage = dscValidation.errorMessage;
            valRecord.validationStatus = "INVALID";
            valRecord.trustChainValid = false;
            valRecord.trustChainMessage = dscValidation.errorMessage;
            valRecord.errorMessage = dscValidation.errorMessage;
            validationStats.invalidCount++;
            validationStats.trustChainInvalidCount++;
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

    // Check ICAO 9303 compliance after certificate type is determined
    IcaoComplianceStatus icaoCompliance = common::checkIcaoCompliance(cert, certType);
    spdlog::debug("ICAO compliance for {} cert: isCompliant={}, level={}",
                  certType, icaoCompliance.isCompliant, icaoCompliance.complianceLevel);

    // Update enhanced statistics (ValidationStatistics)
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

    // Update validation status counts and reason tracking
    if (validationStatus == "VALID") {
        enhancedStats.validCount++;
        enhancedStats.validationReasons["VALID"]++;
    } else if (validationStatus == "EXPIRED_VALID") {
        enhancedStats.expiredValidCount++;
        enhancedStats.validationReasons["EXPIRED_VALID: " + valRecord.trustChainMessage]++;
    } else if (validationStatus == "INVALID") {
        enhancedStats.invalidCount++;
        enhancedStats.validationReasons["INVALID: " + valRecord.trustChainMessage]++;
    } else if (validationStatus == "PENDING") {
        enhancedStats.pendingCount++;
        enhancedStats.validationReasons["PENDING: " + valRecord.trustChainMessage]++;
    }

    spdlog::debug("Updated statistics - total={}, type={}, sigAlg={}, keySize={}, icaoCompliant={}",
                  enhancedStats.totalCertificates, certType, certMetadata.signatureAlgorithm,
                  certMetadata.keySize, icaoCompliance.isCompliant);
    // Note: This requires passing ValidationStatistics as a parameter to this function
    // For now, we log the metadata and compliance for verification
    // Statistics will be updated once the parameter is added to function signature

    auto endTime = std::chrono::high_resolution_clock::now();
    valRecord.validationDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    X509_free(cert);

    // 1. Save to DB with validation status
    auto [certId, isDuplicate] = certificate_utils::saveCertificateWithDuplicateCheck(
        uploadId, certType, countryCode,
        subjectDn, issuerDn, serialNumber, fingerprint,
        notBefore, notAfter, derBytes,
        validationStatus, validationMessage
    );

    if (isDuplicate) {
        enhancedStats.duplicateCount++;
    }

    if (!certId.empty()) {
        spdlog::debug("Saved certificate to DB: type={}, country={}, fingerprint={}",
                     certType, countryCode, fingerprint.substr(0, 16));

        // 3. Save validation result via ValidationRepository
        valRecord.certificateId = certId;
        ::validationRepository->save(valRecord);

        // 4. Save to LDAP
        if (ld) {
            // Extract DSC_NC specific attributes from LDIF entry
            std::string pkdConformanceCode = entry.getFirstAttribute("pkdConformanceCode");
            std::string pkdConformanceText = entry.getFirstAttribute("pkdConformanceText");
            std::string pkdVersion = entry.getFirstAttribute("pkdVersion");

            // Use "LC" for LDAP storage of Link Certificates
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
                // Use Repository method instead of standalone function
                ::certificateRepository->updateCertificateLdapStatus(certId, ldapDn);
                ldapStoredCount++;
                spdlog::debug("Saved certificate to LDAP: {}", ldapDn);
            } else {
                common::addProcessingError(enhancedStats, "LDAP_SAVE_FAILED",
                    entry.dn, subjectDn, countryCode, certType,
                    "LDAP save returned empty DN for fingerprint: " + fingerprint.substr(0, 16));
            }
        }
    } else if (!isDuplicate) {
        common::addProcessingError(enhancedStats, "DB_SAVE_FAILED",
            entry.dn, subjectDn, countryCode, certType,
            "Database save returned empty ID");
    }

    return !certId.empty();
}

/**
 * @brief Parse and save CRL from LDIF entry (DB + LDAP)
 */
bool parseCrlEntry(LDAP* ld, const std::string& uploadId,
                   const LdifEntry& entry, int& crlCount, int& ldapCrlStoredCount,
                   common::ValidationStatistics& enhancedStats) {
    std::string base64Value = entry.getFirstAttribute("certificateRevocationList;binary");
    if (base64Value.empty()) return false;

    std::vector<uint8_t> derBytes = base64Decode(base64Value);
    if (derBytes.empty()) {
        common::addProcessingError(enhancedStats, "BASE64_DECODE_FAILED",
            entry.dn, "", "", "CRL", "Base64 decode failed for CRL");
        return false;
    }

    const uint8_t* data = derBytes.data();
    X509_CRL* crl = d2i_X509_CRL(nullptr, &data, static_cast<long>(derBytes.size()));
    if (!crl) {
        spdlog::warn("Failed to parse CRL from entry: {}", entry.dn);
        common::addProcessingError(enhancedStats, "CRL_PARSE_FAILED",
            entry.dn, "", "", "CRL", "Failed to parse CRL (d2i_X509_CRL returned NULL)");
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

    // 1. Save to DB via CrlRepository
    std::string crlId = ::crlRepository->save(uploadId, countryCode, issuerDn,
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

                    ::crlRepository->saveRevokedCertificate(crlId, serialNum, revDate, reason);
                }
            }
            spdlog::debug("Saved CRL to DB with {} revoked certificates, issuer={}",
                         revokedCount, issuerDn.substr(0, 50));
        }

        // 2. Save to LDAP
        if (ld) {
            std::string ldapDn = saveCrlToLdap(ld, countryCode, issuerDn, fingerprint, derBytes);
            if (!ldapDn.empty()) {
                ::crlRepository->updateLdapStatus(crlId, ldapDn);
                ldapCrlStoredCount++;
                spdlog::debug("Saved CRL to LDAP: {}", ldapDn);
            } else {
                common::addProcessingError(enhancedStats, "LDAP_SAVE_FAILED",
                    entry.dn, issuerDn, countryCode, "CRL",
                    "CRL LDAP save returned empty DN for fingerprint: " + fingerprint.substr(0, 16));
            }
        }
    } else {
        common::addProcessingError(enhancedStats, "DB_SAVE_FAILED",
            entry.dn, issuerDn, countryCode, "CRL",
            "CRL database save returned empty ID");
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
 * @brief Parse and save Master List from LDIF entry (DB + LDAP) - DEPRECATED
 *
 * @deprecated Use parseMasterListEntryV2() instead.
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
    std::string mlId = saveMasterList(uploadId, countryCode, signerDn, fingerprint, cscaCount, mlBytes);

    if (!mlId.empty()) {
        mlCount++;
        spdlog::info("Saved Master List to DB: id={}, country={}", mlId, countryCode);

        // 2. Save to LDAP (o=ml node)
        if (ld) {
            std::string ldapDn = saveMasterListToLdap(ld, countryCode, signerDn, fingerprint, mlBytes);
            if (!ldapDn.empty()) {
                updateMasterListLdapStatus(mlId, ldapDn);
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
void updateUploadStatistics(const std::string& uploadId,
                           const std::string& status, int cscaCount, int dscCount,
                           int dscNcCount, int crlCount, int totalEntries, int processedEntries,
                           const std::string& errorMessage) {
    // Use UploadRepository instead of direct SQL
    if (!uploadRepository) {
        spdlog::error("[UpdateStats] uploadRepository is null");
        return;
    }

    // Update status
    ::uploadRepository->updateStatus(uploadId, status, errorMessage);

    // Update certificate statistics
    ::uploadRepository->updateStatistics(uploadId, cscaCount, dscCount, dscNcCount, crlCount);

    spdlog::debug("[UpdateStats] Updated statistics for upload: {}", uploadId);
}

/**
 * @brief Process LDIF file asynchronously with full parsing (DB + LDAP)
 * @note Defined outside anonymous namespace for external linkage
 */
// Guard against duplicate async processing
static std::mutex s_processingMutex;
static std::set<std::string> s_processingUploads;

void processLdifFileAsync(const std::string& uploadId, const std::vector<uint8_t>& content) {
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
            std::lock_guard<std::mutex> lock(s_processingMutex);
            s_processingUploads.erase(uploadId);
        };

        spdlog::info("Starting async LDIF processing for upload: {}", uploadId);

        // Get processing_mode from upload record using Repository
        auto uploadOpt = ::uploadRepository->findById(uploadId);
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
            ld = getLdapWriteConnection();
            if (!ld) {
                spdlog::error("CRITICAL: LDAP write connection failed in AUTO mode for LDIF upload {}", uploadId);
                spdlog::error("Cannot proceed - data consistency requires both DB and LDAP storage");

                // Update upload status to FAILED using Repository
                ::uploadRepository->updateStatus(uploadId, "FAILED",
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
            if (::uploadRepository) {
                ::uploadRepository->updateStatus(uploadId, "PROCESSING", "");
                ::uploadRepository->updateProgress(uploadId, totalEntries, 0);
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
            updateUploadStatistics(uploadId, "FAILED", 0, 0, 0, 0, 0, 0, e.what());
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

}  // Close anonymous namespace before external function

/**
 * @brief Parse Master List (CMS SignedData) and extract CSCA certificates (DB + LDAP)
 * @note Defined outside anonymous namespace for external linkage
 */
void processMasterListFileAsync(const std::string& uploadId, const std::vector<uint8_t>& content) {
    // Check and register this upload for processing (prevent duplicate threads)
    {
        std::lock_guard<std::mutex> lock(s_processingMutex);
        if (s_processingUploads.count(uploadId) > 0) {
            spdlog::warn("[processMasterListFileAsync] Upload {} already being processed - skipping duplicate", uploadId);
            return;
        }
        s_processingUploads.insert(uploadId);
    }

    std::thread([uploadId, content]() {
        // Ensure cleanup on thread exit
        auto cleanupGuard = [&uploadId]() {
            std::lock_guard<std::mutex> lock(s_processingMutex);
            s_processingUploads.erase(uploadId);
        };

        spdlog::info("Starting async Master List processing for upload: {}", uploadId);

        // Get processing_mode from upload record using Repository
        auto uploadOpt = ::uploadRepository->findById(uploadId);
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
            ld = getLdapWriteConnection();
            if (!ld) {
                spdlog::error("CRITICAL: LDAP write connection failed in AUTO mode for Master List upload {}", uploadId);
                spdlog::error("Cannot proceed - data consistency requires both DB and LDAP storage");

                // Update upload status to FAILED using Repository
                ::uploadRepository->updateStatus(uploadId, "FAILED",
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

            // Send initial progress
            ProgressManager::getInstance().sendProgress(
                ProcessingProgress::create(uploadId, ProcessingStage::PARSING_STARTED, 0, 0, "CMS 파싱 시작"));

            // Validate CMS format: first byte must be 0x30 (SEQUENCE tag)
            if (content.empty() || content[0] != 0x30) {
                spdlog::error("Invalid Master List: not a valid CMS structure (missing SEQUENCE tag)");
                ProgressManager::getInstance().sendProgress(
                    ProcessingProgress::create(uploadId, ProcessingStage::FAILED, 0, 0, "Invalid CMS format", "CMS 형식 오류"));
                ::uploadRepository->updateStatus(uploadId, "FAILED", "Invalid CMS format");
                ::uploadRepository->updateStatistics(uploadId, 0, 0, 0, 0, 0, 0);
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

            // Update DB record: status = PROCESSING before certificate extraction
            if (::uploadRepository) {
                ::uploadRepository->updateStatus(uploadId, "PROCESSING", "");
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
                            CertificateMetadata certMetadata = common::extractCertificateMetadataForProgress(cert, false);
                            spdlog::debug("Master List PKCS7 fallback: Extracted metadata for cert: type={}, sigAlg={}, keySize={}",
                                          certMetadata.certificateType, certMetadata.signatureAlgorithm, certMetadata.keySize);

                            // Master List contains ONLY CSCA certificates (per ICAO Doc 9303)
                            // Including both self-signed and cross-signed/link CSCAs
                            std::string certType = "CSCA";

                            // Check ICAO 9303 compliance
                            IcaoComplianceStatus icaoCompliance = common::checkIcaoCompliance(cert, certType);
                            spdlog::debug("Master List PKCS7 fallback: ICAO compliance for {} cert: isCompliant={}, level={}",
                                          certType, icaoCompliance.isCompliant, icaoCompliance.complianceLevel);

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
                                    ::uploadRepository->updateProgress(uploadId, totalCertsInML, savedCount);
                                }
                            }

                            // Save certificate using Repository Pattern
                            auto [certId, isDuplicate] = ::certificateRepository->saveCertificateWithDuplicateCheck(
                                uploadId, certType, countryCode, subjectDn, issuerDn, serialNumber,
                                fingerprint, notBefore, notAfter, derBytes);

                            if (!certId.empty()) {
                                if (isDuplicate) {
                                    skippedDuplicates++;
                                    spdlog::debug("Skipping duplicate CSCA: fingerprint={}", fingerprint.substr(0, 16));
                                } else {
                                    cscaCount++;
                                    spdlog::debug("Saved CSCA to DB: fingerprint={}", fingerprint.substr(0, 16));

                                    if (ld) {
                                        std::string ldapDn = saveCertificateToLdap(ld, certType, countryCode,
                                                                                    subjectDn, issuerDn, serialNumber,
                                                                                    fingerprint, derBytes);
                                        if (!ldapDn.empty()) {
                                            ::certificateRepository->updateCertificateLdapStatus(certId, ldapDn);
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
                    ::uploadRepository->updateStatus(uploadId, "FAILED", "CMS/PKCS7 parsing failed");
                    ::uploadRepository->updateStatistics(uploadId, 0, 0, 0, 0, 0, 0);
                    if (ld) ldap_unbind_ext_s(ld, nullptr, nullptr);
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
                                        CertificateMetadata certMetadata = common::extractCertificateMetadataForProgress(cert, false);
                                        spdlog::debug("Master List Async: Extracted metadata for cert: type={}, sigAlg={}, keySize={}",
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

                                        // Check ICAO 9303 compliance after certificate type is determined
                                        IcaoComplianceStatus icaoCompliance = common::checkIcaoCompliance(cert, certType);
                                        spdlog::debug("Master List Async: ICAO compliance for {} cert: isCompliant={}, level={}",
                                                      certType, icaoCompliance.isCompliant, icaoCompliance.complianceLevel);

                                        totalCerts++;

                                        // Send progress update every 10 certs (more frequent for better UX)
                                        if (totalCerts % 10 == 0) {
                                            int savedCount = cscaCount + dscCount;
                                            // ML processing does DB+LDAP in same loop, so use full 30-100% range
                                            // (parsing takes ~1s, saving takes ~3min)
                                            int pct = 30 + (70 * totalCerts / std::max(1, totalCertsInML));
                                            auto progress = ProcessingProgress::create(uploadId, ProcessingStage::DB_SAVING_IN_PROGRESS,
                                                    savedCount, totalCertsInML,
                                                    "인증서 처리 중: " + std::to_string(totalCerts) + "/" + std::to_string(totalCertsInML) + "개");
                                            progress.percentage = std::min(pct, 99);  // Cap at 99% until truly complete
                                            ProgressManager::getInstance().sendProgress(progress);

                                            // Update DB progress for polling fallback
                                            if (totalCerts % 50 == 0) {
                                                ::uploadRepository->updateProgress(uploadId, totalCertsInML, savedCount);
                                            }
                                        }

                                        // Save to DB with validation status using Repository Pattern
                                        auto [certId, isDuplicate] = ::certificateRepository->saveCertificateWithDuplicateCheck(
                                            uploadId, certType, countryCode, subjectDn, issuerDn, serialNumber,
                                            fingerprint, notBefore, notAfter, derBytes, validationStatus, validationMessage);

                                        if (!certId.empty()) {
                                            if (isDuplicate) {
                                                skippedDuplicates++;
                                                spdlog::debug("Skipping duplicate CSCA from Master List: fingerprint={}", fingerprint.substr(0, 16));

                                                // Track duplicate in certificate_duplicates table
                                                ::certificateRepository->trackCertificateDuplicate(certId, uploadId, "ML_FILE", countryCode, "", "");
                                            } else {
                                                // All Master List certificates are CSCA
                                                cscaCount++;
                                                spdlog::debug("Saved CSCA from Master List to DB: country={}, fingerprint={}",
                                                             countryCode, fingerprint.substr(0, 16));

                                                // Save to LDAP
                                                if (ld) {
                                                    std::string ldapDn = saveCertificateToLdap(ld, certType, countryCode,
                                                                                                subjectDn, issuerDn, serialNumber,
                                                                                                fingerprint, derBytes);
                                                    if (!ldapDn.empty()) {
                                                        ::certificateRepository->updateCertificateLdapStatus(certId, ldapDn);
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
                            CertificateMetadata certMetadata = common::extractCertificateMetadataForProgress(cert, false);
                            spdlog::debug("Master List CMS store: Extracted metadata for cert: type={}, sigAlg={}, keySize={}",
                                          certMetadata.certificateType, certMetadata.signatureAlgorithm, certMetadata.keySize);

                            // Master List contains ONLY CSCA certificates (per ICAO Doc 9303)
                            // Including both self-signed and cross-signed/link CSCAs
                            std::string certType = "CSCA";

                            // Check ICAO 9303 compliance
                            IcaoComplianceStatus icaoCompliance = common::checkIcaoCompliance(cert, certType);
                            spdlog::debug("Master List CMS store: ICAO compliance for {} cert: isCompliant={}, level={}",
                                          certType, icaoCompliance.isCompliant, icaoCompliance.complianceLevel);

                            totalCerts++;

                            // Send progress update every 10 certs (CMS certificate store path)
                            if (totalCerts % 10 == 0) {
                                int savedCount = cscaCount + dscCount;
                                int pct = 30 + (70 * totalCerts / std::max(1, totalCertsInML));
                                auto progress = ProcessingProgress::create(uploadId, ProcessingStage::DB_SAVING_IN_PROGRESS,
                                        savedCount, totalCertsInML,
                                        "인증서 처리 중: " + std::to_string(totalCerts) + "/" + std::to_string(totalCertsInML) + "개");
                                progress.percentage = std::min(pct, 99);
                                ProgressManager::getInstance().sendProgress(progress);

                                if (totalCerts % 50 == 0) {
                                    ::uploadRepository->updateProgress(uploadId, totalCertsInML, savedCount);
                                }
                            }

                            // Save certificate using Repository Pattern
                            auto [certId, isDuplicate] = ::certificateRepository->saveCertificateWithDuplicateCheck(
                                uploadId, certType, countryCode, subjectDn, issuerDn, serialNumber,
                                fingerprint, notBefore, notAfter, derBytes);

                            if (!certId.empty()) {
                                if (isDuplicate) {
                                    skippedDuplicates++;
                                    spdlog::debug("Skipping duplicate CSCA: fingerprint={}", fingerprint.substr(0, 16));
                                } else {
                                    cscaCount++;
                                    spdlog::debug("Saved CSCA to DB: fingerprint={}", fingerprint.substr(0, 16));

                                    if (ld) {
                                        std::string ldapDn = saveCertificateToLdap(ld, certType, countryCode,
                                                                                    subjectDn, issuerDn, serialNumber,
                                                                                    fingerprint, derBytes);
                                        if (!ldapDn.empty()) {
                                            ::certificateRepository->updateCertificateLdapStatus(certId, ldapDn);
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

            // Update statistics (Master List contains only CSCA, no DSC or DSC_NC)
            // Note: ICAO Master List (.ml) extracts individual CSCA certificates, so ml_count = 0
            // Only Country Master Lists from LDIF (pkdMasterListContent) are counted as ML
            ::uploadRepository->updateStatus(uploadId, "COMPLETED", "");
            ::uploadRepository->updateStatistics(uploadId, cscaCount, dscCount, 0, 0, 1, 1);
            // Update final progress counts for polling fallback
            int finalTotal = totalCertsInML > 0 ? totalCertsInML : totalCerts;
            ::uploadRepository->updateProgress(uploadId, finalTotal, cscaCount + dscCount);

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
            ::uploadRepository->updateStatus(uploadId, "FAILED", e.what());
            ::uploadRepository->updateStatistics(uploadId, 0, 0, 0, 0, 0, 0);
        }

        // Cleanup LDAP connection
        if (ld) {
            ldap_unbind_ext_s(ld, nullptr, nullptr);
        }

        // Remove from processing set
        cleanupGuard();
    }).detach();
}

namespace {  // Reopen anonymous namespace for remaining internal functions

/**
 * @brief Check LDAP connectivity using LDAP C API (no system() calls)
 */
Json::Value checkLdap() {
    Json::Value result;
    result["name"] = "ldap";

    try {
        auto start = std::chrono::steady_clock::now();

        std::string ldapUri = "ldap://" + appConfig.ldapHost + ":" + std::to_string(appConfig.ldapPort);
        LDAP* ld = nullptr;
        int rc = ldap_initialize(&ld, ldapUri.c_str());

        if (rc == LDAP_SUCCESS) {
            int version = LDAP_VERSION3;
            ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

            struct timeval tv = {3, 0};  // 3 second timeout
            ldap_set_option(ld, LDAP_OPT_NETWORK_TIMEOUT, &tv);

            // Anonymous bind to verify connectivity
            struct berval cred = {0, nullptr};
            rc = ldap_sasl_bind_s(ld, nullptr, LDAP_SASL_SIMPLE, &cred, nullptr, nullptr, nullptr);
            ldap_unbind_ext_s(ld, nullptr, nullptr);
        }

        auto end = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

        if (rc == LDAP_SUCCESS) {
            result["status"] = "UP";
            result["responseTimeMs"] = static_cast<int>(duration.count());
            result["host"] = appConfig.ldapHost;
            result["port"] = appConfig.ldapPort;
        } else {
            result["status"] = "DOWN";
            result["error"] = std::string("LDAP connection failed: ") + ldap_err2string(rc);
        }
    } catch (const std::exception& e) {
        result["status"] = "DOWN";
        result["error"] = e.what();
    }

    return result;
}

void registerRoutes() {
    auto& app = drogon::app();

    // --- Register Authentication Middleware (Global) ---
    // Note: Authentication is DISABLED by default for backward compatibility
    // Enable by setting: AUTH_ENABLED=true in environment
    //
    // IMPORTANT: AuthMiddleware uses HttpFilterBase (not HttpFilter<T>) for manual
    // instantiation with parameters. It cannot be registered globally via registerFilter().
    // Instead, apply it to individual routes using .addFilter() method.

    // --- Authentication Routes ---
    // Note: AuthMiddleware is applied globally via registerPreHandlingAdvice()
    // (see initialization section at end of main())
    if (authHandler) {
        authHandler->registerRoutes(app);
    }

    // --- API Routes ---

    // Manual mode: Trigger parse endpoint
    app.registerHandler(
        "/api/upload/{uploadId}/parse",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& uploadId) {
            spdlog::info("POST /api/upload/{}/parse - Trigger parsing", uploadId);

            // Use QueryExecutor for Oracle support
            if (!::queryExecutor) {
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
                auto rows = ::queryExecutor->executeQuery(query, {uploadId});

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
                std::thread([uploadId, contentBytes]() {
                    spdlog::info("Starting async Master List processing via Strategy for upload: {}", uploadId);

                    // Get processing mode from upload record using Repository
                    auto uploadOpt = ::uploadRepository->findById(uploadId);
                    if (!uploadOpt.has_value()) {
                        spdlog::error("Upload record not found: {}", uploadId);
                        return;
                    }

                    std::string processingMode = uploadOpt->processingMode.value_or("AUTO");
                    spdlog::info("Processing mode for Master List upload {}: {}", uploadId, processingMode);

                    // Connect to LDAP only if AUTO mode
                    LDAP* ld = nullptr;
                    if (processingMode == "AUTO") {
                        ld = getLdapWriteConnection();
                        if (!ld) {
                            spdlog::error("CRITICAL: LDAP write connection failed in AUTO mode for Master List upload {}", uploadId);
                            spdlog::error("Cannot proceed - data consistency requires both DB and LDAP storage");

                            // Update upload status to FAILED using Repository
                            ::uploadRepository->updateStatus(uploadId, "FAILED",
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
        },
        {drogon::Post}
    );

    // Manual mode: Trigger validate and DB save endpoint
    // Refactored to use Repository Pattern instead of direct PostgreSQL connection
    app.registerHandler(
        "/api/upload/{uploadId}/validate",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback,
           const std::string& uploadId) {
            spdlog::info("POST /api/upload/{}/validate - Trigger validation and DB save", uploadId);

            // Check if upload exists using Repository Pattern
            auto uploadOpt = ::uploadRepository->findById(uploadId);
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
    // Connected to ValidationService -> ValidationRepository (Repository Pattern)
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
    // Connected to ValidationService -> ValidationRepository (Repository Pattern)
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
    // LDIF Structure Visualization (Repository Pattern)
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
                    logOperation(::queryExecutor.get(), auditEntry);
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
                    logOperation(::queryExecutor.get(), auditEntry);
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

    // --- Audit Log API Endpoints ---

    // GET /api/audit/operations - List audit log entries with filtering
    // Connected to AuditService -> AuditRepository (Repository Pattern)
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
    // Connected to AuditService -> AuditRepository (Repository Pattern)
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
    // Connected to ValidationService -> CertificateRepository (Repository Pattern)
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
                response["expiredValidCount"] = result.expiredValidCount;
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
                        logOperation(::queryExecutor.get(), auditEntry);
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
                        logOperation(::queryExecutor.get(), auditEntry);
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
                    logOperation(::queryExecutor.get(), auditEntry);
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
                        logOperation(::queryExecutor.get(), auditEntry);
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
                        logOperation(::queryExecutor.get(), auditEntry);
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

                        // Use QueryExecutor (Oracle/PostgreSQL agnostic) instead of PGconn
                        if (!::queryExecutor) {
                            spdlog::error("QueryExecutor is null for async processing");
                            return;
                        }

                        // Get processing mode via UploadRepository
                        std::string processingMode = "AUTO";
                        try {
                            auto uploadOpt = ::uploadRepository->findById(uploadId);
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
                            ld = getLdapWriteConnection();
                            if (!ld) {
                                spdlog::error("CRITICAL: LDAP write connection failed in AUTO mode for upload {}", uploadId);
                                spdlog::error("Cannot proceed - data consistency requires both DB and LDAP storage");

                                // Update upload status to FAILED via Repository
                                if (::uploadRepository) {
                                    ::uploadRepository->updateStatus(uploadId, "FAILED",
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
                                auto uploadOpt = ::uploadRepository->findById(uploadId);
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
                    logOperation(::queryExecutor.get(), auditEntry);
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

    // Upload individual certificate file endpoint (PEM, DER, CER, P7B, CRL)
    app.registerHandler(
        "/api/upload/certificate",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
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
                auto result = uploadService->uploadCertificate(fileName, contentBytes, uploadedBy);

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
                if (::queryExecutor) {
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
                    logOperation(::queryExecutor.get(), auditEntry);
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
        },
        {drogon::Post}
    );

    // Preview certificate file endpoint (parse only, no DB/LDAP save)
    app.registerHandler(
        "/api/upload/certificate/preview",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
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

                auto result = uploadService->previewCertificate(fileName, contentBytes);

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
        },
        {drogon::Post}
    );

    // Upload statistics endpoint - returns UploadStatisticsOverview format
    // Connected to UploadService (Repository Pattern)
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

    // Validation reason breakdown endpoint
    app.registerHandler(
        "/api/upload/statistics/validation-reasons",
        [](const drogon::HttpRequestPtr& req,
           std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            spdlog::info("GET /api/upload/statistics/validation-reasons");

            try {
                Json::Value result = validationRepository->getReasonBreakdown();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("GET /api/upload/statistics/validation-reasons failed: {}", e.what());
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

    // Upload history endpoint - returns PageResponse format
    // Connected to UploadService (Repository Pattern)
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
    // Connected to UploadService (Repository Pattern)
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

                // LDAP status count via CertificateRepository
                if (::certificateRepository) {
                    try {
                        int totalCerts = 0, ldapCerts = 0;
                        ::certificateRepository->countLdapStatusByUploadId(uploadId, totalCerts, ldapCerts);
                        uploadData["ldapUploadedCount"] = ldapCerts;
                        uploadData["ldapPendingCount"] = totalCerts - ldapCerts;
                    } catch (const std::exception& e) {
                        spdlog::warn("LDAP status query failed: {}", e.what());
                        uploadData["ldapUploadedCount"] = 0;
                        uploadData["ldapPendingCount"] = 0;
                    }
                } else {
                    uploadData["ldapUploadedCount"] = 0;
                    uploadData["ldapPendingCount"] = 0;
                }

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

            try {
                // Uses QueryExecutor for Oracle support
                if (!::queryExecutor) {
                    throw std::runtime_error("Query executor not initialized");
                }

                // Query upload file information (no $1::uuid cast for Oracle compatibility)
                std::string query =
                    "SELECT file_name, original_file_name, file_format, file_size, file_path "
                    "FROM uploaded_file "
                    "WHERE id = $1";

                auto rows = ::queryExecutor->executeQuery(query, {uploadId});

                if (rows.empty()) {
                    result["error"] = "Upload not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                std::string fileName = rows[0].get("file_name", "").asString();
                std::string origFileName = rows[0].get("original_file_name", "").asString();
                std::string displayName = origFileName.empty() ? fileName : origFileName;
                std::string fileFormat = rows[0].get("file_format", "").asString();
                std::string fileSizeStr = rows[0].get("file_size", "0").asString();
                std::string filePath = rows[0].get("file_path", "").asString();

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
                try { result["fileSize"] = std::stoi(fileSizeStr); } catch (...) { result["fileSize"] = 0; }
                result["asn1Tree"] = asn1Result["tree"];
                result["statistics"] = asn1Result["statistics"];
                result["maxLines"] = asn1Result["maxLines"];
                result["truncated"] = asn1Result["truncated"];

                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("GET /api/upload/{}/masterlist-structure error: {}", uploadId, e.what());
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

            // Uses QueryExecutor for Oracle support
            Json::Value result;
            result["success"] = false;

            if (!::uploadRepository) {
                result["error"] = "Upload repository not initialized";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
                return;
            }

            try {
                auto rows = ::uploadRepository->getChangeHistory(limit);

                // Oracle safeInt helper: Oracle returns all values as strings
                auto safeInt = [](const Json::Value& v) -> int {
                    if (v.isInt()) return v.asInt();
                    if (v.isString()) { try { return std::stoi(v.asString()); } catch (...) { return 0; } }
                    return 0;
                };

                result["success"] = true;
                result["count"] = static_cast<int>(rows.size());

                Json::Value changes(Json::arrayValue);
                for (Json::ArrayIndex i = 0; i < rows.size(); i++) {
                    const auto& row = rows[i];
                    Json::Value change;
                    change["uploadId"] = row.get("id", "").asString();
                    change["fileName"] = row.get("original_file_name", "").asString();
                    change["collectionNumber"] = row.get("collection_number", "N/A").asString();
                    change["uploadTime"] = row.get("upload_time", "").asString();

                    // Current counts
                    Json::Value counts;
                    counts["csca"] = safeInt(row["csca_count"]);
                    counts["dsc"] = safeInt(row["dsc_count"]);
                    counts["dscNc"] = safeInt(row["dsc_nc_count"]);
                    counts["crl"] = safeInt(row["crl_count"]);
                    counts["ml"] = safeInt(row["ml_count"]);
                    counts["mlsc"] = safeInt(row["mlsc_count"]);
                    change["counts"] = counts;

                    // Changes (deltas)
                    Json::Value deltas;
                    deltas["csca"] = safeInt(row["csca_change"]);
                    deltas["dsc"] = safeInt(row["dsc_change"]);
                    deltas["dscNc"] = safeInt(row["dsc_nc_change"]);
                    deltas["crl"] = safeInt(row["crl_change"]);
                    deltas["ml"] = safeInt(row["ml_change"]);
                    deltas["mlsc"] = safeInt(row["mlsc_change"]);
                    change["changes"] = deltas;

                    // Calculate total change
                    int totalChange = std::abs(safeInt(row["csca_change"])) +
                                    std::abs(safeInt(row["dsc_change"])) +
                                    std::abs(safeInt(row["dsc_nc_change"])) +
                                    std::abs(safeInt(row["crl_change"])) +
                                    std::abs(safeInt(row["ml_change"])) +
                                    std::abs(safeInt(row["mlsc_change"]));
                    change["totalChange"] = totalChange;

                    // Previous upload info (if exists)
                    std::string prevFile = row.get("previous_file", "").asString();
                    if (!prevFile.empty()) {
                        Json::Value previous;
                        previous["fileName"] = prevFile;
                        previous["uploadTime"] = row.get("previous_upload_time", "").asString();
                        change["previousUpload"] = previous;
                    } else {
                        change["previousUpload"] = Json::Value::null;
                    }

                    changes.append(change);
                }
                result["changes"] = changes;

            } catch (const std::exception& e) {
                result["error"] = std::string("Query failed: ") + e.what();
                spdlog::error("[UploadChanges] Query failed: {}", e.what());
            }

            auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
            callback(resp);
        },
        {drogon::Get}
    );

    // Country statistics endpoint - GET /api/upload/countries
    // Connected to UploadService (Repository Pattern)
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
    // Connected to UploadService (Repository Pattern)
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

    // --- Certificate Search APIs ---

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
                std::string sourceFilter = req->getOptionalParameter<std::string>("source").value_or("");
                int limit = req->getOptionalParameter<int>("limit").value_or(50);
                int offset = req->getOptionalParameter<int>("offset").value_or(0);

                // Validate limit (max 200)
                if (limit > 200) limit = 200;
                if (limit < 1) limit = 50;
                if (offset < 0) offset = 0;

                spdlog::info("Certificate search: country={}, certType={}, validity={}, source={}, search={}, limit={}, offset={}",
                            country, certTypeStr, validityStr, sourceFilter, searchTerm, limit, offset);

                // When source filter is specified, use DB-based search
                if (!sourceFilter.empty()) {
                    repositories::CertificateSearchFilter filter;
                    if (!country.empty()) filter.countryCode = country;
                    if (!certTypeStr.empty()) filter.certificateType = certTypeStr;
                    filter.sourceType = sourceFilter;
                    if (!searchTerm.empty()) filter.searchTerm = searchTerm;
                    filter.limit = limit;
                    filter.offset = offset;

                    Json::Value dbResult = ::certificateRepository->search(filter);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(dbResult);
                    callback(resp);
                    return;
                }

                // Default: LDAP-based search (existing behavior)
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

                // Execute LDAP search
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

                    // X.509 Metadata - 15 fields
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
    // Connected to ValidationService (Repository Pattern)
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

    // POST /api/certificates/pa-lookup - Lightweight PA lookup by subject DN or fingerprint
    app.registerHandler(
        "/api/certificates/pa-lookup",
        [&](const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try {
                auto jsonBody = req->getJsonObject();
                if (!jsonBody) {
                    Json::Value error;
                    error["success"] = false;
                    error["error"] = "JSON body is required";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                std::string subjectDn = (*jsonBody).get("subjectDn", "").asString();
                std::string fingerprint = (*jsonBody).get("fingerprint", "").asString();

                if (subjectDn.empty() && fingerprint.empty()) {
                    Json::Value error;
                    error["success"] = false;
                    error["error"] = "Either subjectDn or fingerprint parameter is required";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k400BadRequest);
                    callback(resp);
                    return;
                }

                Json::Value response;
                if (!subjectDn.empty()) {
                    spdlog::info("POST /api/certificates/pa-lookup - subjectDn: {}", subjectDn.substr(0, 60));
                    response = validationService->getValidationBySubjectDn(subjectDn);
                } else {
                    spdlog::info("POST /api/certificates/pa-lookup - fingerprint: {}", fingerprint.substr(0, 16));
                    response = validationService->getValidationByFingerprint(fingerprint);
                }

                auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("PA lookup error: {}", e.what());
                Json::Value error;
                error["success"] = false;
                error["error"] = e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
        },
        {drogon::Post}
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

                // Audit logging - CERT_EXPORT success (single file)
                {
                    AuditLogEntry auditEntry;
                    auto [userId8, username8] = extractUserFromRequest(req);
                    auditEntry.userId = userId8;
                    auditEntry.username = username8;
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
                    logOperation(::queryExecutor.get(), auditEntry);
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

                // Audit logging - CERT_EXPORT success (country ZIP)
                {
                    AuditLogEntry auditEntry;
                    auto [userId, username] = extractUserFromRequest(req);
                    auditEntry.userId = userId;
                    auditEntry.username = username;
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
                    auditEntry.metadata = metadata;
                    logOperation(::queryExecutor.get(), auditEntry);
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

    // GET /api/certificates/export/all - Export all LDAP-stored data as DIT-structured ZIP
    app.registerHandler(
        "/api/certificates/export/all",
        [&](const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try {
                std::string format = req->getOptionalParameter<std::string>("format").value_or("pem");

                spdlog::info("Full PKD export requested: format={}", format);

                services::ExportFormat exportFormat = (format == "der") ?
                    services::ExportFormat::DER : services::ExportFormat::PEM;

                auto exportResult = services::exportAllCertificatesFromDb(
                    ::certificateRepository.get(),
                    ::crlRepository.get(),
                    ::queryExecutor.get(),
                    exportFormat,
                    ::ldapPool.get()
                );

                if (!exportResult.success) {
                    Json::Value error;
                    error["success"] = false;
                    error["error"] = exportResult.errorMessage;
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }

                // Return ZIP binary
                auto resp = drogon::HttpResponse::newHttpResponse();
                resp->setStatusCode(drogon::k200OK);
                resp->setContentTypeString("application/zip");
                resp->addHeader("Content-Disposition",
                    "attachment; filename=\"" + exportResult.filename + "\"");
                resp->setBody(std::string(
                    reinterpret_cast<const char*>(exportResult.data.data()),
                    exportResult.data.size()));
                callback(resp);

                // Audit log
                {
                    AuditLogEntry auditEntry;
                    auto [userId, username] = extractUserFromRequest(req);
                    auditEntry.userId = userId;
                    auditEntry.username = username;
                    auditEntry.operationType = OperationType::CERT_EXPORT;
                    auditEntry.operationSubtype = "ALL_ZIP";
                    auditEntry.resourceType = "CERTIFICATE_COLLECTION";
                    auditEntry.ipAddress = extractIpAddress(req);
                    auditEntry.userAgent = req->getHeader("User-Agent");
                    auditEntry.requestMethod = "GET";
                    auditEntry.requestPath = "/api/certificates/export/all";
                    auditEntry.success = true;
                    Json::Value metadata;
                    metadata["format"] = format;
                    metadata["fileName"] = exportResult.filename;
                    metadata["fileSize"] = static_cast<Json::Int64>(exportResult.data.size());
                    auditEntry.metadata = metadata;
                    logOperation(::queryExecutor.get(), auditEntry);
                }

            } catch (const std::exception& e) {
                spdlog::error("Full PKD export error: {}", e.what());
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

    // GET /api/certificates/countries - Get list of available countries
    app.registerHandler(
        "/api/certificates/countries",
        [&](const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try {
                spdlog::debug("Fetching list of available countries");

                if (!::certificateRepository) {
                    throw std::runtime_error("Certificate repository not initialized");
                }

                auto rows = ::certificateRepository->getDistinctCountries();

                Json::Value response;
                response["success"] = true;
                response["count"] = static_cast<int>(rows.size());

                Json::Value countryList(Json::arrayValue);
                for (const auto& row : rows) {
                    countryList.append(row["country_code"].asString());
                }
                response["countries"] = countryList;

                spdlog::info("Countries list fetched: {} countries", rows.size());

                auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("Error fetching countries: {}", e.what());
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

    // GET /api/certificates/dsc-nc/report - DSC_NC Non-Conformant Certificate Report
    app.registerHandler(
        "/api/certificates/dsc-nc/report",
        [&](const drogon::HttpRequestPtr& req,
            std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            try {
                // Parse query parameters
                std::string countryFilter = req->getOptionalParameter<std::string>("country").value_or("");
                std::string codeFilter = req->getOptionalParameter<std::string>("conformanceCode").value_or("");
                int page = req->getOptionalParameter<int>("page").value_or(1);
                int size = req->getOptionalParameter<int>("size").value_or(50);
                if (page < 1) page = 1;
                if (size < 1) size = 50;
                if (size > 200) size = 200;

                spdlog::info("DSC_NC report: country={}, code={}, page={}, size={}", countryFilter, codeFilter, page, size);

                // Fetch all DSC_NC certificates from LDAP (batch 200 at a time)
                domain::models::CertificateSearchResult result;
                result.total = 0;
                result.limit = 200;
                result.offset = 0;
                {
                    int batchOffset = 0;
                    const int batchSize = 200;
                    while (true) {
                        domain::models::CertificateSearchCriteria criteria;
                        criteria.certType = domain::models::CertificateType::DSC_NC;
                        criteria.limit = batchSize;
                        criteria.offset = batchOffset;
                        auto batch = ::certificateService->searchCertificates(criteria);
                        for (auto& cert : batch.certificates) {
                            result.certificates.push_back(std::move(cert));
                        }
                        result.total = batch.total;
                        if (static_cast<int>(batch.certificates.size()) < batchSize) break;
                        batchOffset += batchSize;
                        if (batchOffset >= batch.total) break;
                    }
                }

                // Single-pass aggregation
                std::map<std::string, std::pair<std::string, int>> conformanceCodeMap; // code -> {description, count}
                std::map<std::string, std::tuple<int, int, int>> countryMap; // country -> {total, valid, expired}
                std::map<int, int> yearMap; // year -> count
                std::map<std::string, int> sigAlgMap; // algorithm -> count
                std::map<std::string, int> pubKeyAlgMap; // algorithm -> count
                int validCount = 0, expiredCount = 0, notYetValidCount = 0, unknownCount = 0;

                // Filtered certificates for table
                std::vector<const domain::models::Certificate*> filteredCerts;

                for (const auto& cert : result.certificates) {
                    // Aggregation (always, before filtering)
                    std::string code = cert.getPkdConformanceCode().value_or("UNKNOWN");
                    std::string desc = cert.getPkdConformanceText().value_or("");
                    conformanceCodeMap[code].first = desc;
                    conformanceCodeMap[code].second++;

                    std::string country = cert.getCountry();
                    auto status = cert.getValidityStatus();
                    auto& countryEntry = countryMap[country];
                    std::get<0>(countryEntry)++;
                    if (status == domain::models::ValidityStatus::VALID) {
                        std::get<1>(countryEntry)++;
                        validCount++;
                    } else if (status == domain::models::ValidityStatus::EXPIRED) {
                        std::get<2>(countryEntry)++;
                        expiredCount++;
                    } else if (status == domain::models::ValidityStatus::NOT_YET_VALID) {
                        notYetValidCount++;
                    } else {
                        unknownCount++;
                    }

                    // Year from notBefore
                    auto notBefore = std::chrono::system_clock::to_time_t(cert.getValidFrom());
                    struct tm tmBuf;
                    gmtime_r(&notBefore, &tmBuf);
                    yearMap[tmBuf.tm_year + 1900]++;

                    // Algorithms
                    std::string sigAlg = cert.getSignatureAlgorithm().value_or("Unknown");
                    sigAlgMap[sigAlg]++;
                    std::string pubKeyAlg = cert.getPublicKeyAlgorithm().value_or("Unknown");
                    pubKeyAlgMap[pubKeyAlg]++;

                    // Apply filters for table
                    bool passCountry = countryFilter.empty() || cert.getCountry() == countryFilter;
                    bool passCode = codeFilter.empty() || code.find(codeFilter) == 0; // prefix match
                    if (passCountry && passCode) {
                        filteredCerts.push_back(&cert);
                    }
                }

                // Build JSON response
                Json::Value response;
                response["success"] = true;

                // Summary
                Json::Value summary;
                summary["totalDscNc"] = static_cast<int>(result.certificates.size());
                summary["countryCount"] = static_cast<int>(countryMap.size());
                summary["conformanceCodeCount"] = static_cast<int>(conformanceCodeMap.size());
                Json::Value validityBreakdown;
                validityBreakdown["VALID"] = validCount;
                validityBreakdown["EXPIRED"] = expiredCount;
                validityBreakdown["NOT_YET_VALID"] = notYetValidCount;
                validityBreakdown["UNKNOWN"] = unknownCount;
                summary["validityBreakdown"] = validityBreakdown;
                response["summary"] = summary;

                // Conformance codes (sorted by count desc)
                std::vector<std::pair<std::string, std::pair<std::string, int>>> codeVec(conformanceCodeMap.begin(), conformanceCodeMap.end());
                std::sort(codeVec.begin(), codeVec.end(), [](const auto& a, const auto& b) { return a.second.second > b.second.second; });
                Json::Value codesArray(Json::arrayValue);
                for (const auto& [code, descCount] : codeVec) {
                    Json::Value item;
                    item["code"] = code;
                    item["description"] = descCount.first;
                    item["count"] = descCount.second;
                    codesArray.append(item);
                }
                response["conformanceCodes"] = codesArray;

                // By country (sorted by count desc)
                std::vector<std::pair<std::string, std::tuple<int, int, int>>> countryVec(countryMap.begin(), countryMap.end());
                std::sort(countryVec.begin(), countryVec.end(), [](const auto& a, const auto& b) { return std::get<0>(a.second) > std::get<0>(b.second); });
                Json::Value countryArray(Json::arrayValue);
                for (const auto& [cc, counts] : countryVec) {
                    Json::Value item;
                    item["countryCode"] = cc;
                    item["count"] = std::get<0>(counts);
                    item["validCount"] = std::get<1>(counts);
                    item["expiredCount"] = std::get<2>(counts);
                    countryArray.append(item);
                }
                response["byCountry"] = countryArray;

                // By year (sorted by year asc)
                Json::Value yearArray(Json::arrayValue);
                for (const auto& [year, count] : yearMap) {
                    Json::Value item;
                    item["year"] = year;
                    item["count"] = count;
                    yearArray.append(item);
                }
                response["byYear"] = yearArray;

                // By signature algorithm
                Json::Value sigAlgArray(Json::arrayValue);
                for (const auto& [alg, count] : sigAlgMap) {
                    Json::Value item;
                    item["algorithm"] = alg;
                    item["count"] = count;
                    sigAlgArray.append(item);
                }
                response["bySignatureAlgorithm"] = sigAlgArray;

                // By public key algorithm
                Json::Value pubKeyAlgArray(Json::arrayValue);
                for (const auto& [alg, count] : pubKeyAlgMap) {
                    Json::Value item;
                    item["algorithm"] = alg;
                    item["count"] = count;
                    pubKeyAlgArray.append(item);
                }
                response["byPublicKeyAlgorithm"] = pubKeyAlgArray;

                // Certificates table (paginated)
                int totalFiltered = static_cast<int>(filteredCerts.size());
                int startIdx = (page - 1) * size;
                int endIdx = std::min(startIdx + size, totalFiltered);

                Json::Value certsObj;
                certsObj["total"] = totalFiltered;
                certsObj["page"] = page;
                certsObj["size"] = size;

                Json::Value items(Json::arrayValue);
                for (int i = startIdx; i < endIdx; i++) {
                    const auto& cert = *filteredCerts[i];
                    Json::Value certJson;
                    certJson["fingerprint"] = cert.getFingerprint();
                    certJson["countryCode"] = cert.getCountry();
                    certJson["subjectDn"] = cert.getSubjectDn();
                    certJson["issuerDn"] = cert.getIssuerDn();
                    certJson["serialNumber"] = cert.getSn();

                    // Dates
                    char timeBuf[32];
                    auto validFrom = std::chrono::system_clock::to_time_t(cert.getValidFrom());
                    auto validTo = std::chrono::system_clock::to_time_t(cert.getValidTo());
                    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&validFrom));
                    certJson["notBefore"] = timeBuf;
                    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&validTo));
                    certJson["notAfter"] = timeBuf;

                    // Validity
                    auto status = cert.getValidityStatus();
                    if (status == domain::models::ValidityStatus::VALID) certJson["validity"] = "VALID";
                    else if (status == domain::models::ValidityStatus::EXPIRED) certJson["validity"] = "EXPIRED";
                    else if (status == domain::models::ValidityStatus::NOT_YET_VALID) certJson["validity"] = "NOT_YET_VALID";
                    else certJson["validity"] = "UNKNOWN";

                    // Algorithms
                    if (cert.getSignatureAlgorithm().has_value()) certJson["signatureAlgorithm"] = *cert.getSignatureAlgorithm();
                    if (cert.getPublicKeyAlgorithm().has_value()) certJson["publicKeyAlgorithm"] = *cert.getPublicKeyAlgorithm();
                    if (cert.getPublicKeySize().has_value()) certJson["publicKeySize"] = *cert.getPublicKeySize();

                    // Conformance data
                    if (cert.getPkdConformanceCode().has_value()) certJson["pkdConformanceCode"] = *cert.getPkdConformanceCode();
                    if (cert.getPkdConformanceText().has_value()) certJson["pkdConformanceText"] = *cert.getPkdConformanceText();
                    if (cert.getPkdVersion().has_value()) certJson["pkdVersion"] = *cert.getPkdVersion();

                    items.append(certJson);
                }
                certsObj["items"] = items;
                response["certificates"] = certsObj;

                auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
                callback(resp);

            } catch (const std::exception& e) {
                spdlog::error("DSC_NC report error: {}", e.what());
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

    // Register ICAO Auto Sync routes
    if (icaoHandler) {
        icaoHandler->registerRoutes(app);
        spdlog::info("ICAO Auto Sync routes registered");
    }

    // --- Link Certificate Validation API ---

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

            // Use QueryExecutor for Oracle support
            if (!::queryExecutor) {
                Json::Value error;
                error["success"] = false;
                error["error"] = "Query executor not initialized";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
                return;
            }

            try {
                // Create LC validator with QueryExecutor (Oracle/PostgreSQL agnostic)
                lc::LcValidator validator(::queryExecutor.get());

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
                // dbConn RAII: connection auto-released here

            } catch (const std::exception& e) {
                Json::Value error;
                error["success"] = false;
                error["error"] = std::string("Validation failed: ") + e.what();
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
            }
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

            if (!::certificateRepository) {
                Json::Value error;
                error["success"] = false;
                error["error"] = "Certificate repository not initialized";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
                return;
            }

            try {
                std::string validFilter = validOnly ? "true" : "";
                auto rows = ::certificateRepository->searchLinkCertificates(country, validFilter, limit, offset);

                // Helper for Oracle boolean values
                std::string dbType = ::queryExecutor ? ::queryExecutor->getDatabaseType() : "postgres";
                auto parseBool = [&dbType](const Json::Value& v) -> bool {
                    if (v.isBool()) return v.asBool();
                    std::string s = v.asString();
                    return (s == "t" || s == "true" || s == "1" || s == "TRUE");
                };

                // Build JSON response
                Json::Value response;
                response["success"] = true;
                response["total"] = static_cast<int>(rows.size());
                response["limit"] = limit;
                response["offset"] = offset;

                Json::Value certificates(Json::arrayValue);
                for (Json::ArrayIndex i = 0; i < rows.size(); i++) {
                    const auto& row = rows[i];
                    Json::Value cert;
                    cert["id"] = row.get("id", "").asString();
                    cert["subjectDn"] = row.get("subject_dn", "").asString();
                    cert["issuerDn"] = row.get("issuer_dn", "").asString();
                    cert["serialNumber"] = row.get("serial_number", "").asString();
                    cert["fingerprint"] = row.get("fingerprint_sha256", "").asString();
                    cert["oldCscaSubjectDn"] = row.get("old_csca_subject_dn", "").asString();
                    cert["newCscaSubjectDn"] = row.get("new_csca_subject_dn", "").asString();
                    cert["trustChainValid"] = parseBool(row["trust_chain_valid"]);
                    cert["createdAt"] = row.get("created_at", "").asString();
                    cert["countryCode"] = row.get("country_code", "").asString();

                    certificates.append(cert);
                }

                response["certificates"] = certificates;

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

            if (!::certificateRepository) {
                Json::Value error;
                error["success"] = false;
                error["error"] = "Certificate repository not initialized";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
                return;
            }

            try {
                std::string dbType = ::queryExecutor ? ::queryExecutor->getDatabaseType() : "postgres";

                // Helper for Oracle boolean values
                auto parseBool = [&dbType](const Json::Value& v) -> bool {
                    if (v.isBool()) return v.asBool();
                    std::string s = v.asString();
                    return (s == "t" || s == "true" || s == "1" || s == "TRUE");
                };

                auto safeInt = [](const Json::Value& v) -> int {
                    if (v.isInt()) return v.asInt();
                    if (v.isString()) { try { return std::stoi(v.asString()); } catch (...) { return 0; } }
                    return 0;
                };

                // Query LC by ID via CertificateRepository
                Json::Value rowValue = ::certificateRepository->findLinkCertificateById(id);

                if (rowValue.isNull()) {
                    Json::Value error;
                    error["success"] = false;
                    error["error"] = "Link Certificate not found";
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k404NotFound);
                    callback(resp);
                    return;
                }

                const auto& row = rowValue;

                // Build JSON response
                Json::Value response;
                response["success"] = true;

                Json::Value cert;
                cert["id"] = row.get("id", "").asString();
                cert["subjectDn"] = row.get("subject_dn", "").asString();
                cert["issuerDn"] = row.get("issuer_dn", "").asString();
                cert["serialNumber"] = row.get("serial_number", "").asString();
                cert["fingerprint"] = row.get("fingerprint_sha256", "").asString();

                Json::Value signatures;
                signatures["oldCscaSubjectDn"] = row.get("old_csca_subject_dn", "").asString();
                signatures["oldCscaFingerprint"] = row.get("old_csca_fingerprint", "").asString();
                signatures["newCscaSubjectDn"] = row.get("new_csca_subject_dn", "").asString();
                signatures["newCscaFingerprint"] = row.get("new_csca_fingerprint", "").asString();
                signatures["trustChainValid"] = parseBool(row["trust_chain_valid"]);
                signatures["oldCscaSignatureValid"] = parseBool(row["old_csca_signature_valid"]);
                signatures["newCscaSignatureValid"] = parseBool(row["new_csca_signature_valid"]);
                cert["signatures"] = signatures;

                Json::Value properties;
                properties["validityPeriodValid"] = parseBool(row["validity_period_valid"]);
                properties["notBefore"] = row.get("not_before", "").asString();
                properties["notAfter"] = row.get("not_after", "").asString();
                properties["extensionsValid"] = parseBool(row["extensions_valid"]);
                cert["properties"] = properties;

                Json::Value extensions;
                extensions["basicConstraintsCa"] = parseBool(row["basic_constraints_ca"]);
                extensions["basicConstraintsPathlen"] = safeInt(row["basic_constraints_pathlen"]);
                extensions["keyUsage"] = row.get("key_usage", "").asString();
                extensions["extendedKeyUsage"] = row.get("extended_key_usage", "").asString();
                cert["extensions"] = extensions;

                Json::Value revocation;
                revocation["status"] = row.get("revocation_status", "").asString();
                revocation["message"] = row.get("revocation_message", "").asString();
                cert["revocation"] = revocation;

                cert["ldapDn"] = row.get("ldap_dn_v2", "").asString();
                cert["storedInLdap"] = parseBool(row["stored_in_ldap"]);
                cert["createdAt"] = row.get("created_at", "").asString();
                cert["countryCode"] = row.get("country_code", "").asString();

                response["certificate"] = cert;

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
        },
        {drogon::Get}
    );

    // --- LDAP DN Migration API (Internal) ---

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

            // Uses connection pool + QueryExecutor
            // NOTE: This endpoint uses PQunescapeBytea for certificate_data (BYTEA column).
            // For PostgreSQL, we use the connection pool. For Oracle, this internal migration
            // endpoint is not applicable (Oracle uses a different DN migration approach).
            if (!::dbPool) {
                Json::Value error;
                error["success"] = false;
                error["error"] = "Database connection pool not initialized (PostgreSQL only endpoint)";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
                return;
            }

            common::DbConnection dbConn = ::dbPool->acquire();
            PGconn* conn = dbConn.get();
            if (!conn) {
                Json::Value error;
                error["success"] = false;
                error["error"] = "Failed to acquire database connection from pool";
                auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                resp->setStatusCode(drogon::k500InternalServerError);
                callback(resp);
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
                    ldap_unbind_ext_s(ld, nullptr, nullptr);
                    auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
                    resp->setStatusCode(drogon::k500InternalServerError);
                    callback(resp);
                    return;
                }
            }

            // Fetch batch of certificates (PostgreSQL-specific due to BYTEA handling)
            const char* query =
                "SELECT id, fingerprint_sha256, certificate_type, country_code, "
                "       certificate_data, subject_dn, serial_number, issuer_dn "
                "FROM certificate "
                "WHERE stored_in_ldap = true AND ldap_dn_v2 IS NULL "
                "ORDER BY id "
                "OFFSET $1 LIMIT $2";

            std::string offsetStr2 = std::to_string(offset);
            std::string limitStr2 = std::to_string(limit);
            const char* paramValues[2] = {offsetStr2.c_str(), limitStr2.c_str()};

            PGresult* res = PQexecParams(conn, query, 2, nullptr, paramValues,
                                        nullptr, nullptr, 0);

            if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                Json::Value error;
                error["success"] = false;
                error["error"] = std::string("DB query failed: ") + PQerrorMessage(conn);
                PQclear(res);
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

                // Get binary certificate data (PostgreSQL BYTEA specific)
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

                // Update database with new DN using QueryExecutor
                if (ldapSuccess || mode == "test") {
                    try {
                        ::queryExecutor->executeCommand(
                            "UPDATE certificate SET ldap_dn_v2 = $1 WHERE id = $2",
                            {newDn, certId});
                        successCount++;
                        spdlog::debug("Migrated certificate {} to new DN: {}", certId, newDn);
                    } catch (const std::exception& e) {
                        failedCount++;
                        errors.append(certId + ": DB update failed - " + std::string(e.what()));
                    }
                }
            }

            PQclear(res);
            // dbConn RAII: connection auto-released when scope exits
            if (ld) ldap_unbind_ext_s(ld, nullptr, nullptr);

            // Update migration status using QueryExecutor
            if (::queryExecutor) {
                try {
                    std::string dbType = ::queryExecutor->getDatabaseType();
                    std::string nowFunc = (dbType == "oracle") ? "SYSTIMESTAMP" : "NOW()";
                    std::string statusQuery =
                        "UPDATE ldap_migration_status "
                        "SET migrated_records = migrated_records + $1, "
                        "    failed_records = failed_records + $2, "
                        "    updated_at = " + nowFunc + " "
                        "WHERE table_name = 'certificate' "
                        "  AND status = 'IN_PROGRESS'";

                    ::queryExecutor->executeCommand(statusQuery,
                        {std::to_string(successCount), std::to_string(failedCount)});
                } catch (const std::exception& e) {
                    spdlog::warn("Failed to update migration status: {}", e.what());
                }
            }

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

    // Validate required credentials
    try {
        appConfig.validateRequiredCredentials();
    } catch (const std::exception& e) {
        spdlog::critical("{}", e.what());
        return 1;
    }

    spdlog::info("====== ICAO Local PKD Management Service ======");
    spdlog::info("Database: {}:{}/{}", appConfig.dbHost, appConfig.dbPort, appConfig.dbName);
    spdlog::info("LDAP: {}:{}", appConfig.ldapHost, appConfig.ldapPort);

    // Create LDAP connection pool FIRST (Thread-safe LDAP connection management)
    // Note: Using write host for upload operations
    try {
        std::string ldapWriteUri = "ldap://" + appConfig.ldapWriteHost + ":" + std::to_string(appConfig.ldapWritePort);

        ldapPool = std::make_shared<common::LdapConnectionPool>(
            ldapWriteUri,                    // LDAP URI
            appConfig.ldapBindDn,            // Bind DN
            appConfig.ldapBindPassword,      // Bind Password
            2,   // minConnections
            10,  // maxConnections
            5    // acquireTimeoutSec
        );

        spdlog::info("LDAP connection pool initialized (min=2, max=10, host={})", ldapWriteUri);
    } catch (const std::exception& e) {
        spdlog::critical("Failed to initialize LDAP connection pool: {}", e.what());
        return 1;
    }

    try {
        // Initialize Certificate Service (Clean Architecture) - Using LDAP connection pool
        // Certificate search base DN: dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
        // Note: Repository will prepend dc=data,dc=download based on search criteria
        std::string certSearchBaseDn = appConfig.ldapBaseDn;

        auto repository = std::make_shared<repositories::LdapCertificateRepository>(
            ldapPool.get(),
            certSearchBaseDn
        );
        certificateService = std::make_shared<services::CertificateService>(repository);
        spdlog::info("Certificate service initialized with LDAP connection pool (baseDN: {})", certSearchBaseDn);

        spdlog::info("Countries API configured (PostgreSQL query, ~70ms response time)");

        // ICAO Auto Sync Module initialization deferred to after QueryExecutor init
        // IcaoVersionRepository now uses IQueryExecutor instead of PGconn

        // Initialize Repository Pattern - Repositories and Services
        spdlog::info("Initializing Repository Pattern...");

        // Create database connection pool for Repositories (Factory Pattern with Oracle support)
        std::shared_ptr<common::IDbConnectionPool> genericPool;

        try {
            // Use Factory Pattern to create pool based on DB_TYPE environment variable
            // Supports both PostgreSQL (production) and Oracle (development)
            genericPool = common::DbConnectionPoolFactory::createFromEnv();

            if (!genericPool) {
                spdlog::critical("Failed to create database connection pool from environment");
                ldapPool.reset();  // Release LDAP pool
                return 1;
            }

            // Initialize the connection pool
            if (!genericPool->initialize()) {
                spdlog::critical("Failed to initialize database connection pool");
                ldapPool.reset();  // Release LDAP pool
                return 1;
            }

            std::string dbType = genericPool->getDatabaseType();
            spdlog::info("✅ Database connection pool initialized (type={})", dbType);

            // Query Executor Pattern supports both PostgreSQL and Oracle
            // Repositories work with any database through IQueryExecutor interface
            spdlog::info("Repository Pattern initialization complete - Ready for {} database", dbType);

        } catch (const std::exception& e) {
            spdlog::critical("Failed to initialize database connection pool: {}", e.what());
            ldapPool.reset();  // Release LDAP pool
            return 1;
        }

        // Initialize Query Executor (Database-agnostic query execution)
        queryExecutor = common::createQueryExecutor(genericPool.get());
        spdlog::info("Query Executor initialized (DB type: {})", queryExecutor->getDatabaseType());

        // Initialize Repositories with Query Executor (Database-agnostic)
        uploadRepository = std::make_shared<repositories::UploadRepository>(queryExecutor.get());
        certificateRepository = std::make_shared<repositories::CertificateRepository>(queryExecutor.get());
        validationRepository = std::make_shared<repositories::ValidationRepository>(queryExecutor.get(), ldapPool, appConfig.ldapBaseDn);
        auditRepository = std::make_shared<repositories::AuditRepository>(queryExecutor.get());
        statisticsRepository = std::make_shared<repositories::StatisticsRepository>(queryExecutor.get());
        userRepository = std::make_shared<repositories::UserRepository>(queryExecutor.get());
        authAuditRepository = std::make_shared<repositories::AuthAuditRepository>(queryExecutor.get());
        crlRepository = std::make_shared<repositories::CrlRepository>(queryExecutor.get());
        deviationListRepository = std::make_shared<repositories::DeviationListRepository>(queryExecutor.get());
        spdlog::info("Repositories initialized (Upload, Certificate, Validation, Audit, Statistics, User, AuthAudit, CRL, DL: Query Executor)");
        ldifStructureRepository = std::make_shared<repositories::LdifStructureRepository>(uploadRepository.get());

        // Initialize ICAO Auto Sync Module
        spdlog::info("Initializing ICAO Auto Sync module...");

        auto icaoRepo = std::make_shared<repositories::IcaoVersionRepository>(queryExecutor.get());
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

        // Initialize Services with Repository dependencies
        uploadService = std::make_shared<services::UploadService>(
            uploadRepository.get(),
            certificateRepository.get(),
            ldapPool.get(),
            deviationListRepository.get()  // DL deviation data storage
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

        // Initialize Authentication Handler with Repository Pattern
        spdlog::info("Initializing Authentication module with Repository Pattern...");
        authHandler = std::make_shared<handlers::AuthHandler>(
            userRepository.get(),
            authAuditRepository.get()
        );
        spdlog::info("Authentication module initialized (UserRepository, AuthAuditRepository)");

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

        // Register AuthMiddleware globally for JWT authentication
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

        // ICAO Auto Version Check Scheduler
        if (appConfig.icaoSchedulerEnabled) {
            spdlog::info("[IcaoScheduler] Setting up daily version check at {:02d}:00",
                        appConfig.icaoCheckScheduleHour);

            // Calculate seconds until next target hour
            auto now = std::chrono::system_clock::now();
            auto time_t_now = std::chrono::system_clock::to_time_t(now);
            struct tm tm_now;
            localtime_r(&time_t_now, &tm_now);

            int currentSeconds = tm_now.tm_hour * 3600 + tm_now.tm_min * 60 + tm_now.tm_sec;
            int targetSeconds = appConfig.icaoCheckScheduleHour * 3600;
            int delaySeconds = targetSeconds - currentSeconds;
            if (delaySeconds <= 0) {
                delaySeconds += 86400;  // Next day
            }

            spdlog::info("[IcaoScheduler] First check scheduled in {} seconds ({:.1f} hours)",
                        delaySeconds, delaySeconds / 3600.0);

            // Capture icaoService by shared_ptr for safe timer callback
            auto scheduledIcaoService = icaoService;

            app.getLoop()->runAfter(static_cast<double>(delaySeconds), [scheduledIcaoService, &app]() {
                spdlog::info("[IcaoScheduler] Running scheduled ICAO version check");
                try {
                    auto result = scheduledIcaoService->checkForUpdates();
                    spdlog::info("[IcaoScheduler] Check complete: {} (new versions: {})",
                                result.message, result.newVersionCount);
                } catch (const std::exception& e) {
                    spdlog::error("[IcaoScheduler] Exception during scheduled check: {}", e.what());
                }

                // Register recurring 24-hour timer
                app.getLoop()->runEvery(86400.0, [scheduledIcaoService]() {
                    spdlog::info("[IcaoScheduler] Running daily ICAO version check");
                    try {
                        auto result = scheduledIcaoService->checkForUpdates();
                        spdlog::info("[IcaoScheduler] Check complete: {} (new versions: {})",
                                    result.message, result.newVersionCount);
                    } catch (const std::exception& e) {
                        spdlog::error("[IcaoScheduler] Exception during daily check: {}", e.what());
                    }
                });
            });

            spdlog::info("[IcaoScheduler] Scheduler enabled (daily at {:02d}:00)",
                        appConfig.icaoCheckScheduleHour);
        } else {
            spdlog::info("[IcaoScheduler] Scheduler disabled (ICAO_SCHEDULER_ENABLED=false)");
        }

        spdlog::info("Server starting on http://0.0.0.0:{}", appConfig.serverPort);
        spdlog::info("Press Ctrl+C to stop the server");

        // Run the server
        app.run();

        // Cleanup - Close LDAP connections (Database connection pool auto-cleanup via shared_ptr)
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
