#include "upload_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cstring>
#include <sstream>

namespace repositories {

// ============================================================================
// Constructor
// ============================================================================

UploadRepository::UploadRepository(common::IQueryExecutor* queryExecutor)
    : queryExecutor_(queryExecutor)
{
    if (!queryExecutor_) {
        throw std::invalid_argument("UploadRepository: queryExecutor cannot be nullptr");
    }
    spdlog::debug("[UploadRepository] Initialized (DB type: {})", queryExecutor_->getDatabaseType());
}

// ============================================================================
// CRUD Operations
// ============================================================================

bool UploadRepository::insert(const Upload& upload)
{
    spdlog::debug("[UploadRepository] Inserting upload: {}", upload.fileName);

    try {
        // Database-aware timestamp - PostgreSQL uses CURRENT_TIMESTAMP directly, Oracle uses TO_CHAR(SYSTIMESTAMP)
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string timestampValue = (dbType == "oracle")
            ? "TO_CHAR(SYSTIMESTAMP, 'YYYY-MM-DD HH24:MI:SS')"
            : "CURRENT_TIMESTAMP";

        std::string query =
            "INSERT INTO uploaded_file "
            "(id, file_name, file_hash, file_format, file_size, status, uploaded_by, upload_timestamp) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, " + timestampValue + ")";

        std::vector<std::string> params = {
            upload.id,
            upload.fileName,
            upload.fileHash,
            upload.fileFormat,
            std::to_string(upload.fileSize),
            upload.status,
            upload.uploadedBy
        };

        queryExecutor_->executeCommand(query, params);

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
        // Oracle: upload_timestamp, completed_timestamp are now VARCHAR2 (no conversion needed)
        const char* query =
            "SELECT id, file_name, file_hash, file_format, file_size, status, uploaded_by, "
            "error_message, processing_mode, total_entries, processed_entries, "
            "csca_count, dsc_count, dsc_nc_count, crl_count, mlsc_count, ml_count, "
            "upload_timestamp, completed_timestamp, "
            "COALESCE(validation_valid_count, 0) AS validation_valid_count, "
            "COALESCE(validation_invalid_count, 0) AS validation_invalid_count, "
            "COALESCE(validation_pending_count, 0) AS validation_pending_count, "
            "COALESCE(validation_error_count, 0) AS validation_error_count, "
            "COALESCE(trust_chain_valid_count, 0) AS trust_chain_valid_count, "
            "COALESCE(trust_chain_invalid_count, 0) AS trust_chain_invalid_count, "
            "COALESCE(csca_not_found_count, 0) AS csca_not_found_count, "
            "COALESCE(expired_count, 0) AS expired_count, "
            "COALESCE(revoked_count, 0) AS revoked_count "
            "FROM uploaded_file WHERE id = $1";

        std::vector<std::string> params = {uploadId};
        Json::Value result = queryExecutor_->executeQuery(query, params);

        if (result.empty()) {
            spdlog::debug("[UploadRepository] Upload not found: {}", uploadId);
            return std::nullopt;
        }

        Upload upload = jsonToUpload(result[0]);
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
        // ORDER BY uses raw column names - Oracle handles TIMESTAMP sorting natively
        // TO_CHAR() is only for SELECT clause to convert results to string for OTL
        std::string dbSortBy = sortBy;
        if (sortBy == "createdAt" || sortBy == "created_at") {
            dbSortBy = "upload_timestamp";
        } else if (sortBy == "updatedAt" || sortBy == "updated_at") {
            dbSortBy = "completed_timestamp";
        }

        // Oracle: upload_timestamp, completed_timestamp are now VARCHAR2 (simplified query)
        std::ostringstream query;
        query << "SELECT id, file_name, file_hash, file_format, file_size, status, uploaded_by, "
              << "error_message, processing_mode, total_entries, processed_entries, "
              << "csca_count, dsc_count, dsc_nc_count, crl_count, mlsc_count, ml_count, "
              << "upload_timestamp, completed_timestamp, "
              << "COALESCE(validation_valid_count, 0) AS validation_valid_count, "
              << "COALESCE(validation_invalid_count, 0) AS validation_invalid_count, "
              << "COALESCE(validation_pending_count, 0) AS validation_pending_count, "
              << "COALESCE(validation_error_count, 0) AS validation_error_count, "
              << "COALESCE(trust_chain_valid_count, 0) AS trust_chain_valid_count, "
              << "COALESCE(trust_chain_invalid_count, 0) AS trust_chain_invalid_count, "
              << "COALESCE(csca_not_found_count, 0) AS csca_not_found_count, "
              << "COALESCE(expired_count, 0) AS expired_count, "
              << "COALESCE(revoked_count, 0) AS revoked_count "
              << "FROM uploaded_file "
              << "ORDER BY " << dbSortBy << " " << direction << " "
              << "OFFSET " << offset << " ROWS FETCH NEXT " << limit << " ROWS ONLY";

        Json::Value result = queryExecutor_->executeQuery(query.str());

        uploads.reserve(result.size());
        for (const auto& row : result) {
            uploads.push_back(jsonToUpload(row));
        }

        spdlog::debug("[UploadRepository] Found {} uploads", result.size());

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
        // Database-aware timestamp - PostgreSQL uses CURRENT_TIMESTAMP directly, Oracle uses TO_CHAR(SYSTIMESTAMP)
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string timestampValue = (dbType == "oracle")
            ? "TO_CHAR(SYSTIMESTAMP, 'YYYY-MM-DD HH24:MI:SS')"
            : "CURRENT_TIMESTAMP";

        std::string query;
        std::vector<std::string> params;

        if (errorMessage.empty()) {
            query = "UPDATE uploaded_file SET status = $1::VARCHAR, "
                   "completed_timestamp = CASE WHEN $1::VARCHAR IN ('COMPLETED', 'FAILED') THEN " + timestampValue + " ELSE completed_timestamp END "
                   "WHERE id = $2";
            params = {status, uploadId};
        } else {
            query = "UPDATE uploaded_file SET status = $1::VARCHAR, error_message = $2, "
                   "completed_timestamp = CASE WHEN $1::VARCHAR IN ('COMPLETED', 'FAILED') THEN " + timestampValue + " ELSE completed_timestamp END "
                   "WHERE id = $3";
            params = {status, errorMessage, uploadId};
        }

        queryExecutor_->executeCommand(query, params);

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

        queryExecutor_->executeCommand(query, params);

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

        queryExecutor_->executeCommand(query, params);

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

        queryExecutor_->executeCommand(query, params);

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
        // Oracle: upload_timestamp, completed_timestamp are now VARCHAR2 (simplified query)
        const char* query =
            "SELECT id, file_name, file_hash, file_format, file_size, status, uploaded_by, "
            "error_message, processing_mode, total_entries, processed_entries, "
            "csca_count, dsc_count, dsc_nc_count, crl_count, mlsc_count, ml_count, "
            "upload_timestamp, completed_timestamp, "
            "COALESCE(validation_valid_count, 0) AS validation_valid_count, "
            "COALESCE(validation_invalid_count, 0) AS validation_invalid_count, "
            "COALESCE(validation_pending_count, 0) AS validation_pending_count, "
            "COALESCE(validation_error_count, 0) AS validation_error_count, "
            "COALESCE(trust_chain_valid_count, 0) AS trust_chain_valid_count, "
            "COALESCE(trust_chain_invalid_count, 0) AS trust_chain_invalid_count, "
            "COALESCE(csca_not_found_count, 0) AS csca_not_found_count, "
            "COALESCE(expired_count, 0) AS expired_count, "
            "COALESCE(revoked_count, 0) AS revoked_count "
            "FROM uploaded_file WHERE file_hash = $1 "
            "FETCH FIRST 1 ROWS ONLY";

        std::vector<std::string> params = {fileHash};
        Json::Value result = queryExecutor_->executeQuery(query, params);

        if (result.empty()) {
            spdlog::debug("[UploadRepository] No duplicate found for hash: {}", fileHash.substr(0, 16) + "...");
            return std::nullopt;
        }

        Upload upload = jsonToUpload(result[0]);

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

        Json::Value result = queryExecutor_->executeScalar(query, params);
        return result.asInt();

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

        Json::Value result = queryExecutor_->executeScalar(query);
        return result.asInt();

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
        // Get total uploads
        Json::Value totalUploadsResult = queryExecutor_->executeScalar("SELECT COUNT(*) FROM uploaded_file");
        int totalUploads = totalUploadsResult.asInt();

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

        Json::Value certResult = queryExecutor_->executeQuery(certQuery);
        int totalCsca = certResult[0]["total_csca"].asInt();
        int totalDsc = certResult[0]["total_dsc"].asInt();
        int totalDscNc = certResult[0]["total_dsc_nc"].asInt();
        int totalMlsc = certResult[0]["total_mlsc"].asInt();
        int totalCrl = certResult[0]["total_crl"].asInt();
        int totalMl = certResult[0]["total_ml"].asInt();

        // Get uploads by status (for successfulUploads/failedUploads)
        const char* statusQuery =
            "SELECT status, COUNT(*) as count FROM uploaded_file GROUP BY status";
        Json::Value statusResult = queryExecutor_->executeQuery(statusQuery);

        int successfulUploads = 0;
        int failedUploads = 0;
        for (const auto& row : statusResult) {
            std::string status = row["status"].asString();
            int count = row["count"].asInt();
            if (status == "COMPLETED") {
                successfulUploads += count;
            } else if (status == "FAILED" || status == "ERROR") {
                failedUploads += count;
            }
        }

        // Get country count (distinct countries)
        Json::Value countryResult = queryExecutor_->executeScalar(
            "SELECT COUNT(DISTINCT country_code) FROM certificate "
            "WHERE country_code IS NOT NULL AND country_code != ''"
        );
        int countriesCount = countryResult.asInt();

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
            "COALESCE(SUM(CASE WHEN validity_period_valid = false THEN 1 ELSE 0 END), 0) as expired_count, "
            "COALESCE(SUM(CASE WHEN revocation_status = 'REVOKED' THEN 1 ELSE 0 END), 0) as revoked_count "
            "FROM validation_result";

        Json::Value validationResult = queryExecutor_->executeQuery(validationQuery);
        Json::Value validation;
        if (!validationResult.empty()) {
            validation["validCount"] = validationResult[0]["valid_count"].asInt();
            validation["invalidCount"] = validationResult[0]["invalid_count"].asInt();
            validation["pendingCount"] = validationResult[0]["pending_count"].asInt();
            validation["errorCount"] = validationResult[0]["error_count"].asInt();
            validation["trustChainValidCount"] = validationResult[0]["trust_chain_valid_count"].asInt();
            validation["trustChainInvalidCount"] = validationResult[0]["trust_chain_invalid_count"].asInt();
            validation["cscaNotFoundCount"] = validationResult[0]["csca_not_found_count"].asInt();
            validation["expiredCount"] = validationResult[0]["expired_count"].asInt();
            validation["revokedCount"] = validationResult[0]["revoked_count"].asInt();
        }

        // Get CSCA breakdown (self-signed vs link certificates)
        const char* cscaBreakdownQuery =
            "SELECT "
            "COALESCE(SUM(CASE WHEN is_self_signed = true THEN 1 ELSE 0 END), 0) as self_signed_count, "
            "COALESCE(SUM(CASE WHEN is_self_signed = false THEN 1 ELSE 0 END), 0) as link_cert_count "
            "FROM certificate WHERE certificate_type = 'CSCA'";

        Json::Value cscaResult = queryExecutor_->executeQuery(cscaBreakdownQuery);
        int selfSignedCount = 0;
        int linkCertCount = 0;
        if (!cscaResult.empty()) {
            selfSignedCount = cscaResult[0]["self_signed_count"].asInt();
            linkCertCount = cscaResult[0]["link_cert_count"].asInt();
        }

        // Build byType object with CSCA breakdown
        Json::Value byType;
        byType["csca"] = totalCsca;
        byType["cscaSelfSigned"] = selfSignedCount;
        byType["cscaLinkCert"] = linkCertCount;
        byType["mlsc"] = totalMlsc;
        byType["dsc"] = totalDsc;
        byType["dscNc"] = totalDscNc;
        byType["crl"] = totalCrl;

        // Build cscaBreakdown object matching frontend UploadStatisticsOverview interface
        Json::Value cscaBreakdown;
        cscaBreakdown["total"] = totalCsca;
        cscaBreakdown["selfSigned"] = selfSignedCount;
        cscaBreakdown["linkCertificates"] = linkCertCount;

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
        response["byType"] = byType;
        response["cscaBreakdown"] = cscaBreakdown;
        response["validation"] = validation;

        spdlog::debug("[UploadRepository] Statistics: {} uploads ({} successful, {} failed), {} certificates, {} countries",
            totalUploads, successfulUploads, failedUploads, response["totalCertificates"].asInt(), countriesCount);

    } catch (const std::exception& e) {
        spdlog::error("[UploadRepository] Get statistics summary failed: {}", e.what());
        response["error"] = e.what();
    }

    return response;
}

Json::Value UploadRepository::getCountryStatistics(int limit)
{
    spdlog::debug("[UploadRepository] Getting country statistics (limit: {})", limit);

    Json::Value response;

    try {
        // Get certificate counts by country and type
        std::ostringstream query;
        query << "SELECT "
              << "c.country_code, "
              << "SUM(CASE WHEN c.certificate_type = 'CSCA' THEN 1 ELSE 0 END) as csca_count, "
              << "SUM(CASE WHEN c.certificate_type = 'MLSC' THEN 1 ELSE 0 END) as mlsc_count, "
              << "SUM(CASE WHEN c.certificate_type = 'DSC' THEN 1 ELSE 0 END) as dsc_count, "
              << "SUM(CASE WHEN c.certificate_type = 'DSC_NC' THEN 1 ELSE 0 END) as dsc_nc_count, "
              << "COUNT(*) as total_certificates "
              << "FROM certificate c "
              << "WHERE c.country_code IS NOT NULL AND c.country_code != '' "
              << "GROUP BY c.country_code "
              << "ORDER BY total_certificates DESC ";

        if (limit > 0) {
            query << "LIMIT " << limit;
        }

        Json::Value result = queryExecutor_->executeQuery(query.str());

        Json::Value countries = Json::arrayValue;
        for (const auto& row : result) {
            Json::Value countryData;
            countryData["country"] = row["country_code"];
            countryData["csca"] = row["csca_count"];
            countryData["mlsc"] = row["mlsc_count"];
            countryData["dsc"] = row["dsc_count"];
            countryData["dscNc"] = row["dsc_nc_count"];
            countryData["total"] = row["total_certificates"];
            countries.append(countryData);
        }

        response["countries"] = countries;
        response["totalCountries"] = static_cast<int>(result.size());

        spdlog::debug("[UploadRepository] Found {} countries with certificates", result.size());

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

        Json::Value result = queryExecutor_->executeQuery(query.str());

        Json::Value countries = Json::arrayValue;
        for (const auto& row : result) {
            Json::Value countryData;
            countryData["countryCode"] = row["country_code"];
            countryData["mlsc"] = row["mlsc_count"];
            countryData["cscaSelfSigned"] = row["csca_self_signed_count"];
            countryData["cscaLinkCert"] = row["csca_link_cert_count"];
            countryData["dsc"] = row["dsc_count"];
            countryData["dscNc"] = row["dsc_nc_count"];
            countryData["crl"] = row["crl_count"];
            countryData["totalCerts"] = row["total_certificates"];
            countries.append(countryData);
        }

        response["countries"] = countries;
        response["totalCountries"] = static_cast<int>(result.size());

        spdlog::debug("[UploadRepository] Found detailed statistics for {} countries", result.size());

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
        // Returns ALL duplicates detected during upload processing
        // Includes both: duplicates within the same file and duplicates from previous uploads
        // Enhanced for tree view: includes first_upload info and all tracking details
        std::string query =
            "SELECT "
            "  cd.id, "
            "  cd.source_type, "
            "  cd.source_country, "
            "  cd.source_entry_dn, "
            "  cd.source_file_name, "
            "  cd.detected_at, "
            "  c.id as certificate_id, "
            "  c.certificate_type, "
            "  c.country_code, "
            "  c.subject_dn, "
            "  c.fingerprint_sha256, "
            "  c.first_upload_id, "
            "  uf.file_name as first_upload_file_name, "
            "  uf.upload_timestamp as first_upload_timestamp "
            "FROM certificate_duplicates cd "
            "JOIN certificate c ON cd.certificate_id = c.id "
            "LEFT JOIN uploaded_file uf ON c.first_upload_id = uf.id "
            "WHERE cd.upload_id = $1 "
            "ORDER BY c.fingerprint_sha256, cd.detected_at DESC";

        std::vector<std::string> params = {uploadId};
        Json::Value queryResult = queryExecutor_->executeQuery(query, params);

        result["success"] = true;
        Json::Value duplicates(Json::arrayValue);

        // Count by type
        std::map<std::string, int> byType;
        byType["CSCA"] = 0;
        byType["DSC"] = 0;
        byType["DSC_NC"] = 0;
        byType["MLSC"] = 0;
        byType["CRL"] = 0;

        for (const auto& row : queryResult) {
            Json::Value dup;
            dup["id"] = row["id"];
            dup["sourceType"] = row["source_type"];
            dup["sourceCountry"] = row.get("source_country", "");
            dup["sourceEntryDn"] = row.get("source_entry_dn", "");
            dup["sourceFileName"] = row.get("source_file_name", "");
            dup["detectedAt"] = row["detected_at"];

            // Certificate information
            dup["certificateId"] = row["certificate_id"];
            std::string certType = row["certificate_type"].asString();
            dup["certificateType"] = certType;
            dup["country"] = row["country_code"];
            dup["subjectDn"] = row["subject_dn"];
            dup["fingerprint"] = row["fingerprint_sha256"];

            // First upload information (for tree view root)
            dup["firstUploadId"] = row["first_upload_id"];
            dup["firstUploadFileName"] = row.get("first_upload_file_name", "");
            dup["firstUploadTimestamp"] = row.get("first_upload_timestamp", "");

            duplicates.append(dup);

            // Count by type
            if (byType.find(certType) != byType.end()) {
                byType[certType]++;
            }
        }

        result["duplicates"] = duplicates;
        result["totalDuplicates"] = static_cast<int>(queryResult.size());

        // Add type breakdown
        Json::Value byTypeJson;
        byTypeJson["CSCA"] = byType["CSCA"];
        byTypeJson["DSC"] = byType["DSC"];
        byTypeJson["DSC_NC"] = byType["DSC_NC"];
        byTypeJson["MLSC"] = byType["MLSC"];
        byTypeJson["CRL"] = byType["CRL"];
        result["byType"] = byTypeJson;

        spdlog::debug("[UploadRepository] Found {} duplicates for upload {}", queryResult.size(), uploadId);

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

Upload UploadRepository::jsonToUpload(const Json::Value& json)
{
    Upload upload;

    upload.id = json.get("id", "").asString();
    upload.fileName = json.get("file_name", "").asString();
    upload.fileHash = json.get("file_hash", "").asString();
    upload.fileFormat = json.get("file_format", "").asString();
    upload.fileSize = getInt(json, "file_size", 0);
    upload.status = json.get("status", "").asString();
    upload.uploadedBy = json.get("uploaded_by", "").asString();

    upload.errorMessage = getOptionalString(json, "error_message");
    upload.processingMode = getOptionalString(json, "processing_mode");

    upload.totalEntries = getInt(json, "total_entries", 0);
    upload.processedEntries = getInt(json, "processed_entries", 0);

    upload.cscaCount = getInt(json, "csca_count", 0);
    upload.dscCount = getInt(json, "dsc_count", 0);
    upload.dscNcCount = getInt(json, "dsc_nc_count", 0);
    upload.crlCount = getInt(json, "crl_count", 0);
    upload.mlscCount = getInt(json, "mlsc_count", 0);
    upload.mlCount = getInt(json, "ml_count", 0);

    // Timestamps
    upload.createdAt = json.get("upload_timestamp", "").asString();
    upload.updatedAt = json.get("completed_timestamp", "").asString();

    // Validation statistics
    upload.validationValidCount = getInt(json, "validation_valid_count", 0);
    upload.validationInvalidCount = getInt(json, "validation_invalid_count", 0);
    upload.validationPendingCount = getInt(json, "validation_pending_count", 0);
    upload.validationErrorCount = getInt(json, "validation_error_count", 0);
    upload.trustChainValidCount = getInt(json, "trust_chain_valid_count", 0);
    upload.trustChainInvalidCount = getInt(json, "trust_chain_invalid_count", 0);
    upload.cscaNotFoundCount = getInt(json, "csca_not_found_count", 0);
    upload.expiredCount = getInt(json, "expired_count", 0);
    upload.revokedCount = getInt(json, "revoked_count", 0);

    return upload;
}

std::optional<std::string> UploadRepository::getOptionalString(const Json::Value& json, const std::string& field)
{
    if (!json.isMember(field) || json[field].isNull()) {
        return std::nullopt;
    }
    return json[field].asString();
}

/**
 * @brief Comprehensive integer parsing helper (handles PostgreSQL and Oracle)
 *
 * PostgreSQL may return integers as int/uint types
 * Oracle may return integers as string types (e.g., "123")
 * This function handles all cases to prevent "Value is not convertible to Int" errors
 *
 * @param json JSON object containing the field
 * @param field Field name to parse
 * @param defaultValue Default value if field is missing/null
 * @return Parsed integer value
 */
int UploadRepository::getInt(const Json::Value& json, const std::string& field, int defaultValue)
{
    if (!json.isMember(field) || json[field].isNull()) {
        return defaultValue;
    }

    const Json::Value& value = json[field];

    // Handle different JSON types that Oracle/PostgreSQL might return
    if (value.isInt()) {
        return value.asInt();
    } else if (value.isUInt()) {
        return static_cast<int>(value.asUInt());
    } else if (value.isInt64()) {
        return static_cast<int>(value.asInt64());
    } else if (value.isUInt64()) {
        return static_cast<int>(value.asUInt64());
    } else if (value.isString()) {
        // Oracle returns integers as strings
        std::string str = value.asString();
        if (str.empty()) {
            return defaultValue;
        }
        try {
            return std::stoi(str);
        } catch (const std::exception& e) {
            spdlog::warn("[UploadRepository] Failed to parse integer field '{}' with value '{}': {}",
                         field, str, e.what());
            return defaultValue;
        }
    } else if (value.isDouble()) {
        // Handle numeric types
        return static_cast<int>(value.asDouble());
    }

    spdlog::warn("[UploadRepository] Unexpected type for integer field '{}': {}",
                 field, static_cast<int>(value.type()));
    return defaultValue;
}

std::optional<int> UploadRepository::getOptionalInt(const Json::Value& json, const std::string& field)
{
    if (!json.isMember(field) || json[field].isNull()) {
        return std::nullopt;
    }
    return json[field].asInt();
}

} // namespace repositories
