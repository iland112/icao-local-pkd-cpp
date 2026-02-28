#pragma once

/**
 * @file code_master_handler.h
 * @brief HTTP handler for Code Master API endpoints
 *
 * Provides CRUD operations for the code_master table.
 * GET endpoints are public; POST/PUT/DELETE require JWT.
 */

#include <drogon/drogon.h>
#include "../repositories/code_master_repository.h"
#include "i_query_executor.h"

namespace handlers {

class CodeMasterHandler {
public:
    CodeMasterHandler(repositories::CodeMasterRepository* repository,
                      common::IQueryExecutor* queryExecutor);
    ~CodeMasterHandler();

    void registerRoutes(drogon::HttpAppFramework& app);

private:
    repositories::CodeMasterRepository* repository_;
    common::IQueryExecutor* queryExecutor_;

    /** GET /api/code-master — List codes (category filter, pagination) */
    void handleGetAll(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /** GET /api/code-master/categories — Get all distinct categories */
    void handleGetCategories(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /** GET /api/code-master/{id} — Get single code by ID */
    void handleGetById(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& id);

    /** POST /api/code-master — Create new code (JWT required) */
    void handleCreate(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /** PUT /api/code-master/{id} — Update code (JWT required) */
    void handleUpdate(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& id);

    /** DELETE /api/code-master/{id} — Deactivate code (JWT required) */
    void handleDelete(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& id);

    /** Helper: convert CodeMaster to JSON */
    Json::Value modelToJson(const domain::models::CodeMaster& item);
};

} // namespace handlers
