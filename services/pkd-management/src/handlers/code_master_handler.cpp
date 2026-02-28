/** @file code_master_handler.cpp
 *  @brief CodeMasterHandler implementation
 */

#include "code_master_handler.h"
#include <icao/audit/audit_log.h>
#include <spdlog/spdlog.h>

using namespace drogon;

namespace handlers {

CodeMasterHandler::CodeMasterHandler(repositories::CodeMasterRepository* repository,
                                     common::IQueryExecutor* queryExecutor)
    : repository_(repository), queryExecutor_(queryExecutor) {
    spdlog::info("[CodeMasterHandler] Initialized");
}

CodeMasterHandler::~CodeMasterHandler() {}

void CodeMasterHandler::registerRoutes(HttpAppFramework& app) {
    spdlog::info("[CodeMasterHandler] Registering Code Master API routes");

    // GET /api/code-master
    app.registerHandler(
        "/api/code-master",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback) {
            this->handleGetAll(req, std::move(callback));
        },
        {Get});

    // GET /api/code-master/categories
    app.registerHandler(
        "/api/code-master/categories",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback) {
            this->handleGetCategories(req, std::move(callback));
        },
        {Get});

    // GET /api/code-master/{id}
    app.registerHandler(
        "/api/code-master/{id}",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback,
               const std::string& id) {
            this->handleGetById(req, std::move(callback), id);
        },
        {Get});

    // POST /api/code-master
    app.registerHandler(
        "/api/code-master",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback) {
            this->handleCreate(req, std::move(callback));
        },
        {Post});

    // PUT /api/code-master/{id}
    app.registerHandler(
        "/api/code-master/{id}",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback,
               const std::string& id) {
            this->handleUpdate(req, std::move(callback), id);
        },
        {Put});

    // DELETE /api/code-master/{id}
    app.registerHandler(
        "/api/code-master/{id}",
        [this](const HttpRequestPtr& req,
               std::function<void(const HttpResponsePtr&)>&& callback,
               const std::string& id) {
            this->handleDelete(req, std::move(callback), id);
        },
        {Delete});

    spdlog::info("[CodeMasterHandler] Routes registered: GET/POST /api/code-master, "
                "GET/PUT/DELETE /api/code-master/{{id}}, GET /api/code-master/categories");
}

void CodeMasterHandler::handleGetAll(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {

    try {
        auto category = req->getParameter("category");
        auto activeOnlyParam = req->getParameter("activeOnly");
        bool activeOnly = (activeOnlyParam.empty() || activeOnlyParam == "true");

        auto pageParam = req->getParameter("page");
        auto sizeParam = req->getParameter("size");
        int page = 1, size = 200;
        if (!pageParam.empty()) {
            try { page = std::stoi(pageParam); if (page < 1) page = 1; } catch (...) {}
        }
        if (!sizeParam.empty()) {
            try { size = std::stoi(sizeParam); if (size < 1) size = 20; if (size > 1000) size = 1000; } catch (...) {}
        }
        int offset = (page - 1) * size;

        auto items = repository_->findAll(category, activeOnly, size, offset);
        int total = repository_->countAll(category, activeOnly);

        Json::Value response;
        response["success"] = true;
        response["total"] = total;
        response["page"] = page;
        response["size"] = size;

        Json::Value itemsArray(Json::arrayValue);
        for (const auto& item : items) {
            itemsArray.append(modelToJson(item));
        }
        response["items"] = itemsArray;

        auto resp = HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("[CodeMasterHandler] GET /api/code-master failed: {}", e.what());
        Json::Value error;
        error["success"] = false;
        error["message"] = std::string("Error: ") + e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
}

void CodeMasterHandler::handleGetCategories(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {

    try {
        auto categories = repository_->getCategories();

        Json::Value response;
        response["success"] = true;
        response["count"] = static_cast<int>(categories.size());

        Json::Value categoriesArray(Json::arrayValue);
        for (const auto& cat : categories) {
            categoriesArray.append(cat);
        }
        response["categories"] = categoriesArray;

        auto resp = HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("[CodeMasterHandler] GET /api/code-master/categories failed: {}", e.what());
        Json::Value error;
        error["success"] = false;
        error["message"] = std::string("Error: ") + e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
}

void CodeMasterHandler::handleGetById(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {

    try {
        auto item = repository_->findById(id);

        if (!item.has_value()) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "Code not found: " + id;
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k404NotFound);
            callback(resp);
            return;
        }

        Json::Value response;
        response["success"] = true;
        response["item"] = modelToJson(item.value());

        auto resp = HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("[CodeMasterHandler] GET /api/code-master/{{id}} failed: {}", e.what());
        Json::Value error;
        error["success"] = false;
        error["message"] = std::string("Error: ") + e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
}

void CodeMasterHandler::handleCreate(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback) {

    try {
        auto body = req->getJsonObject();
        if (!body) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "Invalid JSON body";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        domain::models::CodeMaster item;
        item.category = (*body).get("category", "").asString();
        item.code = (*body).get("code", "").asString();
        item.nameKo = (*body).get("nameKo", "").asString();

        if (item.category.empty() || item.code.empty() || item.nameKo.empty()) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "category, code, and nameKo are required";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        auto nameEn = (*body).get("nameEn", "").asString();
        if (!nameEn.empty()) item.nameEn = nameEn;
        auto desc = (*body).get("description", "").asString();
        if (!desc.empty()) item.description = desc;
        auto sev = (*body).get("severity", "").asString();
        if (!sev.empty()) item.severity = sev;
        item.sortOrder = (*body).get("sortOrder", 0).asInt();
        item.isActive = (*body).get("isActive", true).asBool();
        auto meta = (*body).get("metadata", "").asString();
        if (!meta.empty()) item.metadata = meta;

        bool result = repository_->insert(item);

        // Audit log
        auto auditEntry = icao::audit::createAuditEntryFromRequest(req, icao::audit::OperationType::CODE_MASTER_CREATE);
        auditEntry.success = result;
        auditEntry.resourceType = "CODE_MASTER";
        Json::Value auditMeta;
        auditMeta["category"] = item.category;
        auditMeta["code"] = item.code;
        auditEntry.metadata = auditMeta;
        icao::audit::logOperation(queryExecutor_, auditEntry);

        Json::Value response;
        response["success"] = result;
        response["message"] = result ? "Created" : "Insert failed (duplicate?)";

        auto resp = HttpResponse::newHttpJsonResponse(response);
        if (!result) resp->setStatusCode(k409Conflict);
        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("[CodeMasterHandler] POST /api/code-master failed: {}", e.what());
        Json::Value error;
        error["success"] = false;
        error["message"] = std::string("Error: ") + e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
}

void CodeMasterHandler::handleUpdate(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {

    try {
        auto body = req->getJsonObject();
        if (!body) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "Invalid JSON body";
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k400BadRequest);
            callback(resp);
            return;
        }

        // First check existence
        auto existing = repository_->findById(id);
        if (!existing.has_value()) {
            Json::Value error;
            error["success"] = false;
            error["message"] = "Code not found: " + id;
            auto resp = HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(k404NotFound);
            callback(resp);
            return;
        }

        // Merge fields
        domain::models::CodeMaster item = existing.value();
        auto nameKo = (*body).get("nameKo", "").asString();
        if (!nameKo.empty()) item.nameKo = nameKo;
        auto nameEn = (*body).get("nameEn", "").asString();
        if (!nameEn.empty()) item.nameEn = nameEn;
        auto desc = (*body).get("description", "").asString();
        if (!desc.empty()) item.description = desc;
        auto sev = (*body).get("severity", "").asString();
        if (!sev.empty()) item.severity = sev;
        if ((*body).isMember("sortOrder")) item.sortOrder = (*body)["sortOrder"].asInt();
        if ((*body).isMember("isActive")) item.isActive = (*body)["isActive"].asBool();
        auto meta = (*body).get("metadata", "").asString();
        if (!meta.empty()) item.metadata = meta;

        bool result = repository_->update(item);

        // Audit log
        auto auditEntry = icao::audit::createAuditEntryFromRequest(req, icao::audit::OperationType::CODE_MASTER_UPDATE);
        auditEntry.success = result;
        auditEntry.resourceId = id;
        auditEntry.resourceType = "CODE_MASTER";
        icao::audit::logOperation(queryExecutor_, auditEntry);

        Json::Value response;
        response["success"] = result;
        response["message"] = result ? "Updated" : "Update failed";

        auto resp = HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("[CodeMasterHandler] PUT /api/code-master/{{id}} failed: {}", e.what());
        Json::Value error;
        error["success"] = false;
        error["message"] = std::string("Error: ") + e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
}

void CodeMasterHandler::handleDelete(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& id) {

    try {
        bool result = repository_->deactivate(id);

        // Audit log
        auto auditEntry = icao::audit::createAuditEntryFromRequest(req, icao::audit::OperationType::CODE_MASTER_DELETE);
        auditEntry.success = result;
        auditEntry.resourceId = id;
        auditEntry.resourceType = "CODE_MASTER";
        icao::audit::logOperation(queryExecutor_, auditEntry);

        Json::Value response;
        response["success"] = result;
        response["message"] = result ? "Deactivated" : "Not found or already inactive";

        auto resp = HttpResponse::newHttpJsonResponse(response);
        if (!result) resp->setStatusCode(k404NotFound);
        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("[CodeMasterHandler] DELETE /api/code-master/{{id}} failed: {}", e.what());
        Json::Value error;
        error["success"] = false;
        error["message"] = std::string("Error: ") + e.what();
        auto resp = HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(k500InternalServerError);
        callback(resp);
    }
}

Json::Value CodeMasterHandler::modelToJson(const domain::models::CodeMaster& item) {
    Json::Value json;

    json["id"] = item.id;
    json["category"] = item.category;
    json["code"] = item.code;
    json["nameKo"] = item.nameKo;
    json["nameEn"] = item.nameEn.has_value() ? Json::Value(item.nameEn.value()) : Json::Value::null;
    json["description"] = item.description.has_value() ? Json::Value(item.description.value()) : Json::Value::null;
    json["severity"] = item.severity.has_value() ? Json::Value(item.severity.value()) : Json::Value::null;
    json["sortOrder"] = item.sortOrder;
    json["isActive"] = item.isActive;

    // Parse metadata JSON string
    if (item.metadata.has_value() && !item.metadata.value().empty()) {
        Json::Value metaJson;
        Json::CharReaderBuilder readerBuilder;
        std::istringstream stream(item.metadata.value());
        std::string errors;
        if (Json::parseFromStream(readerBuilder, stream, &metaJson, &errors)) {
            json["metadata"] = metaJson;
        } else {
            json["metadata"] = item.metadata.value();
        }
    } else {
        json["metadata"] = Json::Value::null;
    }

    json["createdAt"] = item.createdAt;
    json["updatedAt"] = item.updatedAt;

    return json;
}

} // namespace handlers
