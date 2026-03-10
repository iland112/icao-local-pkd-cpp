#pragma once

/**
 * @file pending_dsc_repository.h
 * @brief Repository for pending DSC registration approval workflow
 *
 * CRUD operations on pending_dsc_registration table.
 * Supports PostgreSQL + Oracle via IQueryExecutor.
 *
 * @date 2026-03-10
 */

#include "i_query_executor.h"
#include <json/json.h>
#include <string>

namespace repositories {

class PendingDscRepository {
public:
    explicit PendingDscRepository(common::IQueryExecutor* queryExecutor);

    /** List pending DSC registrations (paginated, filterable) */
    Json::Value findAll(int limit, int offset,
                        const std::string& status = "",
                        const std::string& countryCode = "");

    /** Count pending DSC registrations */
    int countAll(const std::string& status = "",
                 const std::string& countryCode = "");

    /** Find by ID */
    Json::Value findById(const std::string& id);

    /** Update status to APPROVED/REJECTED with reviewer info */
    bool updateStatus(const std::string& id,
                      const std::string& status,
                      const std::string& reviewedBy,
                      const std::string& reviewComment = "");

    /** Delete a pending entry (after approval, move to certificate) */
    bool deleteById(const std::string& id);

    /** Get summary statistics */
    Json::Value getStatistics();

private:
    common::IQueryExecutor* queryExecutor_;
};

} // namespace repositories
