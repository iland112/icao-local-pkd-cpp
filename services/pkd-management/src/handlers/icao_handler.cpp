#include "icao_handler.h"
#include <spdlog/spdlog.h>
#include <json/json.h>

using namespace drogon;

namespace handlers {

IcaoHandler::IcaoHandler(std::shared_ptr<services::IcaoSyncService> service)
    : service_(service) {
    spdlog::info("[IcaoHandler] Initialized");
}

IcaoHandler::~IcaoHandler() {}

void IcaoHandler::registerRoutes(HttpAppFramework& app) {
    spdlog::info("[IcaoHandler] Registering ICAO API routes");

    // GET /api/icao/check-updates
    app.registerHandler(
        "/api/icao/check-updates",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback) {
            this->handleCheckUpdates(req, std::move(callback));
        },
        {Get});

    // GET /api/icao/latest
    app.registerHandler(
        "/api/icao/latest",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback) {
            this->handleGetLatest(req, std::move(callback));
        },
        {Get});

    // GET /api/icao/history?limit=N
    app.registerHandler(
        "/api/icao/history",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback) {
            // Parse query parameter
            int limit = 10;  // Default
            auto limitParam = req->getParameter("limit");
            if (!limitParam.empty()) {
                try {
                    limit = std::stoi(limitParam);
                    if (limit <= 0) limit = 10;
                    if (limit > 100) limit = 100;
                } catch (...) {
                    limit = 10;
                }
            }
            this->handleGetHistory(req, std::move(callback), limit);
        },
        {Get});

    // GET /api/icao/status
    app.registerHandler(
        "/api/icao/status",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback) {
            this->handleGetStatus(req, std::move(callback));
        },
        {Get});

    spdlog::info("[IcaoHandler] Routes registered: /api/icao/check-updates, "
                "/api/icao/latest, /api/icao/history, /api/icao/status");
}

void IcaoHandler::handleCheckUpdates(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {

    spdlog::info("[IcaoHandler] GET /api/icao/check-updates");

    try {
        auto result = service_->checkForUpdates();

        Json::Value response;
        response["success"] = result.success;
        response["message"] = result.message;
        response["new_version_count"] = result.newVersionCount;

        // Add new versions array
        Json::Value versionsArray(Json::arrayValue);
        for (const auto& version : result.newVersions) {
            versionsArray.append(versionToJson(version));
        }
        response["new_versions"] = versionsArray;

        auto resp = HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("[IcaoHandler] Exception: {}", e.what());

        Json::Value error;
        error["success"] = false;
        error["message"] = std::string("Error: ") + e.what();

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
}

void IcaoHandler::handleGetLatest(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {

    spdlog::info("[IcaoHandler] GET /api/icao/latest");

    try {
        auto versions = service_->getLatestVersions();

        Json::Value response;
        response["success"] = true;
        response["count"] = static_cast<int>(versions.size());

        Json::Value versionsArray(Json::arrayValue);
        for (const auto& version : versions) {
            versionsArray.append(versionToJson(version));
        }
        response["versions"] = versionsArray;

        auto resp = HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("[IcaoHandler] Exception: {}", e.what());

        Json::Value error;
        error["success"] = false;
        error["message"] = std::string("Error: ") + e.what();

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
}

void IcaoHandler::handleGetHistory(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    int limit) {

    spdlog::info("[IcaoHandler] GET /api/icao/history?limit={}", limit);

    try {
        auto versions = service_->getVersionHistory(limit);

        Json::Value response;
        response["success"] = true;
        response["limit"] = limit;
        response["count"] = static_cast<int>(versions.size());

        Json::Value versionsArray(Json::arrayValue);
        for (const auto& version : versions) {
            versionsArray.append(versionToJson(version));
        }
        response["versions"] = versionsArray;

        auto resp = HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("[IcaoHandler] Exception: {}", e.what());

        Json::Value error;
        error["success"] = false;
        error["message"] = std::string("Error: ") + e.what();

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
}

void IcaoHandler::handleGetStatus(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {

    spdlog::info("[IcaoHandler] GET /api/icao/status");

    try {
        auto comparisons = service_->getVersionComparison();

        Json::Value response;
        response["success"] = true;
        response["count"] = static_cast<int>(comparisons.size());

        bool anyNeedsUpdate = false;

        Json::Value statusArray(Json::arrayValue);
        for (const auto& comp : comparisons) {
            Json::Value item;
            item["collection_type"] = std::get<0>(comp);
            item["detected_version"] = std::get<1>(comp);
            item["uploaded_version"] = std::get<2>(comp);
            item["upload_timestamp"] = std::get<3>(comp);

            // Calculate version difference
            int detectedVersion = std::get<1>(comp);
            int uploadedVersion = std::get<2>(comp);
            item["version_diff"] = detectedVersion - uploadedVersion;
            bool needsUpdate = (detectedVersion > uploadedVersion);
            item["needs_update"] = needsUpdate;
            if (needsUpdate) anyNeedsUpdate = true;

            // Status message
            if (uploadedVersion == 0) {
                item["status"] = "NOT_UPLOADED";
                item["status_message"] = "No upload found for this collection";
            } else if (detectedVersion > uploadedVersion) {
                item["status"] = "UPDATE_NEEDED";
                item["status_message"] = "New version available (+" +
                    std::to_string(detectedVersion - uploadedVersion) + " versions behind)";
            } else {
                item["status"] = "UP_TO_DATE";
                item["status_message"] = "System is up to date";
            }

            statusArray.append(item);
        }
        response["status"] = statusArray;
        response["any_needs_update"] = anyNeedsUpdate;

        // Last checked timestamp
        auto lastChecked = service_->getLastCheckedAt();
        response["last_checked_at"] = lastChecked.empty() ? Json::Value::null : Json::Value(lastChecked);

        auto resp = HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("[IcaoHandler] Exception: {}", e.what());

        Json::Value error;
        error["success"] = false;
        error["message"] = std::string("Error: ") + e.what();

        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
}

Json::Value IcaoHandler::versionToJson(const domain::models::IcaoVersion& version) {
    Json::Value json;

    json["id"] = version.id;
    json["collection_type"] = version.collectionType;
    json["file_name"] = version.fileName;
    json["file_version"] = version.fileVersion;
    json["detected_at"] = version.detectedAt;

    if (version.downloadedAt.has_value()) {
        json["downloaded_at"] = version.downloadedAt.value();
    } else {
        json["downloaded_at"] = Json::Value::null;
    }

    if (version.importedAt.has_value()) {
        json["imported_at"] = version.importedAt.value();
    } else {
        json["imported_at"] = Json::Value::null;
    }

    json["status"] = version.status;
    json["status_description"] = version.getStatusDescription();
    json["notification_sent"] = version.notificationSent;

    if (version.notificationSentAt.has_value()) {
        json["notification_sent_at"] = version.notificationSentAt.value();
    } else {
        json["notification_sent_at"] = Json::Value::null;
    }

    if (version.importUploadId.has_value()) {
        json["import_upload_id"] = version.importUploadId.value();
    } else {
        json["import_upload_id"] = Json::Value::null;
    }

    if (version.certificateCount.has_value()) {
        json["certificate_count"] = version.certificateCount.value();
    } else {
        json["certificate_count"] = Json::Value::null;
    }

    if (version.errorMessage.has_value()) {
        json["error_message"] = version.errorMessage.value();
    } else {
        json["error_message"] = Json::Value::null;
    }

    return json;
}

} // namespace handlers
