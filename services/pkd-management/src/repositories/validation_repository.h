#pragma once

#include <string>
#include <vector>
#include <libpq-fe.h>
#include <json/json.h>

/**
 * @file validation_repository.h
 * @brief Validation Repository - Database Access Layer for validation_result table
 *
 * @note Part of main.cpp refactoring Phase 1.5
 * @date 2026-01-29
 */

namespace repositories {

class ValidationRepository {
public:
    explicit ValidationRepository(PGconn* dbConn);
    ~ValidationRepository() = default;

    /**
     * @brief Save validation result
     */
    bool save(
        const std::string& fingerprint,
        const std::string& uploadId,
        const std::string& certificateType,
        const std::string& validationStatus,
        bool trustChainValid,
        const std::string& trustChainPath,
        bool signatureValid,
        bool crlChecked,
        bool revoked
    );

    /**
     * @brief Find validation by fingerprint
     */
    Json::Value findByFingerprint(const std::string& fingerprint);

    /**
     * @brief Find validations by upload ID
     */
    Json::Value findByUploadId(
        const std::string& uploadId,
        int limit,
        int offset,
        const std::string& statusFilter = ""
    );

    /**
     * @brief Count validations by status
     */
    int countByStatus(const std::string& status);

private:
    PGconn* dbConn_;

    PGresult* executeParamQuery(const std::string& query, const std::vector<std::string>& params);
    PGresult* executeQuery(const std::string& query);
    Json::Value pgResultToJson(PGresult* res);
};

} // namespace repositories
