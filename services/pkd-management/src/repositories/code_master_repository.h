#pragma once

/**
 * @file code_master_repository.h
 * @brief Repository for code_master table — centralized code/status management
 *
 * Uses IQueryExecutor for database-agnostic operation (PostgreSQL + Oracle).
 */

#include <string>
#include <vector>
#include <optional>
#include <json/json.h>
#include "i_query_executor.h"
#include "../domain/models/code_master.h"

namespace repositories {

class CodeMasterRepository {
public:
    explicit CodeMasterRepository(common::IQueryExecutor* executor);
    ~CodeMasterRepository();

    /**
     * @brief Find all codes by category
     * @param category Code category (e.g., "VALIDATION_STATUS")
     * @param activeOnly If true, only return active codes
     * @return Vector of CodeMaster items ordered by sort_order
     */
    std::vector<domain::models::CodeMaster> findByCategory(
        const std::string& category, bool activeOnly = true);

    /**
     * @brief Find all codes with optional category filter and pagination
     */
    std::vector<domain::models::CodeMaster> findAll(
        const std::string& categoryFilter = "",
        bool activeOnly = true,
        int limit = 200, int offset = 0);

    /**
     * @brief Count codes with optional category filter
     */
    int countAll(const std::string& categoryFilter = "", bool activeOnly = true);

    /**
     * @brief Find a single code by ID
     */
    std::optional<domain::models::CodeMaster> findById(const std::string& id);

    /**
     * @brief Get all distinct categories
     */
    std::vector<std::string> getCategories();

    /**
     * @brief Insert a new code
     * @return true if inserted, false on duplicate or error
     */
    bool insert(const domain::models::CodeMaster& item);

    /**
     * @brief Update an existing code
     */
    bool update(const domain::models::CodeMaster& item);

    /**
     * @brief Deactivate a code (soft delete)
     */
    bool deactivate(const std::string& id);

private:
    common::IQueryExecutor* executor_;

    domain::models::CodeMaster jsonToModel(const Json::Value& row);
    std::optional<std::string> getOptionalString(const Json::Value& val);
};

} // namespace repositories
