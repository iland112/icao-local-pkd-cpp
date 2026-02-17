#pragma once

/**
 * @file upload_stats_handler.h
 * @brief Upload statistics, history, and progress endpoints handler
 *
 * Provides upload statistics and monitoring API endpoints:
 * - GET  /api/upload/statistics                       - Upload statistics overview
 * - GET  /api/upload/statistics/validation-reasons     - Validation reason breakdown
 * - GET  /api/upload/history                          - Upload history (paginated)
 * - GET  /api/upload/detail/{uploadId}                - Upload detail by ID
 * - GET  /api/upload/{uploadId}/issues                - Duplicate certificates detected
 * - GET  /api/upload/{uploadId}/masterlist-structure   - ASN.1 tree structure
 * - GET  /api/upload/changes                          - Recent upload changes (deltas)
 * - GET  /api/upload/countries                        - Country statistics (dashboard)
 * - GET  /api/upload/countries/detailed               - Detailed country breakdown
 * - GET  /api/progress/stream/{uploadId}              - SSE progress stream
 * - GET  /api/progress/status/{uploadId}              - Progress status (polling)
 *
 * Uses Repository Pattern for database-agnostic operation.
 *
 * @date 2026-02-17
 */

#include <drogon/drogon.h>
#include <string>

// Forward declarations - repositories
namespace repositories {
    class UploadRepository;
    class CertificateRepository;
    class ValidationRepository;
}

// Forward declarations - services
namespace services {
    class UploadService;
}

// Forward declaration - query executor
namespace common {
    class IQueryExecutor;
}

namespace handlers {

/**
 * @brief Upload statistics, history, and progress endpoints handler
 *
 * Provides all upload monitoring and statistics API endpoints
 * extracted from main.cpp.
 */
class UploadStatsHandler {
public:
    /**
     * @brief Construct UploadStatsHandler
     *
     * Initializes all dependencies for upload statistics operations.
     *
     * @param uploadService Upload service (non-owning pointer)
     * @param uploadRepository Upload repository (non-owning pointer)
     * @param certificateRepository Certificate repository (non-owning pointer)
     * @param validationRepository Validation repository (non-owning pointer)
     * @param queryExecutor Query executor for direct DB queries (non-owning pointer)
     * @param asn1MaxLines Default max lines for ASN.1 structure parsing
     */
    UploadStatsHandler(
        services::UploadService* uploadService,
        repositories::UploadRepository* uploadRepository,
        repositories::CertificateRepository* certificateRepository,
        repositories::ValidationRepository* validationRepository,
        common::IQueryExecutor* queryExecutor,
        int asn1MaxLines = 100);

    /**
     * @brief Register upload statistics routes
     *
     * Registers all upload statistics endpoints with Drogon application.
     *
     * @param app Drogon application instance
     */
    void registerRoutes(drogon::HttpAppFramework& app);

private:
    // --- Dependencies (non-owning pointers) ---
    services::UploadService* uploadService_;
    repositories::UploadRepository* uploadRepository_;
    repositories::CertificateRepository* certificateRepository_;
    repositories::ValidationRepository* validationRepository_;
    common::IQueryExecutor* queryExecutor_;
    int asn1MaxLines_;

    // --- Handler methods ---

    /**
     * @brief GET /api/upload/statistics
     *
     * Returns UploadStatisticsOverview format with certificate counts
     * and upload summary data.
     */
    void handleGetStatistics(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/upload/statistics/validation-reasons
     *
     * Returns validation reason breakdown (reason string -> count per status).
     */
    void handleGetValidationReasons(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/upload/history
     *
     * Returns paginated upload history with PageResponse format.
     * Query params: page, size, sort, direction
     */
    void handleGetHistory(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/upload/detail/{uploadId}
     *
     * Returns detailed upload information including LDAP status counts.
     */
    void handleGetDetail(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& uploadId);

    /**
     * @brief GET /api/upload/{uploadId}/issues
     *
     * Returns duplicate certificates detected during upload processing.
     */
    void handleGetIssues(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& uploadId);

    /**
     * @brief GET /api/upload/{uploadId}/masterlist-structure
     *
     * Returns ASN.1 tree structure with TLV information for Master List files.
     * Query params: maxLines (default from config)
     */
    void handleGetMasterListStructure(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& uploadId);

    /**
     * @brief GET /api/upload/changes
     *
     * Returns upload change deltas with certificate count differences.
     * Query params: limit (default: 10, max: 100)
     */
    void handleGetChanges(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/upload/countries
     *
     * Returns country-level certificate statistics for dashboard.
     * Query params: limit (default: 20)
     */
    void handleGetCountries(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/upload/countries/detailed
     *
     * Returns detailed country breakdown with all certificate types.
     * Query params: limit (default: 0 = all countries)
     */
    void handleGetCountriesDetailed(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/progress/stream/{uploadId}
     *
     * SSE (Server-Sent Events) progress stream for real-time upload monitoring.
     * Sends progress events as they occur during file processing.
     */
    void handleProgressStream(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& uploadId);

    /**
     * @brief GET /api/progress/status/{uploadId}
     *
     * Returns current progress status for polling-based monitoring.
     */
    void handleProgressStatus(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& uploadId);
};

} // namespace handlers
