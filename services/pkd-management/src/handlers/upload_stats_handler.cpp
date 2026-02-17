/**
 * @file upload_stats_handler.cpp
 * @brief UploadStatsHandler implementation
 *
 * Extracted from main.cpp - upload statistics, history, and progress
 * handler endpoints.
 *
 * @date 2026-02-17
 */

#include "upload_stats_handler.h"

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>
#include <json/json.h>
#include <cstdlib>

// Repositories
#include "../repositories/upload_repository.h"
#include "../repositories/certificate_repository.h"
#include "../repositories/validation_repository.h"

// Services
#include "../services/upload_service.h"

// Common utilities
#include "../common/progress_manager.h"
#include "../common/asn1_parser.h"

// Progress manager types
using common::ProcessingStage;
using common::ProcessingProgress;
using common::ProgressManager;
using common::stageToString;
using common::stageToKorean;

namespace handlers {

// =============================================================================
// Constructor
// =============================================================================

UploadStatsHandler::UploadStatsHandler(
    services::UploadService* uploadService,
    repositories::UploadRepository* uploadRepository,
    repositories::CertificateRepository* certificateRepository,
    repositories::ValidationRepository* validationRepository,
    common::IQueryExecutor* queryExecutor,
    int asn1MaxLines)
    : uploadService_(uploadService),
      uploadRepository_(uploadRepository),
      certificateRepository_(certificateRepository),
      validationRepository_(validationRepository),
      queryExecutor_(queryExecutor),
      asn1MaxLines_(asn1MaxLines)
{
    if (!uploadService_) {
        throw std::invalid_argument("UploadStatsHandler: uploadService cannot be nullptr");
    }
    if (!uploadRepository_) {
        throw std::invalid_argument("UploadStatsHandler: uploadRepository cannot be nullptr");
    }
    if (!certificateRepository_) {
        throw std::invalid_argument("UploadStatsHandler: certificateRepository cannot be nullptr");
    }
    if (!validationRepository_) {
        throw std::invalid_argument("UploadStatsHandler: validationRepository cannot be nullptr");
    }
    if (!queryExecutor_) {
        throw std::invalid_argument("UploadStatsHandler: queryExecutor cannot be nullptr");
    }

    spdlog::info("[UploadStatsHandler] Initialized with Repository Pattern (asn1MaxLines={})", asn1MaxLines_);
}

// =============================================================================
// Route Registration
// =============================================================================

void UploadStatsHandler::registerRoutes(drogon::HttpAppFramework& app) {
    // GET /api/upload/statistics
    app.registerHandler(
        "/api/upload/statistics",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleGetStatistics(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/upload/statistics/validation-reasons
    app.registerHandler(
        "/api/upload/statistics/validation-reasons",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleGetValidationReasons(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/upload/history
    app.registerHandler(
        "/api/upload/history",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleGetHistory(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/upload/detail/{uploadId}
    app.registerHandler(
        "/api/upload/detail/{uploadId}",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& uploadId) {
            handleGetDetail(req, std::move(callback), uploadId);
        },
        {drogon::Get}
    );

    // GET /api/upload/{uploadId}/issues
    app.registerHandler(
        "/api/upload/{uploadId}/issues",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& uploadId) {
            handleGetIssues(req, std::move(callback), uploadId);
        },
        {drogon::Get}
    );

    // GET /api/upload/{uploadId}/masterlist-structure
    app.registerHandler(
        "/api/upload/{uploadId}/masterlist-structure",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& uploadId) {
            handleGetMasterListStructure(req, std::move(callback), uploadId);
        },
        {drogon::Get}
    );

    // GET /api/upload/changes
    app.registerHandler(
        "/api/upload/changes",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleGetChanges(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/upload/countries
    app.registerHandler(
        "/api/upload/countries",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleGetCountries(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/upload/countries/detailed
    app.registerHandler(
        "/api/upload/countries/detailed",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleGetCountriesDetailed(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/progress/stream/{uploadId}
    app.registerHandler(
        "/api/progress/stream/{uploadId}",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& uploadId) {
            handleProgressStream(req, std::move(callback), uploadId);
        },
        {drogon::Get}
    );

    // GET /api/progress/status/{uploadId}
    app.registerHandler(
        "/api/progress/status/{uploadId}",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& uploadId) {
            handleProgressStatus(req, std::move(callback), uploadId);
        },
        {drogon::Get}
    );

    spdlog::info("[UploadStatsHandler] Registered 11 routes (statistics, history, progress)");
}

// =============================================================================
// Handler Implementations
// =============================================================================

// -----------------------------------------------------------------------------
// GET /api/upload/statistics
// -----------------------------------------------------------------------------

void UploadStatsHandler::handleGetStatistics(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("GET /api/upload/statistics");

    try {
        Json::Value result = uploadService_->getUploadStatistics();
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
}

// -----------------------------------------------------------------------------
// GET /api/upload/statistics/validation-reasons
// -----------------------------------------------------------------------------

void UploadStatsHandler::handleGetValidationReasons(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("GET /api/upload/statistics/validation-reasons");

    try {
        Json::Value result = validationRepository_->getReasonBreakdown();
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
}

// -----------------------------------------------------------------------------
// GET /api/upload/history
// -----------------------------------------------------------------------------

void UploadStatsHandler::handleGetHistory(
    const drogon::HttpRequestPtr& req,
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
        Json::Value result = uploadService_->getUploadHistory(filter);

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
}

// -----------------------------------------------------------------------------
// GET /api/upload/detail/{uploadId}
// -----------------------------------------------------------------------------

void UploadStatsHandler::handleGetDetail(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& uploadId) {

    spdlog::info("GET /api/upload/detail/{}", uploadId);

    try {
        // Call Service method (uses Repository)
        Json::Value uploadData = uploadService_->getUploadDetail(uploadId);

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
        if (certificateRepository_) {
            try {
                int totalCerts = 0, ldapCerts = 0;
                certificateRepository_->countLdapStatusByUploadId(uploadId, totalCerts, ldapCerts);
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
}

// -----------------------------------------------------------------------------
// GET /api/upload/{uploadId}/issues
// -----------------------------------------------------------------------------

void UploadStatsHandler::handleGetIssues(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& uploadId) {

    spdlog::info("GET /api/upload/{}/issues", uploadId);

    try {
        Json::Value result = uploadService_->getUploadIssues(uploadId);
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
}

// -----------------------------------------------------------------------------
// GET /api/upload/{uploadId}/masterlist-structure
// -----------------------------------------------------------------------------

void UploadStatsHandler::handleGetMasterListStructure(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& uploadId) {

    spdlog::info("GET /api/upload/{}/masterlist-structure", uploadId);

    Json::Value result;
    result["success"] = false;

    try {
        // Uses QueryExecutor for Oracle support
        if (!queryExecutor_) {
            throw std::runtime_error("Query executor not initialized");
        }

        // Query upload file information (no $1::uuid cast for Oracle compatibility)
        std::string query =
            "SELECT file_name, original_file_name, file_format, file_size, file_path "
            "FROM uploaded_file "
            "WHERE id = $1";

        auto rows = queryExecutor_->executeQuery(query, {uploadId});

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
        int maxLines = asn1MaxLines_;
        if (auto ml = req->getParameter("maxLines"); !ml.empty()) {
            try {
                maxLines = std::stoi(ml);
                if (maxLines < 0) maxLines = asn1MaxLines_;
            } catch (...) {
                maxLines = asn1MaxLines_;
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
}

// -----------------------------------------------------------------------------
// GET /api/upload/changes
// -----------------------------------------------------------------------------

void UploadStatsHandler::handleGetChanges(
    const drogon::HttpRequestPtr& req,
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

    if (!uploadRepository_) {
        result["error"] = "Upload repository not initialized";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
        return;
    }

    try {
        auto rows = uploadRepository_->getChangeHistory(limit);

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
}

// -----------------------------------------------------------------------------
// GET /api/upload/countries
// -----------------------------------------------------------------------------

void UploadStatsHandler::handleGetCountries(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("GET /api/upload/countries");

    try {
        // Get query parameter for limit (default 20)
        int limit = 20;
        if (auto l = req->getParameter("limit"); !l.empty()) {
            limit = std::stoi(l);
        }

        Json::Value result = uploadService_->getCountryStatistics(limit);
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
}

// -----------------------------------------------------------------------------
// GET /api/upload/countries/detailed
// -----------------------------------------------------------------------------

void UploadStatsHandler::handleGetCountriesDetailed(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("GET /api/upload/countries/detailed");

    try {
        // Get query parameters for limit (default ALL countries)
        int limit = 0;  // 0 = no limit
        if (auto l = req->getParameter("limit"); !l.empty()) {
            limit = std::stoi(l);
        }

        Json::Value result = uploadService_->getDetailedCountryStatistics(limit);
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
}

// -----------------------------------------------------------------------------
// GET /api/progress/stream/{uploadId} - SSE Progress Stream
// -----------------------------------------------------------------------------

void UploadStatsHandler::handleProgressStream(
    const drogon::HttpRequestPtr& /* req */,
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
}

// -----------------------------------------------------------------------------
// GET /api/progress/status/{uploadId}
// -----------------------------------------------------------------------------

void UploadStatsHandler::handleProgressStatus(
    const drogon::HttpRequestPtr& req,
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
}

} // namespace handlers
