#include "upload_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cstring>
#include <sstream>

namespace repositories {

// ============================================================================
// Constructor
// ============================================================================

UploadRepository::UploadRepository(PGconn* dbConn)
    : dbConn_(dbConn)
{
    if (!dbConn_) {
        throw std::invalid_argument("UploadRepository: dbConn cannot be nullptr");
    }
    spdlog::debug("[UploadRepository] Initialized");
}

// ============================================================================
// CRUD Operations
// ============================================================================

bool UploadRepository::insert(const Upload& upload)
{
    spdlog::debug("[UploadRepository] Inserting upload: {}", upload.fileName);

    try {
        const char* query =
            "INSERT INTO uploaded_file "
            "(id, file_name, file_format, file_size, status, uploaded_by, created_at, updated_at) "
            "VALUES ($1, $2, $3, $4, $5, $6, NOW(), NOW())";

        std::vector<std::string> params = {
            upload.id,
            upload.fileName,
            upload.fileFormat,
            std::to_string(upload.fileSize),
            upload.status,
            upload.uploadedBy
        };

        PGresult* res = executeParamQuery(query, params);
        PQclear(res);

        spdlog::info("[UploadRepository] Upload inserted: {} ({})", upload.fileName, upload.id);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[UploadRepository] Insert failed: {}", e.what());
        return false;
    }
}

std::optional<Upload> UploadRepository::findById(const std::string& uploadId)
{
    spdlog::debug("[UploadRepository] Finding upload by ID: {}", uploadId);

    try {
        const char* query =
            "SELECT id, file_name, file_format, file_size, status, uploaded_by, "
            "error_message, processing_mode, total_entries, processed_entries, "
            "csca_count, dsc_count, dsc_nc_count, crl_count, mlsc_count, ml_count, "
            "created_at, updated_at, "
            "COALESCE(validation_valid_count, 0), COALESCE(validation_invalid_count, 0), "
            "COALESCE(validation_pending_count, 0), COALESCE(validation_error_count, 0), "
            "COALESCE(trust_chain_valid_count, 0), COALESCE(trust_chain_invalid_count, 0), "
            "COALESCE(csca_not_found_count, 0), COALESCE(expired_count, 0), COALESCE(revoked_count, 0) "
            "FROM uploaded_file WHERE id = $1";

        std::vector<std::string> params = {uploadId};
        PGresult* res = executeParamQuery(query, params);

        if (PQntuples(res) == 0) {
            PQclear(res);
            spdlog::debug("[UploadRepository] Upload not found: {}", uploadId);
            return std::nullopt;
        }

        Upload upload = resultToUpload(res, 0);
        PQclear(res);

        return upload;

    } catch (const std::exception& e) {
        spdlog::error("[UploadRepository] Find by ID failed: {}", e.what());
        return std::nullopt;
    }
}

std::vector<Upload> UploadRepository::findAll(
    int limit,
    int offset,
    const std::string& sortBy,
    const std::string& direction
)
{
    spdlog::debug("[UploadRepository] Finding all uploads (limit: {}, offset: {})",
        limit, offset);

    std::vector<Upload> uploads;

    try {
        std::ostringstream query;
        query << "SELECT id, file_name, file_format, file_size, status, uploaded_by, "
              << "error_message, processing_mode, total_entries, processed_entries, "
              << "csca_count, dsc_count, dsc_nc_count, crl_count, mlsc_count, ml_count, "
              << "created_at, updated_at, "
              << "COALESCE(validation_valid_count, 0), COALESCE(validation_invalid_count, 0), "
              << "COALESCE(validation_pending_count, 0), COALESCE(validation_error_count, 0), "
              << "COALESCE(trust_chain_valid_count, 0), COALESCE(trust_chain_invalid_count, 0), "
              << "COALESCE(csca_not_found_count, 0), COALESCE(expired_count, 0), COALESCE(revoked_count, 0) "
              << "FROM uploaded_file "
              << "ORDER BY " << sortBy << " " << direction << " "
              << "LIMIT " << limit << " OFFSET " << offset;

        PGresult* res = executeQuery(query.str());

        int rows = PQntuples(res);
        uploads.reserve(rows);

        for (int i = 0; i < rows; ++i) {
            uploads.push_back(resultToUpload(res, i));
        }

        PQclear(res);

        spdlog::debug("[UploadRepository] Found {} uploads", rows);

    } catch (const std::exception& e) {
        spdlog::error("[UploadRepository] Find all failed: {}", e.what());
    }

    return uploads;
}

bool UploadRepository::updateStatus(
    const std::string& uploadId,
    const std::string& status,
    const std::string& errorMessage
)
{
    spdlog::debug("[UploadRepository] Updating status: {} -> {}", uploadId, status);

    try {
        std::string query;
        std::vector<std::string> params;

        if (errorMessage.empty()) {
            query = "UPDATE uploaded_file SET status = $1, updated_at = NOW() WHERE id = $2";
            params = {status, uploadId};
        } else {
            query = "UPDATE uploaded_file SET status = $1, error_message = $2, updated_at = NOW() WHERE id = $3";
            params = {status, errorMessage, uploadId};
        }

        PGresult* res = executeParamQuery(query, params);
        PQclear(res);

        spdlog::info("[UploadRepository] Status updated: {} -> {}", uploadId, status);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[UploadRepository] Update status failed: {}", e.what());
        return false;
    }
}

bool UploadRepository::updateStatistics(
    const std::string& uploadId,
    int cscaCount,
    int dscCount,
    int dscNcCount,
    int crlCount,
    int mlscCount,
    int mlCount
)
{
    spdlog::debug("[UploadRepository] Updating statistics: {}", uploadId);

    try {
        const char* query =
            "UPDATE uploaded_file SET "
            "csca_count = $1, dsc_count = $2, dsc_nc_count = $3, crl_count = $4, "
            "mlsc_count = $5, ml_count = $6, updated_at = NOW() "
            "WHERE id = $7";

        std::vector<std::string> params = {
            std::to_string(cscaCount),
            std::to_string(dscCount),
            std::to_string(dscNcCount),
            std::to_string(crlCount),
            std::to_string(mlscCount),
            std::to_string(mlCount),
            uploadId
        };

        PGresult* res = executeParamQuery(query, params);
        PQclear(res);

        spdlog::info("[UploadRepository] Statistics updated: {}", uploadId);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[UploadRepository] Update statistics failed: {}", e.what());
        return false;
    }
}

bool UploadRepository::deleteById(const std::string& uploadId)
{
    spdlog::debug("[UploadRepository] Deleting upload: {}", uploadId);

    try {
        const char* query = "DELETE FROM uploaded_file WHERE id = $1";
        std::vector<std::string> params = {uploadId};

        PGresult* res = executeParamQuery(query, params);
        PQclear(res);

        spdlog::info("[UploadRepository] Upload deleted: {}", uploadId);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[UploadRepository] Delete failed: {}", e.what());
        return false;
    }
}

bool UploadRepository::updateFileHash(const std::string& uploadId, const std::string& fileHash)
{
    spdlog::debug("[UploadRepository] Updating file hash: {}", uploadId);

    try {
        const char* query = "UPDATE uploaded_file SET file_hash = $1 WHERE id = $2";
        std::vector<std::string> params = {fileHash, uploadId};

        PGresult* res = executeParamQuery(query, params);
        PQclear(res);

        spdlog::debug("[UploadRepository] File hash updated: {}", uploadId);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[UploadRepository] Update file hash failed: {}", e.what());
        return false;
    }
}

// ============================================================================
// Business-Specific Queries
// ============================================================================

std::optional<Upload> UploadRepository::findByFileHash(const std::string& fileHash)
{
    spdlog::debug("[UploadRepository] Finding upload by file hash: {}", fileHash.substr(0, 16) + "...");

    try {
        const char* query =
            "SELECT id, file_name, file_format, file_size, status, uploaded_by, "
            "error_message, processing_mode, total_entries, processed_entries, "
            "csca_count, dsc_count, dsc_nc_count, crl_count, mlsc_count, ml_count, "
            "created_at, updated_at, "
            "COALESCE(validation_valid_count, 0), COALESCE(validation_invalid_count, 0), "
            "COALESCE(validation_pending_count, 0), COALESCE(validation_error_count, 0), "
            "COALESCE(trust_chain_valid_count, 0), COALESCE(trust_chain_invalid_count, 0), "
            "COALESCE(csca_not_found_count, 0), COALESCE(expired_count, 0), COALESCE(revoked_count, 0) "
            "FROM uploaded_file WHERE file_hash = $1 LIMIT 1";

        std::vector<std::string> params = {fileHash};
        PGresult* res = executeParamQuery(query, params);

        if (PQntuples(res) == 0) {
            PQclear(res);
            spdlog::debug("[UploadRepository] No duplicate found for hash: {}", fileHash.substr(0, 16) + "...");
            return std::nullopt;
        }

        Upload upload = resultToUpload(res, 0);
        PQclear(res);

        spdlog::info("[UploadRepository] Duplicate upload found: {}", upload.id);
        return upload;

    } catch (const std::exception& e) {
        spdlog::error("[UploadRepository] Find by file hash failed: {}", e.what());
        return std::nullopt;
    }
}

int UploadRepository::countByStatus(const std::string& status)
{
    spdlog::debug("[UploadRepository] Counting by status: {}", status);

    try {
        const char* query = "SELECT COUNT(*) FROM uploaded_file WHERE status = $1";
        std::vector<std::string> params = {status};

        PGresult* res = executeParamQuery(query, params);
        int count = std::atoi(PQgetvalue(res, 0, 0));
        PQclear(res);

        return count;

    } catch (const std::exception& e) {
        spdlog::error("[UploadRepository] Count by status failed: {}", e.what());
        return 0;
    }
}

int UploadRepository::countAll()
{
    spdlog::debug("[UploadRepository] Counting all uploads");

    try {
        const char* query = "SELECT COUNT(*) FROM uploaded_file";
        PGresult* res = executeQuery(query);
        int count = std::atoi(PQgetvalue(res, 0, 0));
        PQclear(res);

        return count;

    } catch (const std::exception& e) {
        spdlog::error("[UploadRepository] Count all failed: {}", e.what());
        return 0;
    }
}

std::vector<Upload> UploadRepository::findRecentUploads(int hours)
{
    spdlog::debug("[UploadRepository] Finding recent uploads (last {} hours)", hours);

    std::vector<Upload> uploads;

    try {
        // TODO: Implement recent uploads query
        spdlog::warn("[UploadRepository] findRecentUploads - TODO: Implement");

    } catch (const std::exception& e) {
        spdlog::error("[UploadRepository] Find recent uploads failed: {}", e.what());
    }

    return uploads;
}

Json::Value UploadRepository::getStatisticsSummary()
{
    spdlog::debug("[UploadRepository] Getting statistics summary");

    Json::Value response;

    try {
        // TODO: Implement statistics summary query
        spdlog::warn("[UploadRepository] getStatisticsSummary - TODO: Implement");
        response["success"] = false;
        response["message"] = "Not yet implemented";

    } catch (const std::exception& e) {
        spdlog::error("[UploadRepository] Get statistics summary failed: {}", e.what());
        response["error"] = e.what();
    }

    return response;
}

Json::Value UploadRepository::getCountryStatistics()
{
    spdlog::debug("[UploadRepository] Getting country statistics");

    Json::Value response;

    try {
        // TODO: Implement country statistics query
        spdlog::warn("[UploadRepository] getCountryStatistics - TODO: Implement");
        response["success"] = false;
        response["message"] = "Not yet implemented";

    } catch (const std::exception& e) {
        spdlog::error("[UploadRepository] Get country statistics failed: {}", e.what());
        response["error"] = e.what();
    }

    return response;
}

Json::Value UploadRepository::getDetailedCountryStatistics(int limit)
{
    spdlog::debug("[UploadRepository] Getting detailed country statistics (limit: {})", limit);

    Json::Value response;

    try {
        // TODO: Implement detailed country statistics query
        spdlog::warn("[UploadRepository] getDetailedCountryStatistics - TODO: Implement");
        response["success"] = false;
        response["message"] = "Not yet implemented";

    } catch (const std::exception& e) {
        spdlog::error("[UploadRepository] Get detailed country statistics failed: {}", e.what());
        response["error"] = e.what();
    }

    return response;
}

// ============================================================================
// Private Helper Methods
// ============================================================================

PGresult* UploadRepository::executeParamQuery(
    const std::string& query,
    const std::vector<std::string>& params
)
{
    std::vector<const char*> paramValues;
    for (const auto& param : params) {
        paramValues.push_back(param.c_str());
    }

    PGresult* res = PQexecParams(
        dbConn_,
        query.c_str(),
        params.size(),
        nullptr,
        paramValues.data(),
        nullptr,
        nullptr,
        0
    );

    if (!res) {
        throw std::runtime_error("Query execution failed: null result");
    }

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        std::string error = PQerrorMessage(dbConn_);
        PQclear(res);
        throw std::runtime_error("Query failed: " + error);
    }

    return res;
}

PGresult* UploadRepository::executeQuery(const std::string& query)
{
    PGresult* res = PQexec(dbConn_, query.c_str());

    if (!res) {
        throw std::runtime_error("Query execution failed: null result");
    }

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        std::string error = PQerrorMessage(dbConn_);
        PQclear(res);
        throw std::runtime_error("Query failed: " + error);
    }

    return res;
}

Upload UploadRepository::resultToUpload(PGresult* res, int row)
{
    Upload upload;

    upload.id = PQgetvalue(res, row, PQfnumber(res, "id"));
    upload.fileName = PQgetvalue(res, row, PQfnumber(res, "file_name"));
    upload.fileFormat = PQgetvalue(res, row, PQfnumber(res, "file_format"));
    upload.fileSize = std::atoi(PQgetvalue(res, row, PQfnumber(res, "file_size")));
    upload.status = PQgetvalue(res, row, PQfnumber(res, "status"));
    upload.uploadedBy = PQgetvalue(res, row, PQfnumber(res, "uploaded_by"));

    upload.errorMessage = getOptionalString(res, row, PQfnumber(res, "error_message"));
    upload.processingMode = getOptionalString(res, row, PQfnumber(res, "processing_mode"));

    upload.totalEntries = std::atoi(PQgetvalue(res, row, PQfnumber(res, "total_entries")));
    upload.processedEntries = std::atoi(PQgetvalue(res, row, PQfnumber(res, "processed_entries")));

    upload.cscaCount = std::atoi(PQgetvalue(res, row, PQfnumber(res, "csca_count")));
    upload.dscCount = std::atoi(PQgetvalue(res, row, PQfnumber(res, "dsc_count")));
    upload.dscNcCount = std::atoi(PQgetvalue(res, row, PQfnumber(res, "dsc_nc_count")));
    upload.crlCount = std::atoi(PQgetvalue(res, row, PQfnumber(res, "crl_count")));
    upload.mlscCount = std::atoi(PQgetvalue(res, row, PQfnumber(res, "mlsc_count")));
    upload.mlCount = std::atoi(PQgetvalue(res, row, PQfnumber(res, "ml_count")));

    // Validation statistics (using column indices after the named fields)
    upload.validationValidCount = std::atoi(PQgetvalue(res, row, 17));  // COALESCE(validation_valid_count, 0)
    upload.validationInvalidCount = std::atoi(PQgetvalue(res, row, 18));  // COALESCE(validation_invalid_count, 0)
    upload.validationPendingCount = std::atoi(PQgetvalue(res, row, 19));  // COALESCE(validation_pending_count, 0)
    upload.validationErrorCount = std::atoi(PQgetvalue(res, row, 20));  // COALESCE(validation_error_count, 0)
    upload.trustChainValidCount = std::atoi(PQgetvalue(res, row, 21));  // COALESCE(trust_chain_valid_count, 0)
    upload.trustChainInvalidCount = std::atoi(PQgetvalue(res, row, 22));  // COALESCE(trust_chain_invalid_count, 0)
    upload.cscaNotFoundCount = std::atoi(PQgetvalue(res, row, 23));  // COALESCE(csca_not_found_count, 0)
    upload.expiredCount = std::atoi(PQgetvalue(res, row, 24));  // COALESCE(expired_count, 0)
    upload.revokedCount = std::atoi(PQgetvalue(res, row, 25));  // COALESCE(revoked_count, 0)

    upload.createdAt = PQgetvalue(res, row, 26);  // created_at
    upload.updatedAt = PQgetvalue(res, row, 27);  // updated_at

    return upload;
}

Json::Value UploadRepository::pgResultToJson(PGresult* res)
{
    Json::Value array = Json::arrayValue;

    int rows = PQntuples(res);
    int cols = PQnfields(res);

    for (int i = 0; i < rows; ++i) {
        Json::Value row;
        for (int j = 0; j < cols; ++j) {
            const char* fieldName = PQfname(res, j);
            const char* value = PQgetvalue(res, i, j);

            if (PQgetisnull(res, i, j)) {
                row[fieldName] = Json::nullValue;
            } else {
                Oid type = PQftype(res, j);
                if (type == 23 || type == 20) {  // INT4 or INT8
                    row[fieldName] = std::atoi(value);
                } else if (type == 700 || type == 701) {  // FLOAT4 or FLOAT8
                    row[fieldName] = std::atof(value);
                } else if (type == 16) {  // BOOL
                    row[fieldName] = (value[0] == 't');
                } else {
                    row[fieldName] = value;
                }
            }
        }
        array.append(row);
    }

    return array;
}

std::optional<std::string> UploadRepository::getOptionalString(PGresult* res, int row, int col)
{
    if (PQgetisnull(res, row, col)) {
        return std::nullopt;
    }
    return std::string(PQgetvalue(res, row, col));
}

std::optional<int> UploadRepository::getOptionalInt(PGresult* res, int row, int col)
{
    if (PQgetisnull(res, row, col)) {
        return std::nullopt;
    }
    return std::atoi(PQgetvalue(res, row, col));
}

} // namespace repositories
