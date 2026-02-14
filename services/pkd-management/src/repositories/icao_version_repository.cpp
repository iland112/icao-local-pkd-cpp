/** @file icao_version_repository.cpp
 *  @brief IcaoVersionRepository implementation
 */

#include "icao_version_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace repositories {

IcaoVersionRepository::IcaoVersionRepository(common::IQueryExecutor* executor)
    : executor_(executor)
{
    if (!executor_) {
        throw std::invalid_argument("IcaoVersionRepository: executor cannot be nullptr");
    }
    spdlog::debug("[IcaoVersionRepository] Initialized (DB type: {})", executor_->getDatabaseType());
}

IcaoVersionRepository::~IcaoVersionRepository() {}

bool IcaoVersionRepository::insert(const domain::models::IcaoVersion& version) {
    try {
        std::string dbType = executor_->getDatabaseType();

        // Oracle doesn't support ON CONFLICT - check existence first, then INSERT
        if (exists(version.collectionType, version.fileVersion)) {
            spdlog::debug("[IcaoVersionRepository] Version already exists: {} (v{})",
                         version.fileName, version.fileVersion);
            return false;
        }

        const char* query =
            "INSERT INTO icao_pkd_versions "
            "(collection_type, file_name, file_version, status, detected_at) "
            "VALUES ($1, $2, $3, $4, $5)";

        std::vector<std::string> params = {
            version.collectionType,
            version.fileName,
            std::to_string(version.fileVersion),
            version.status,
            version.detectedAt.empty() ? "CURRENT_TIMESTAMP" : version.detectedAt
        };

        int rowsAffected = executor_->executeCommand(query, params);

        if (rowsAffected > 0) {
            spdlog::info("[IcaoVersionRepository] Inserted new version: {} (v{})",
                        version.fileName, version.fileVersion);
            return true;
        }

        return false;

    } catch (const std::exception& e) {
        spdlog::error("[IcaoVersionRepository] Insert failed: {}", e.what());
        return false;
    }
}

bool IcaoVersionRepository::updateStatus(const std::string& fileName,
                                        const std::string& newStatus) {
    try {
        const char* query =
            "UPDATE icao_pkd_versions "
            "SET status = $1, "
            "    downloaded_at = CASE WHEN $1 = 'DOWNLOADED' THEN CURRENT_TIMESTAMP ELSE downloaded_at END, "
            "    imported_at = CASE WHEN $1 = 'IMPORTED' THEN CURRENT_TIMESTAMP ELSE imported_at END "
            "WHERE file_name = $2";

        std::vector<std::string> params = { newStatus, fileName };

        int rowsAffected = executor_->executeCommand(query, params);

        if (rowsAffected > 0) {
            spdlog::info("[IcaoVersionRepository] Updated status: {} -> {}",
                        fileName, newStatus);
            return true;
        } else {
            spdlog::warn("[IcaoVersionRepository] No rows updated for: {}", fileName);
            return false;
        }

    } catch (const std::exception& e) {
        spdlog::error("[IcaoVersionRepository] Update status failed: {}", e.what());
        return false;
    }
}

bool IcaoVersionRepository::markNotificationSent(const std::string& fileName) {
    try {
        std::string dbType = executor_->getDatabaseType();

        // Oracle uses NUMBER(1) for booleans, PostgreSQL uses BOOLEAN
        std::string trueVal = (dbType == "oracle") ? "1" : "TRUE";

        std::string query =
            "UPDATE icao_pkd_versions "
            "SET notification_sent = " + trueVal + ", "
            "    notification_sent_at = CURRENT_TIMESTAMP, "
            "    status = 'NOTIFIED' "
            "WHERE file_name = $1";

        std::vector<std::string> params = { fileName };

        int rowsAffected = executor_->executeCommand(query, params);

        if (rowsAffected > 0) {
            spdlog::info("[IcaoVersionRepository] Marked notification sent: {}", fileName);
            return true;
        }

        return false;

    } catch (const std::exception& e) {
        spdlog::error("[IcaoVersionRepository] Mark notification failed: {}", e.what());
        return false;
    }
}

bool IcaoVersionRepository::linkToUpload(const std::string& fileName,
                                        const std::string& uploadId,
                                        int certificateCount) {
    try {
        const char* query =
            "UPDATE icao_pkd_versions "
            "SET import_upload_id = $1, "
            "    certificate_count = $2, "
            "    status = 'IMPORTED', "
            "    imported_at = CURRENT_TIMESTAMP "
            "WHERE file_name = $3";

        std::vector<std::string> params = {
            uploadId,
            std::to_string(certificateCount),
            fileName
        };

        int rowsAffected = executor_->executeCommand(query, params);

        if (rowsAffected > 0) {
            spdlog::info("[IcaoVersionRepository] Linked to upload: {} -> upload_id={}",
                        fileName, uploadId);
            return true;
        }

        return false;

    } catch (const std::exception& e) {
        spdlog::error("[IcaoVersionRepository] Link to upload failed: {}", e.what());
        return false;
    }
}

bool IcaoVersionRepository::exists(const std::string& collectionType,
                                  int fileVersion) {
    try {
        const char* query =
            "SELECT COUNT(*) FROM icao_pkd_versions "
            "WHERE collection_type = $1 AND file_version = $2";

        std::vector<std::string> params = {
            collectionType,
            std::to_string(fileVersion)
        };

        Json::Value result = executor_->executeScalar(query, params);
        int count = getInt(result, 0);
        return (count > 0);

    } catch (const std::exception& e) {
        spdlog::error("[IcaoVersionRepository] Exists check failed: {}", e.what());
        return false;
    }
}

std::optional<domain::models::IcaoVersion> IcaoVersionRepository::getByFileName(
    const std::string& fileName) {

    try {
        const char* query =
            "SELECT id, collection_type, file_name, file_version, "
            "       detected_at, downloaded_at, imported_at, status, "
            "       notification_sent, notification_sent_at, "
            "       import_upload_id, certificate_count, error_message "
            "FROM icao_pkd_versions "
            "WHERE file_name = $1";

        std::vector<std::string> params = { fileName };

        Json::Value result = executor_->executeQuery(query, params);

        if (result.isArray() && result.size() > 0) {
            return jsonToVersion(result[0]);
        }

        return std::nullopt;

    } catch (const std::exception& e) {
        spdlog::error("[IcaoVersionRepository] getByFileName failed: {}", e.what());
        return std::nullopt;
    }
}

std::vector<domain::models::IcaoVersion> IcaoVersionRepository::getLatest() {
    try {
        std::string dbType = executor_->getDatabaseType();
        std::string query;

        if (dbType == "oracle") {
            // Oracle doesn't support DISTINCT ON - use ROW_NUMBER() instead
            query =
                "SELECT id, collection_type, file_name, file_version, "
                "       detected_at, downloaded_at, imported_at, status, "
                "       notification_sent, notification_sent_at, "
                "       import_upload_id, certificate_count, error_message "
                "FROM ( "
                "  SELECT id, collection_type, file_name, file_version, "
                "         detected_at, downloaded_at, imported_at, status, "
                "         notification_sent, notification_sent_at, "
                "         import_upload_id, certificate_count, error_message, "
                "         ROW_NUMBER() OVER (PARTITION BY collection_type ORDER BY file_version DESC) as rn "
                "  FROM icao_pkd_versions "
                ") WHERE rn = 1 "
                "ORDER BY collection_type";
        } else {
            query =
                "SELECT DISTINCT ON (collection_type) "
                "       id, collection_type, file_name, file_version, "
                "       detected_at, downloaded_at, imported_at, status, "
                "       notification_sent, notification_sent_at, "
                "       import_upload_id, certificate_count, error_message "
                "FROM icao_pkd_versions "
                "ORDER BY collection_type, file_version DESC";
        }

        Json::Value result = executor_->executeQuery(query);

        std::vector<domain::models::IcaoVersion> versions;
        if (result.isArray()) {
            for (const auto& row : result) {
                versions.push_back(jsonToVersion(row));
            }
        }

        return versions;

    } catch (const std::exception& e) {
        spdlog::error("[IcaoVersionRepository] getLatest failed: {}", e.what());
        return {};
    }
}

std::vector<domain::models::IcaoVersion> IcaoVersionRepository::getHistory(int limit) {
    try {
        std::string dbType = executor_->getDatabaseType();

        std::string query =
            "SELECT id, collection_type, file_name, file_version, "
            "       detected_at, downloaded_at, imported_at, status, "
            "       notification_sent, notification_sent_at, "
            "       import_upload_id, certificate_count, error_message "
            "FROM icao_pkd_versions "
            "ORDER BY detected_at DESC ";

        if (dbType == "oracle") {
            query += "FETCH FIRST $1 ROWS ONLY";
        } else {
            query += "LIMIT $1";
        }

        std::vector<std::string> params = { std::to_string(limit) };

        Json::Value result = executor_->executeQuery(query, params);

        std::vector<domain::models::IcaoVersion> versions;
        if (result.isArray()) {
            for (const auto& row : result) {
                versions.push_back(jsonToVersion(row));
            }
        }

        return versions;

    } catch (const std::exception& e) {
        spdlog::error("[IcaoVersionRepository] getHistory failed: {}", e.what());
        return {};
    }
}

std::vector<domain::models::IcaoVersion> IcaoVersionRepository::getAllVersions() {
    try {
        const char* query =
            "SELECT id, collection_type, file_name, file_version, "
            "       detected_at, downloaded_at, imported_at, status, "
            "       notification_sent, notification_sent_at, "
            "       import_upload_id, certificate_count, error_message "
            "FROM icao_pkd_versions "
            "ORDER BY collection_type, file_version DESC";

        Json::Value result = executor_->executeQuery(query);

        std::vector<domain::models::IcaoVersion> versions;
        if (result.isArray()) {
            for (const auto& row : result) {
                versions.push_back(jsonToVersion(row));
            }
        }

        return versions;

    } catch (const std::exception& e) {
        spdlog::error("[IcaoVersionRepository] getAllVersions failed: {}", e.what());
        return {};
    }
}

// --- Private Helper Methods ---

domain::models::IcaoVersion IcaoVersionRepository::jsonToVersion(const Json::Value& row) {
    domain::models::IcaoVersion version;

    version.id = row.get("id", "").asString();
    version.collectionType = row.get("collection_type", "").asString();
    version.fileName = row.get("file_name", "").asString();
    version.fileVersion = getInt(row.get("file_version", 0));
    version.detectedAt = row.get("detected_at", "").asString();
    version.downloadedAt = getOptionalString(row.get("downloaded_at", Json::nullValue));
    version.importedAt = getOptionalString(row.get("imported_at", Json::nullValue));
    version.status = row.get("status", "").asString();

    // Handle boolean for notification_sent (Oracle returns "1"/"0", PostgreSQL returns "t"/"f" or bool)
    Json::Value notifSent = row.get("notification_sent", false);
    if (notifSent.isBool()) {
        version.notificationSent = notifSent.asBool();
    } else if (notifSent.isString()) {
        std::string s = notifSent.asString();
        version.notificationSent = (s == "t" || s == "true" || s == "1");
    } else if (notifSent.isInt()) {
        version.notificationSent = (notifSent.asInt() != 0);
    } else {
        version.notificationSent = false;
    }

    version.notificationSentAt = getOptionalString(row.get("notification_sent_at", Json::nullValue));
    version.importUploadId = getOptionalString(row.get("import_upload_id", Json::nullValue));
    version.certificateCount = getOptionalInt(row.get("certificate_count", Json::nullValue));
    version.errorMessage = getOptionalString(row.get("error_message", Json::nullValue));

    return version;
}

std::optional<std::string> IcaoVersionRepository::getOptionalString(const Json::Value& val) {
    if (val.isNull() || (val.isString() && val.asString().empty())) {
        return std::nullopt;
    }
    return val.asString();
}

std::optional<int> IcaoVersionRepository::getOptionalInt(const Json::Value& val) {
    if (val.isNull()) {
        return std::nullopt;
    }
    return getInt(val, 0);
}

int IcaoVersionRepository::getInt(const Json::Value& val, int defaultVal) {
    if (val.isNull()) return defaultVal;
    if (val.isInt()) return val.asInt();
    if (val.isUInt()) return static_cast<int>(val.asUInt());
    if (val.isString()) {
        try { return std::stoi(val.asString()); } catch (...) { return defaultVal; }
    }
    if (val.isDouble()) return static_cast<int>(val.asDouble());
    return defaultVal;
}

std::vector<std::tuple<std::string, int, int, std::string>>
IcaoVersionRepository::getVersionComparison() {
    try {
        std::string dbType = executor_->getDatabaseType();
        std::string query;

        if (dbType == "oracle") {
            // Oracle version: no DISTINCT ON, no regex ~, use REGEXP_SUBSTR and ROW_NUMBER
            query =
                "SELECT "
                "  v.collection_type, "
                "  v.file_version as detected_version, "
                "  CASE "
                "    WHEN u.original_file_name IS NOT NULL AND REGEXP_SUBSTR(u.original_file_name, 'icaopkd-00[123]-complete-(\\d+)', 1, 1, NULL, 1) IS NOT NULL THEN "
                "      TO_NUMBER(REGEXP_SUBSTR(u.original_file_name, 'icaopkd-00[123]-complete-(\\d+)', 1, 1, NULL, 1)) "
                "    ELSE 0 "
                "  END as uploaded_version, "
                "  COALESCE(TO_CHAR(u.upload_timestamp, 'YYYY-MM-DD HH24:MI:SS'), 'N/A') as upload_timestamp "
                "FROM ( "
                "  SELECT collection_type, file_version "
                "  FROM ( "
                "    SELECT collection_type, file_version, "
                "           ROW_NUMBER() OVER (PARTITION BY collection_type ORDER BY file_version DESC) as rn "
                "    FROM icao_pkd_versions "
                "  ) WHERE rn = 1 "
                ") v "
                "LEFT JOIN ( "
                "  SELECT "
                "    CASE "
                "      WHEN dsc_count > 0 OR crl_count > 0 THEN 'DSC_CRL' "
                "      WHEN dsc_nc_count > 0 THEN 'DSC_NC' "
                "      WHEN ml_count > 0 THEN 'MASTERLIST' "
                "    END as collection_type, "
                "    original_file_name, "
                "    upload_timestamp, "
                "    ROW_NUMBER() OVER (PARTITION BY "
                "      CASE "
                "        WHEN dsc_count > 0 OR crl_count > 0 THEN 'DSC_CRL' "
                "        WHEN dsc_nc_count > 0 THEN 'DSC_NC' "
                "        WHEN ml_count > 0 THEN 'MASTERLIST' "
                "      END "
                "      ORDER BY upload_timestamp DESC) as rn "
                "  FROM uploaded_file "
                "  WHERE status = 'COMPLETED' "
                ") u ON v.collection_type = u.collection_type AND u.rn = 1 "
                "ORDER BY v.collection_type";
        } else {
            // PostgreSQL version: DISTINCT ON and regex ~ supported
            query =
                "SELECT "
                "  v.collection_type, "
                "  v.file_version as detected_version, "
                "  CASE "
                "    WHEN u.original_file_name ~ 'icaopkd-00[123]-complete-(\\d+)' THEN "
                "      substring(u.original_file_name from 'icaopkd-00[123]-complete-(\\d+)')::int "
                "    ELSE 0 "
                "  END as uploaded_version, "
                "  COALESCE(to_char(u.upload_timestamp, 'YYYY-MM-DD HH24:MI:SS'), 'N/A') as upload_timestamp "
                "FROM ( "
                "  SELECT DISTINCT ON (collection_type) "
                "    collection_type, file_version "
                "  FROM icao_pkd_versions "
                "  ORDER BY collection_type, file_version DESC "
                ") v "
                "LEFT JOIN ( "
                "  SELECT "
                "    CASE "
                "      WHEN dsc_count > 0 OR crl_count > 0 THEN 'DSC_CRL' "
                "      WHEN dsc_nc_count > 0 THEN 'DSC_NC' "
                "      WHEN ml_count > 0 THEN 'MASTERLIST' "
                "    END as collection_type, "
                "    original_file_name, "
                "    upload_timestamp, "
                "    ROW_NUMBER() OVER (PARTITION BY "
                "      CASE "
                "        WHEN dsc_count > 0 OR crl_count > 0 THEN 'DSC_CRL' "
                "        WHEN dsc_nc_count > 0 THEN 'DSC_NC' "
                "        WHEN ml_count > 0 THEN 'MASTERLIST' "
                "      END "
                "      ORDER BY upload_timestamp DESC) as rn "
                "  FROM uploaded_file "
                "  WHERE status = 'COMPLETED' "
                ") u ON v.collection_type = u.collection_type AND u.rn = 1 "
                "ORDER BY v.collection_type";
        }

        Json::Value result = executor_->executeQuery(query);

        std::vector<std::tuple<std::string, int, int, std::string>> comparisons;

        if (result.isArray()) {
            spdlog::info("[IcaoVersionRepository] Version comparison returned {} rows", result.size());

            for (const auto& row : result) {
                std::string collectionType = row.get("collection_type", "").asString();
                int detectedVersion = getInt(row.get("detected_version", 0));
                int uploadedVersion = getInt(row.get("uploaded_version", 0));
                std::string uploadTimestamp = row.get("upload_timestamp", "N/A").asString();

                comparisons.push_back(std::make_tuple(
                    collectionType,
                    detectedVersion,
                    uploadedVersion,
                    uploadTimestamp
                ));

                spdlog::debug("[IcaoVersionRepository] {}: detected={}, uploaded={}, timestamp={}",
                             collectionType, detectedVersion, uploadedVersion, uploadTimestamp);
            }
        }

        return comparisons;

    } catch (const std::exception& e) {
        spdlog::error("[IcaoVersionRepository] Version comparison failed: {}", e.what());
        return {};
    }
}

} // namespace repositories
