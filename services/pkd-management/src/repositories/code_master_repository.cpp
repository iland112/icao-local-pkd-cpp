/** @file code_master_repository.cpp
 *  @brief CodeMasterRepository implementation
 */

#include "code_master_repository.h"
#include "query_helpers.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace repositories {

CodeMasterRepository::CodeMasterRepository(common::IQueryExecutor* executor)
    : executor_(executor)
{
    if (!executor_) {
        throw std::invalid_argument("CodeMasterRepository: executor cannot be nullptr");
    }
    spdlog::debug("[CodeMasterRepository] Initialized (DB type: {})", executor_->getDatabaseType());
}

CodeMasterRepository::~CodeMasterRepository() {}

std::vector<domain::models::CodeMaster> CodeMasterRepository::findByCategory(
    const std::string& category, bool activeOnly) {

    try {
        std::string dbType = executor_->getDatabaseType();
        std::string trueVal = common::db::boolLiteral(dbType, true);

        std::string query =
            "SELECT id, category, code, name_ko, name_en, description, "
            "       severity, sort_order, is_active, metadata, created_at, updated_at "
            "FROM code_master "
            "WHERE category = $1";

        if (activeOnly) {
            query += " AND is_active = " + trueVal;
        }

        query += " ORDER BY sort_order, code";

        std::vector<std::string> params = { category };
        Json::Value result = executor_->executeQuery(query, params);

        std::vector<domain::models::CodeMaster> items;
        if (result.isArray()) {
            for (const auto& row : result) {
                items.push_back(jsonToModel(row));
            }
        }

        return items;

    } catch (const std::exception& e) {
        spdlog::error("[CodeMasterRepository] findByCategory failed: {}", e.what());
        return {};
    }
}

std::vector<domain::models::CodeMaster> CodeMasterRepository::findAll(
    const std::string& categoryFilter, bool activeOnly,
    int limit, int offset) {

    try {
        std::string dbType = executor_->getDatabaseType();
        std::string trueVal = common::db::boolLiteral(dbType, true);

        std::string query =
            "SELECT id, category, code, name_ko, name_en, description, "
            "       severity, sort_order, is_active, metadata, created_at, updated_at "
            "FROM code_master WHERE 1=1";

        std::vector<std::string> params;
        int paramIdx = 1;

        if (!categoryFilter.empty()) {
            query += " AND category = $" + std::to_string(paramIdx++);
            params.push_back(categoryFilter);
        }

        if (activeOnly) {
            query += " AND is_active = " + trueVal;
        }

        query += " ORDER BY category, sort_order, code ";
        query += common::db::paginationClause(dbType, limit, offset);

        Json::Value result = executor_->executeQuery(query, params);

        std::vector<domain::models::CodeMaster> items;
        if (result.isArray()) {
            for (const auto& row : result) {
                items.push_back(jsonToModel(row));
            }
        }

        return items;

    } catch (const std::exception& e) {
        spdlog::error("[CodeMasterRepository] findAll failed: {}", e.what());
        return {};
    }
}

int CodeMasterRepository::countAll(const std::string& categoryFilter, bool activeOnly) {
    try {
        std::string dbType = executor_->getDatabaseType();
        std::string trueVal = common::db::boolLiteral(dbType, true);

        std::string query = "SELECT COUNT(*) FROM code_master WHERE 1=1";
        std::vector<std::string> params;
        int paramIdx = 1;

        if (!categoryFilter.empty()) {
            query += " AND category = $" + std::to_string(paramIdx++);
            params.push_back(categoryFilter);
        }

        if (activeOnly) {
            query += " AND is_active = " + trueVal;
        }

        Json::Value result = executor_->executeScalar(query, params);
        return common::db::scalarToInt(result);

    } catch (const std::exception& e) {
        spdlog::error("[CodeMasterRepository] countAll failed: {}", e.what());
        return 0;
    }
}

std::optional<domain::models::CodeMaster> CodeMasterRepository::findById(const std::string& id) {
    try {
        const char* query =
            "SELECT id, category, code, name_ko, name_en, description, "
            "       severity, sort_order, is_active, metadata, created_at, updated_at "
            "FROM code_master WHERE id = $1";

        std::vector<std::string> params = { id };
        Json::Value result = executor_->executeQuery(query, params);

        if (result.isArray() && result.size() > 0) {
            return jsonToModel(result[0]);
        }

        return std::nullopt;

    } catch (const std::exception& e) {
        spdlog::error("[CodeMasterRepository] findById failed: {}", e.what());
        return std::nullopt;
    }
}

std::vector<std::string> CodeMasterRepository::getCategories() {
    try {
        std::string dbType = executor_->getDatabaseType();
        std::string trueVal = common::db::boolLiteral(dbType, true);

        std::string query =
            "SELECT DISTINCT category FROM code_master "
            "WHERE is_active = " + trueVal + " "
            "ORDER BY category";

        Json::Value result = executor_->executeQuery(query);

        std::vector<std::string> categories;
        if (result.isArray()) {
            for (const auto& row : result) {
                categories.push_back(row.get("category", "").asString());
            }
        }

        return categories;

    } catch (const std::exception& e) {
        spdlog::error("[CodeMasterRepository] getCategories failed: {}", e.what());
        return {};
    }
}

bool CodeMasterRepository::insert(const domain::models::CodeMaster& item) {
    try {
        std::string dbType = executor_->getDatabaseType();
        std::string isActiveVal = common::db::boolLiteral(dbType, item.isActive);

        std::string query =
            "INSERT INTO code_master (category, code, name_ko, name_en, description, "
            "                         severity, sort_order, is_active, metadata) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, " + isActiveVal + ", $8)";

        std::vector<std::string> params = {
            item.category,
            item.code,
            item.nameKo,
            item.nameEn.value_or(""),
            item.description.value_or(""),
            item.severity.value_or(""),
            std::to_string(item.sortOrder),
            item.metadata.value_or("")
        };

        int rowsAffected = executor_->executeCommand(query, params);

        if (rowsAffected > 0) {
            spdlog::info("[CodeMasterRepository] Inserted: {}/{}", item.category, item.code);
            return true;
        }

        return false;

    } catch (const std::exception& e) {
        spdlog::error("[CodeMasterRepository] Insert failed: {}", e.what());
        return false;
    }
}

bool CodeMasterRepository::update(const domain::models::CodeMaster& item) {
    try {
        std::string dbType = executor_->getDatabaseType();
        std::string isActiveVal = common::db::boolLiteral(dbType, item.isActive);

        std::string tsFunc = (dbType == "oracle") ? "SYSTIMESTAMP" : "NOW()";

        std::string query =
            "UPDATE code_master SET "
            "  name_ko = $1, name_en = $2, description = $3, "
            "  severity = $4, sort_order = $5, is_active = " + isActiveVal + ", "
            "  metadata = $6, updated_at = " + tsFunc + " "
            "WHERE id = $7";

        std::vector<std::string> params = {
            item.nameKo,
            item.nameEn.value_or(""),
            item.description.value_or(""),
            item.severity.value_or(""),
            std::to_string(item.sortOrder),
            item.metadata.value_or(""),
            item.id
        };

        int rowsAffected = executor_->executeCommand(query, params);

        if (rowsAffected > 0) {
            spdlog::info("[CodeMasterRepository] Updated: {}", item.id);
            return true;
        }

        return false;

    } catch (const std::exception& e) {
        spdlog::error("[CodeMasterRepository] Update failed: {}", e.what());
        return false;
    }
}

bool CodeMasterRepository::deactivate(const std::string& id) {
    try {
        std::string dbType = executor_->getDatabaseType();
        std::string falseVal = common::db::boolLiteral(dbType, false);
        std::string tsFunc = (dbType == "oracle") ? "SYSTIMESTAMP" : "NOW()";

        std::string query =
            "UPDATE code_master SET is_active = " + falseVal + ", "
            "  updated_at = " + tsFunc + " "
            "WHERE id = $1";

        std::vector<std::string> params = { id };
        int rowsAffected = executor_->executeCommand(query, params);

        if (rowsAffected > 0) {
            spdlog::info("[CodeMasterRepository] Deactivated: {}", id);
            return true;
        }

        return false;

    } catch (const std::exception& e) {
        spdlog::error("[CodeMasterRepository] Deactivate failed: {}", e.what());
        return false;
    }
}

// --- Private Helpers ---

domain::models::CodeMaster CodeMasterRepository::jsonToModel(const Json::Value& row) {
    domain::models::CodeMaster item;

    item.id = row.get("id", "").asString();
    item.category = row.get("category", "").asString();
    item.code = row.get("code", "").asString();
    item.nameKo = row.get("name_ko", "").asString();
    item.nameEn = getOptionalString(row.get("name_en", Json::nullValue));
    item.description = getOptionalString(row.get("description", Json::nullValue));
    item.severity = getOptionalString(row.get("severity", Json::nullValue));
    item.sortOrder = common::db::scalarToInt(row.get("sort_order", 0));

    // Handle boolean (Oracle: "1"/"0", PostgreSQL: "t"/"f" or bool)
    Json::Value isActiveVal = row.get("is_active", true);
    if (isActiveVal.isBool()) {
        item.isActive = isActiveVal.asBool();
    } else if (isActiveVal.isString()) {
        std::string s = isActiveVal.asString();
        item.isActive = (s == "t" || s == "true" || s == "1");
    } else if (isActiveVal.isInt()) {
        item.isActive = (isActiveVal.asInt() != 0);
    } else {
        item.isActive = true;
    }

    item.metadata = getOptionalString(row.get("metadata", Json::nullValue));
    item.createdAt = row.get("created_at", "").asString();
    item.updatedAt = row.get("updated_at", "").asString();

    return item;
}

std::optional<std::string> CodeMasterRepository::getOptionalString(const Json::Value& val) {
    if (val.isNull() || (val.isString() && val.asString().empty())) {
        return std::nullopt;
    }
    return val.asString();
}

} // namespace repositories
