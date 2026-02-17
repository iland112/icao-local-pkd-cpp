#pragma once

/**
 * @file upload_handler.h
 * @brief Upload endpoints handler
 *
 * Provides upload-related API endpoints:
 * - POST /api/upload/{uploadId}/parse       - Trigger parsing
 * - POST /api/upload/{uploadId}/validate    - Trigger validation and DB save
 * - GET  /api/upload/{uploadId}/validations - Get validation results
 * - GET  /api/upload/{uploadId}/validation-statistics - Get validation stats
 * - GET  /api/upload/{uploadId}/ldif-structure       - Get LDIF structure
 * - DELETE /api/upload/{uploadId}           - Delete upload
 * - POST /api/upload/ldif                   - Upload LDIF file
 * - POST /api/upload/masterlist             - Upload Master List file
 * - POST /api/upload/certificate            - Upload individual certificate
 * - POST /api/upload/certificate/preview    - Preview certificate (parse only)
 *
 * Uses Repository Pattern for database-agnostic operation.
 *
 * @date 2026-02-17
 */

#include <drogon/drogon.h>
#include <string>
#include <vector>
#include <mutex>
#include <set>

// Forward declaration for OpenLDAP type
typedef struct ldap LDAP;

// Forward declarations - repositories
namespace repositories {
    class UploadRepository;
    class CertificateRepository;
    class CrlRepository;
    class ValidationRepository;
}

// Forward declarations - services
namespace services {
    class UploadService;
    class ValidationService;
    class LdifStructureService;
}

// Forward declaration - query executor
namespace common {
    class IQueryExecutor;
}

namespace handlers {

/**
 * @brief Upload endpoints handler
 *
 * Provides all upload-related API endpoints extracted from main.cpp.
 * Manages LDIF, Master List, and individual certificate upload workflows.
 */
class UploadHandler {
public:
    /**
     * @brief LDAP configuration for write operations
     */
    struct LdapConfig {
        std::string writeHost;
        int writePort = 389;
        std::string bindDn;
        std::string bindPassword;
        std::string baseDn;
    };

    /**
     * @brief Construct UploadHandler
     *
     * Initializes all dependencies for upload operations.
     *
     * @param uploadService Upload service (non-owning pointer)
     * @param validationService Validation service (non-owning pointer)
     * @param ldifStructureService LDIF structure service (non-owning pointer)
     * @param uploadRepository Upload repository (non-owning pointer)
     * @param certificateRepository Certificate repository (non-owning pointer)
     * @param crlRepository CRL repository (non-owning pointer)
     * @param validationRepository Validation repository (non-owning pointer)
     * @param queryExecutor Query executor for audit logging (non-owning pointer)
     * @param ldapConfig LDAP configuration for write connections
     */
    UploadHandler(
        services::UploadService* uploadService,
        services::ValidationService* validationService,
        services::LdifStructureService* ldifStructureService,
        repositories::UploadRepository* uploadRepository,
        repositories::CertificateRepository* certificateRepository,
        repositories::CrlRepository* crlRepository,
        repositories::ValidationRepository* validationRepository,
        common::IQueryExecutor* queryExecutor,
        const LdapConfig& ldapConfig);

    /**
     * @brief Register upload routes
     *
     * Registers all upload endpoints with Drogon application.
     *
     * @param app Drogon application instance
     */
    void registerRoutes(drogon::HttpAppFramework& app);

private:
    // --- Dependencies (non-owning pointers) ---
    services::UploadService* uploadService_;
    services::ValidationService* validationService_;
    services::LdifStructureService* ldifStructureService_;
    repositories::UploadRepository* uploadRepository_;
    repositories::CertificateRepository* certificateRepository_;
    repositories::CrlRepository* crlRepository_;
    repositories::ValidationRepository* validationRepository_;
    common::IQueryExecutor* queryExecutor_;
    LdapConfig ldapConfig_;

    // --- Static processing state (shared across instances) ---
    static std::mutex s_processingMutex;
    static std::set<std::string> s_processingUploads;

    // --- Handler methods ---

    /**
     * @brief POST /api/upload/{uploadId}/parse
     *
     * Trigger parsing of previously uploaded file.
     * Reads file from disk and dispatches async processing.
     */
    void handleParse(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& uploadId);

    /**
     * @brief POST /api/upload/{uploadId}/validate
     *
     * Trigger validation and DB save (MANUAL mode Stage 2).
     */
    void handleValidate(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& uploadId);

    /**
     * @brief GET /api/upload/{uploadId}/validations
     *
     * Get validation results for an upload with pagination and filtering.
     * Query params: limit, offset, status, certType
     */
    void handleGetValidations(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& uploadId);

    /**
     * @brief GET /api/upload/{uploadId}/validation-statistics
     *
     * Get validation statistics summary for an upload.
     */
    void handleGetValidationStatistics(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& uploadId);

    /**
     * @brief GET /api/upload/{uploadId}/ldif-structure
     *
     * Get LDIF/ASN.1 file structure for visualization.
     * Query params: maxEntries (default: 100)
     */
    void handleGetLdifStructure(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& uploadId);

    /**
     * @brief DELETE /api/upload/{uploadId}
     *
     * Delete upload and associated data.
     * Includes audit logging for both success and failure.
     */
    void handleDelete(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& uploadId);

    /**
     * @brief POST /api/upload/ldif
     *
     * Upload LDIF file with multipart form data.
     * Supports AUTO and MANUAL processing modes.
     * Includes file validation, duplicate detection, and audit logging.
     */
    void handleUploadLdif(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief POST /api/upload/masterlist
     *
     * Upload Master List (CMS/PKCS7) file with multipart form data.
     * Supports AUTO and MANUAL processing modes.
     * Includes format validation, duplicate detection, and audit logging.
     */
    void handleUploadMasterList(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief POST /api/upload/certificate
     *
     * Upload individual certificate file (PEM, DER, CER, P7B, DL, CRL).
     * Saves to DB + LDAP immediately.
     */
    void handleUploadCertificate(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief POST /api/upload/certificate/preview
     *
     * Preview certificate file (parse only, no DB/LDAP save).
     * Returns parsed certificate metadata and structure.
     */
    void handlePreviewCertificate(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    // --- Helper methods ---

    /**
     * @brief Get LDAP connection for write operations
     * @return LDAP connection pointer or nullptr on failure
     */
    LDAP* getLdapWriteConnection();

    /**
     * @brief Process LDIF file asynchronously with full parsing (DB + LDAP)
     *
     * Spawns a detached thread for async processing.
     * Guards against duplicate processing via s_processingMutex/s_processingUploads.
     *
     * @param uploadId Upload record UUID
     * @param content Raw LDIF file content
     */
    void processLdifFileAsync(const std::string& uploadId, const std::vector<uint8_t>& content);

    /**
     * @brief Process Master List file asynchronously (CMS parsing + DB + LDAP)
     *
     * Spawns a detached thread for async processing.
     * Guards against duplicate processing via s_processingMutex/s_processingUploads.
     *
     * @param uploadId Upload record UUID
     * @param content Raw Master List file content
     */
    void processMasterListFileAsync(const std::string& uploadId, const std::vector<uint8_t>& content);
};

} // namespace handlers
