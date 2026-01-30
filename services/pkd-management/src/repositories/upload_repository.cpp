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
            "(id, file_name, file_hash, file_format, file_size, status, uploaded_by, upload_timestamp) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, NOW())";

        std::vector<std::string> params = {
            upload.id,
            upload.fileName,
            upload.fileHash,
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
            "SELECT id, file_name, file_hash, file_format, file_size, status, uploaded_by, "
            "error_message, processing_mode, total_entries, processed_entries, "
            "csca_count, dsc_count, dsc_nc_count, crl_count, mlsc_count, ml_count, "
            "upload_timestamp, completed_timestamp, "
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
        // Map domain field names to database column names (support both camelCase and snake_case)
        std::string dbSortBy = sortBy;
        if (sortBy == "createdAt" || sortBy == "created_at") {
            dbSortBy = "upload_timestamp";
        } else if (sortBy == "updatedAt" || sortBy == "updated_at") {
            dbSortBy = "completed_timestamp";
        }

        std::ostringstream query;
        query << "SELECT id, file_name, file_hash, file_format, file_size, status, uploaded_by, "
              << "error_message, processing_mode, total_entries, processed_entries, "
              << "csca_count, dsc_count, dsc_nc_count, crl_count, mlsc_count, ml_count, "
              << "upload_timestamp, completed_timestamp, "
              << "COALESCE(validation_valid_count, 0), COALESCE(validation_invalid_count, 0), "
              << "COALESCE(validation_pending_count, 0), COALESCE(validation_error_count, 0), "
              << "COALESCE(trust_chain_valid_count, 0), COALESCE(trust_chain_invalid_count, 0), "
              << "COALESCE(csca_not_found_count, 0), COALESCE(expired_count, 0), COALESCE(revoked_count, 0) "
              << "FROM uploaded_file "
              << "ORDER BY " << dbSortBy << " " << direction << " "
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
            query = "UPDATE uploaded_file SET status = $1, "
                   "completed_timestamp = CASE WHEN $1 IN ('COMPLETED', 'FAILED') THEN NOW() ELSE completed_timestamp END "
                   "WHERE id = $2";
            params = {status, uploadId};
        } else {
            query = "UPDATE uploaded_file SET status = $1, error_message = $2, "
                   "completed_timestamp = CASE WHEN $1 IN ('COMPLETED', 'FAILED') THEN NOW() ELSE completed_timestamp END "
                   "WHERE id = $3";
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
            "mlsc_count = $5, ml_count = $6 "
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
            "upload_timestamp, completed_timestamp, "
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
        // Get total uploads and status breakdown
        const char* countQuery = "SELECT COUNT(*) FROM uploaded_file";
        PGresult* countRes = executeQuery(countQuery);
        int totalUploads = std::atoi(PQgetvalue(countRes, 0, 0));
        PQclear(countRes);

        // Get certificate counts by type
        const char* certQuery =
            "SELECT "
            "COALESCE(SUM(csca_count), 0) as total_csca, "
            "COALESCE(SUM(dsc_count), 0) as total_dsc, "
            "COALESCE(SUM(dsc_nc_count), 0) as total_dsc_nc, "
            "COALESCE(SUM(mlsc_count), 0) as total_mlsc, "
            "COALESCE(SUM(crl_count), 0) as total_crl, "
            "COALESCE(SUM(ml_count), 0) as total_ml "
            "FROM uploaded_file";

        PGresult* certRes = executeQuery(certQuery);
        int totalCsca = std::atoi(PQgetvalue(certRes, 0, 0));
        int totalDsc = std::atoi(PQgetvalue(certRes, 0, 1));
        int totalDscNc = std::atoi(PQgetvalue(certRes, 0, 2));
        int totalMlsc = std::atoi(PQgetvalue(certRes, 0, 3));
        int totalCrl = std::atoi(PQgetvalue(certRes, 0, 4));
        int totalMl = std::atoi(PQgetvalue(certRes, 0, 5));
        PQclear(certRes);

        // Get uploads by status (for successfulUploads/failedUploads)
        const char* statusQuery =
            "SELECT status, COUNT(*) FROM uploaded_file GROUP BY status";
        PGresult* statusRes = executeQuery(statusQuery);

        int successfulUploads = 0;
        int failedUploads = 0;
        int statusRows = PQntuples(statusRes);
        for (int i = 0; i < statusRows; i++) {
            std::string status = PQgetvalue(statusRes, i, 0);
            int count = std::atoi(PQgetvalue(statusRes, i, 1));
            if (status == "COMPLETED") {
                successfulUploads += count;
            } else if (status == "FAILED" || status == "ERROR") {
                failedUploads += count;
            }
        }
        PQclear(statusRes);

        // Get country count (distinct countries)
        const char* countryQuery =
            "SELECT COUNT(DISTINCT country_code) FROM certificate "
            "WHERE country_code IS NOT NULL AND country_code != ''";
        PGresult* countryRes = executeQuery(countryQuery);
        int countriesCount = std::atoi(PQgetvalue(countryRes, 0, 0));
        PQclear(countryRes);

        // Get validation statistics
        const char* validationQuery =
            "SELECT "
            "COALESCE(SUM(CASE WHEN validation_status = 'VALID' THEN 1 ELSE 0 END), 0) as valid_count, "
            "COALESCE(SUM(CASE WHEN validation_status = 'INVALID' THEN 1 ELSE 0 END), 0) as invalid_count, "
            "COALESCE(SUM(CASE WHEN validation_status = 'PENDING' THEN 1 ELSE 0 END), 0) as pending_count, "
            "COALESCE(SUM(CASE WHEN validation_status = 'ERROR' THEN 1 ELSE 0 END), 0) as error_count, "
            "COALESCE(SUM(CASE WHEN trust_chain_valid = true THEN 1 ELSE 0 END), 0) as trust_chain_valid_count, "
            "COALESCE(SUM(CASE WHEN trust_chain_valid = false THEN 1 ELSE 0 END), 0) as trust_chain_invalid_count, "
            "COALESCE(SUM(CASE WHEN csca_found = false THEN 1 ELSE 0 END), 0) as csca_not_found_count, "
            "COALESCE(SUM(CASE WHEN is_expired = true THEN 1 ELSE 0 END), 0) as expired_count, "
            "COALESCE(SUM(CASE WHEN revocation_status = 'REVOKED' THEN 1 ELSE 0 END), 0) as revoked_count "
            "FROM validation_result";

        PGresult* validationRes = executeQuery(validationQuery);
        Json::Value validation;
        validation["validCount"] = std::atoi(PQgetvalue(validationRes, 0, 0));
        validation["invalidCount"] = std::atoi(PQgetvalue(validationRes, 0, 1));
        validation["pendingCount"] = std::atoi(PQgetvalue(validationRes, 0, 2));
        validation["errorCount"] = std::atoi(PQgetvalue(validationRes, 0, 3));
        validation["trustChainValidCount"] = std::atoi(PQgetvalue(validationRes, 0, 4));
        validation["trustChainInvalidCount"] = std::atoi(PQgetvalue(validationRes, 0, 5));
        validation["cscaNotFoundCount"] = std::atoi(PQgetvalue(validationRes, 0, 6));
        validation["expiredCount"] = std::atoi(PQgetvalue(validationRes, 0, 7));
        validation["revokedCount"] = std::atoi(PQgetvalue(validationRes, 0, 8));
        PQclear(validationRes);

        // Build response matching frontend UploadStatisticsOverview interface
        response["totalUploads"] = totalUploads;
        response["successfulUploads"] = successfulUploads;
        response["failedUploads"] = failedUploads;
        response["totalCertificates"] = totalCsca + totalDsc + totalDscNc + totalMlsc;
        response["cscaCount"] = totalCsca;
        response["mlscCount"] = totalMlsc;
        response["dscCount"] = totalDsc;
        response["dscNcCount"] = totalDscNc;
        response["crlCount"] = totalCrl;
        response["mlCount"] = totalMl;
        response["countriesCount"] = countriesCount;
        response["validation"] = validation;

        spdlog::debug("[UploadRepository] Statistics: {} uploads ({} successful, {} failed), {} certificates, {} countries",
            totalUploads, successfulUploads, failedUploads, response["totalCertificates"].asInt(), countriesCount);

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
        // Get certificate counts by country and type (top 20 countries)
        const char* query =
            "SELECT "
            "c.country_code, "
            "SUM(CASE WHEN c.certificate_type = 'CSCA' THEN 1 ELSE 0 END) as csca_count, "
            "SUM(CASE WHEN c.certificate_type = 'MLSC' THEN 1 ELSE 0 END) as mlsc_count, "
            "SUM(CASE WHEN c.certificate_type = 'DSC' THEN 1 ELSE 0 END) as dsc_count, "
            "SUM(CASE WHEN c.certificate_type = 'DSC_NC' THEN 1 ELSE 0 END) as dsc_nc_count, "
            "COUNT(*) as total_certificates "
            "FROM certificate c "
            "WHERE c.country_code IS NOT NULL AND c.country_code != '' "
            "GROUP BY c.country_code "
            "ORDER BY total_certificates DESC "
            "LIMIT 20";

        PGresult* res = executeQuery(query);
        int rows = PQntuples(res);

        Json::Value countries = Json::arrayValue;
        for (int i = 0; i < rows; i++) {
            Json::Value countryData;
            countryData["country"] = PQgetvalue(res, i, 0);
            countryData["csca"] = std::atoi(PQgetvalue(res, i, 1));
            countryData["mlsc"] = std::atoi(PQgetvalue(res, i, 2));
            countryData["dsc"] = std::atoi(PQgetvalue(res, i, 3));
            countryData["dscNc"] = std::atoi(PQgetvalue(res, i, 4));
            countryData["total"] = std::atoi(PQgetvalue(res, i, 5));
            countries.append(countryData);
        }

        PQclear(res);

        response["countries"] = countries;
        response["totalCountries"] = rows;

        spdlog::debug("[UploadRepository] Found {} countries with certificates", rows);

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
        // Get detailed certificate counts by country and type
        std::ostringstream query;
        query << "SELECT "
              << "c.country_code, "
              << "SUM(CASE WHEN c.certificate_type = 'MLSC' THEN 1 ELSE 0 END) as mlsc_count, "
              << "SUM(CASE WHEN c.certificate_type = 'CSCA' AND c.subject_dn = c.issuer_dn THEN 1 ELSE 0 END) as csca_self_signed_count, "
              << "SUM(CASE WHEN c.certificate_type = 'CSCA' AND c.subject_dn != c.issuer_dn THEN 1 ELSE 0 END) as csca_link_cert_count, "
              << "SUM(CASE WHEN c.certificate_type = 'DSC' THEN 1 ELSE 0 END) as dsc_count, "
              << "SUM(CASE WHEN c.certificate_type = 'DSC_NC' THEN 1 ELSE 0 END) as dsc_nc_count, "
              << "COALESCE((SELECT COUNT(*) FROM crl WHERE country_code = c.country_code), 0) as crl_count, "
              << "COUNT(*) as total_certificates "
              << "FROM certificate c "
              << "WHERE c.country_code IS NOT NULL AND c.country_code != '' "
              << "GROUP BY c.country_code "
              << "ORDER BY total_certificates DESC ";

        if (limit > 0) {
            query << "LIMIT " << limit;
        }

        PGresult* res = executeQuery(query.str());
        int rows = PQntuples(res);

        Json::Value countries = Json::arrayValue;
        for (int i = 0; i < rows; i++) {
            Json::Value countryData;
            countryData["country"] = PQgetvalue(res, i, 0);
            countryData["mlscCount"] = std::atoi(PQgetvalue(res, i, 1));
            countryData["cscaSelfSignedCount"] = std::atoi(PQgetvalue(res, i, 2));
            countryData["cscaLinkCertCount"] = std::atoi(PQgetvalue(res, i, 3));
            countryData["dscCount"] = std::atoi(PQgetvalue(res, i, 4));
            countryData["dscNcCount"] = std::atoi(PQgetvalue(res, i, 5));
            countryData["crlCount"] = std::atoi(PQgetvalue(res, i, 6));
            countryData["totalCertificates"] = std::atoi(PQgetvalue(res, i, 7));
            countries.append(countryData);
        }

        PQclear(res);

        response["countries"] = countries;
        response["totalCountries"] = rows;

        spdlog::debug("[UploadRepository] Found detailed statistics for {} countries", rows);

    } catch (const std::exception& e) {
        spdlog::error("[UploadRepository] Get detailed country statistics failed: {}", e.what());
        response["error"] = e.what();
    }

    return response;
}

Json::Value UploadRepository::findDuplicatesByUploadId(const std::string& uploadId)
{
    spdlog::debug("[UploadRepository] Finding duplicates for upload: {}", uploadId);

    Json::Value result;
    result["success"] = false;
    result["uploadId"] = uploadId;

    try {
        // Query duplicate certificates for this upload
        // CRITICAL: Only return certificates that already existed before this upload
        // (exclude first appearance, only show true duplicates by fingerprint)
        std::string query =
            "SELECT "
            "  cd.id, "
            "  cd.source_type, "
            "  cd.source_country, "
            "  cd.detected_at, "
            "  c.certificate_type, "
            "  c.country_code, "
            "  c.subject_dn, "
            "  c.fingerprint_sha256 "
            "FROM certificate_duplicates cd "
            "JOIN certificate c ON cd.certificate_id = c.id "
            "WHERE cd.upload_id = $1 "
            "  AND c.first_upload_id != $2 "
            "ORDER BY cd.detected_at DESC";

        std::vector<std::string> params = {uploadId, uploadId};
        PGresult* res = executeParamQuery(query, params);

        result["success"] = true;
        Json::Value duplicates(Json::arrayValue);

        // Count by type
        std::map<std::string, int> byType;
        byType["CSCA"] = 0;
        byType["DSC"] = 0;
        byType["DSC_NC"] = 0;
        byType["MLSC"] = 0;
        byType["CRL"] = 0;

        int rows = PQntuples(res);
        for (int i = 0; i < rows; i++) {
            Json::Value dup;
            dup["id"] = std::atoi(PQgetvalue(res, i, 0));
            dup["sourceType"] = PQgetvalue(res, i, 1);
            dup["sourceCountry"] = PQgetvalue(res, i, 2) ? PQgetvalue(res, i, 2) : "";
            dup["detectedAt"] = PQgetvalue(res, i, 3);

            std::string certType = PQgetvalue(res, i, 4);
            dup["certificateType"] = certType;
            dup["country"] = PQgetvalue(res, i, 5);
            dup["subjectDn"] = PQgetvalue(res, i, 6);
            dup["fingerprint"] = PQgetvalue(res, i, 7);

            duplicates.append(dup);

            // Count by type
            if (byType.find(certType) != byType.end()) {
                byType[certType]++;
            }
        }

        result["duplicates"] = duplicates;
        result["totalDuplicates"] = rows;

        // Add type breakdown
        Json::Value byTypeJson;
        byTypeJson["CSCA"] = byType["CSCA"];
        byTypeJson["DSC"] = byType["DSC"];
        byTypeJson["DSC_NC"] = byType["DSC_NC"];
        byTypeJson["MLSC"] = byType["MLSC"];
        byTypeJson["CRL"] = byType["CRL"];
        result["byType"] = byTypeJson;

        PQclear(res);

        spdlog::debug("[UploadRepository] Found {} duplicates for upload {}", rows, uploadId);

    } catch (const std::exception& e) {
        spdlog::error("[UploadRepository] Find duplicates failed: {}", e.what());
        result["error"] = e.what();
        result["duplicates"] = Json::Value(Json::arrayValue);
        result["totalDuplicates"] = 0;
    }

    return result;
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
    upload.fileHash = PQgetvalue(res, row, PQfnumber(res, "file_hash"));
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

    // Validation statistics (using column indices after upload_timestamp and completed_timestamp)
    upload.validationValidCount = std::atoi(PQgetvalue(res, row, 18));  // COALESCE(validation_valid_count, 0)
    upload.validationInvalidCount = std::atoi(PQgetvalue(res, row, 19));  // COALESCE(validation_invalid_count, 0)
    upload.validationPendingCount = std::atoi(PQgetvalue(res, row, 20));  // COALESCE(validation_pending_count, 0)
    upload.validationErrorCount = std::atoi(PQgetvalue(res, row, 21));  // COALESCE(validation_error_count, 0)
    upload.trustChainValidCount = std::atoi(PQgetvalue(res, row, 22));  // COALESCE(trust_chain_valid_count, 0)
    upload.trustChainInvalidCount = std::atoi(PQgetvalue(res, row, 23));  // COALESCE(trust_chain_invalid_count, 0)
    upload.cscaNotFoundCount = std::atoi(PQgetvalue(res, row, 24));  // COALESCE(csca_not_found_count, 0)
    upload.expiredCount = std::atoi(PQgetvalue(res, row, 25));  // COALESCE(expired_count, 0)
    upload.revokedCount = std::atoi(PQgetvalue(res, row, 26));  // COALESCE(revoked_count, 0)

    upload.createdAt = PQgetvalue(res, row, 16);  // upload_timestamp
    upload.updatedAt = PQgetvalue(res, row, 17);  // completed_timestamp (may be NULL)

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
