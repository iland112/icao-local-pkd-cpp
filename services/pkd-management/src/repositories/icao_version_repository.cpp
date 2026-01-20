#include "icao_version_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>

namespace repositories {

IcaoVersionRepository::IcaoVersionRepository(const std::string& connInfo)
    : connInfo_(connInfo) {}

IcaoVersionRepository::~IcaoVersionRepository() {}

bool IcaoVersionRepository::insert(const domain::models::IcaoVersion& version) {
    PGconn* conn = PQconnectdb(connInfo_.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::error("[IcaoVersionRepository] DB connection failed: {}",
                     PQerrorMessage(conn));
        PQfinish(conn);
        return false;
    }

    // Use parameterized query to prevent SQL injection
    const char* query =
        "INSERT INTO icao_pkd_versions "
        "(collection_type, file_name, file_version, status, detected_at) "
        "VALUES ($1, $2, $3, $4, $5) "
        "ON CONFLICT (file_name) DO NOTHING "
        "RETURNING id";

    const char* paramValues[5] = {
        version.collectionType.c_str(),
        version.fileName.c_str(),
        std::to_string(version.fileVersion).c_str(),
        version.status.c_str(),
        version.detectedAt.c_str()
    };

    PGresult* res = PQexecParams(conn, query, 5, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    bool success = false;
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        success = true;
        spdlog::info("[IcaoVersionRepository] Inserted new version: {} (v{})",
                    version.fileName, version.fileVersion);
    } else if (PQresultStatus(res) == PGRES_COMMAND_OK) {
        // ON CONFLICT DO NOTHING triggered - version already exists
        spdlog::debug("[IcaoVersionRepository] Version already exists: {}",
                     version.fileName);
        success = false;
    } else {
        spdlog::error("[IcaoVersionRepository] Insert failed: {}",
                     PQerrorMessage(conn));
        success = false;
    }

    PQclear(res);
    PQfinish(conn);
    return success;
}

bool IcaoVersionRepository::updateStatus(const std::string& fileName,
                                        const std::string& newStatus) {
    PGconn* conn = PQconnectdb(connInfo_.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::error("[IcaoVersionRepository] DB connection failed");
        PQfinish(conn);
        return false;
    }

    const char* query =
        "UPDATE icao_pkd_versions "
        "SET status = $1, "
        "    downloaded_at = CASE WHEN $1 = 'DOWNLOADED' THEN CURRENT_TIMESTAMP ELSE downloaded_at END, "
        "    imported_at = CASE WHEN $1 = 'IMPORTED' THEN CURRENT_TIMESTAMP ELSE imported_at END "
        "WHERE file_name = $2";

    const char* paramValues[2] = {
        newStatus.c_str(),
        fileName.c_str()
    };

    PGresult* res = PQexecParams(conn, query, 2, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    bool success = (PQresultStatus(res) == PGRES_COMMAND_OK);

    if (success) {
        spdlog::info("[IcaoVersionRepository] Updated status: {} → {}",
                    fileName, newStatus);
    } else {
        spdlog::error("[IcaoVersionRepository] Update failed: {}",
                     PQerrorMessage(conn));
    }

    PQclear(res);
    PQfinish(conn);
    return success;
}

bool IcaoVersionRepository::markNotificationSent(const std::string& fileName) {
    PGconn* conn = PQconnectdb(connInfo_.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::error("[IcaoVersionRepository] DB connection failed");
        PQfinish(conn);
        return false;
    }

    const char* query =
        "UPDATE icao_pkd_versions "
        "SET notification_sent = TRUE, "
        "    notification_sent_at = CURRENT_TIMESTAMP, "
        "    status = 'NOTIFIED' "
        "WHERE file_name = $1";

    const char* paramValues[1] = { fileName.c_str() };

    PGresult* res = PQexecParams(conn, query, 1, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    bool success = (PQresultStatus(res) == PGRES_COMMAND_OK);

    if (success) {
        spdlog::info("[IcaoVersionRepository] Marked notification sent: {}", fileName);
    }

    PQclear(res);
    PQfinish(conn);
    return success;
}

bool IcaoVersionRepository::linkToUpload(const std::string& fileName,
                                        const std::string& uploadId,
                                        int certificateCount) {
    PGconn* conn = PQconnectdb(connInfo_.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::error("[IcaoVersionRepository] DB connection failed");
        PQfinish(conn);
        return false;
    }

    const char* query =
        "UPDATE icao_pkd_versions "
        "SET import_upload_id = $1, "
        "    certificate_count = $2, "
        "    status = 'IMPORTED', "
        "    imported_at = CURRENT_TIMESTAMP "
        "WHERE file_name = $3";

    const char* paramValues[3] = {
        uploadId.c_str(),  // UUID as string
        std::to_string(certificateCount).c_str(),
        fileName.c_str()
    };

    PGresult* res = PQexecParams(conn, query, 3, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    bool success = (PQresultStatus(res) == PGRES_COMMAND_OK);

    if (success) {
        spdlog::info("[IcaoVersionRepository] Linked to upload: {} → upload_id={}",
                    fileName, uploadId);
    }

    PQclear(res);
    PQfinish(conn);
    return success;
}

bool IcaoVersionRepository::exists(const std::string& collectionType,
                                  int fileVersion) {
    PGconn* conn = PQconnectdb(connInfo_.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::error("[IcaoVersionRepository] DB connection failed");
        PQfinish(conn);
        return false;
    }

    const char* query =
        "SELECT COUNT(*) FROM icao_pkd_versions "
        "WHERE collection_type = $1 AND file_version = $2";

    const char* paramValues[2] = {
        collectionType.c_str(),
        std::to_string(fileVersion).c_str()
    };

    PGresult* res = PQexecParams(conn, query, 2, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    bool exists = false;
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        int count = std::stoi(PQgetvalue(res, 0, 0));
        exists = (count > 0);
    }

    PQclear(res);
    PQfinish(conn);
    return exists;
}

std::optional<domain::models::IcaoVersion> IcaoVersionRepository::getByFileName(
    const std::string& fileName) {

    PGconn* conn = PQconnectdb(connInfo_.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::error("[IcaoVersionRepository] DB connection failed");
        PQfinish(conn);
        return std::nullopt;
    }

    const char* query =
        "SELECT id, collection_type, file_name, file_version, "
        "       detected_at, downloaded_at, imported_at, status, "
        "       notification_sent, notification_sent_at, "
        "       import_upload_id, certificate_count, error_message "
        "FROM icao_pkd_versions "
        "WHERE file_name = $1";

    const char* paramValues[1] = { fileName.c_str() };

    PGresult* res = PQexecParams(conn, query, 1, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    std::optional<domain::models::IcaoVersion> result = std::nullopt;

    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        result = resultToVersion(res, 0);
    }

    PQclear(res);
    PQfinish(conn);
    return result;
}

std::vector<domain::models::IcaoVersion> IcaoVersionRepository::getLatest() {
    PGconn* conn = PQconnectdb(connInfo_.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::error("[IcaoVersionRepository] DB connection failed");
        PQfinish(conn);
        return {};
    }

    const char* query =
        "SELECT DISTINCT ON (collection_type) "
        "       id, collection_type, file_name, file_version, "
        "       detected_at, downloaded_at, imported_at, status, "
        "       notification_sent, notification_sent_at, "
        "       import_upload_id, certificate_count, error_message "
        "FROM icao_pkd_versions "
        "ORDER BY collection_type, file_version DESC";

    PGresult* res = PQexec(conn, query);

    std::vector<domain::models::IcaoVersion> versions;

    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        int rows = PQntuples(res);
        for (int i = 0; i < rows; i++) {
            versions.push_back(resultToVersion(res, i));
        }
    }

    PQclear(res);
    PQfinish(conn);
    return versions;
}

std::vector<domain::models::IcaoVersion> IcaoVersionRepository::getHistory(int limit) {
    PGconn* conn = PQconnectdb(connInfo_.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::error("[IcaoVersionRepository] DB connection failed");
        PQfinish(conn);
        return {};
    }

    const char* query =
        "SELECT id, collection_type, file_name, file_version, "
        "       detected_at, downloaded_at, imported_at, status, "
        "       notification_sent, notification_sent_at, "
        "       import_upload_id, certificate_count, error_message "
        "FROM icao_pkd_versions "
        "ORDER BY detected_at DESC "
        "LIMIT $1";

    const char* paramValues[1] = { std::to_string(limit).c_str() };

    PGresult* res = PQexecParams(conn, query, 1, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    std::vector<domain::models::IcaoVersion> versions;

    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        int rows = PQntuples(res);
        for (int i = 0; i < rows; i++) {
            versions.push_back(resultToVersion(res, i));
        }
    }

    PQclear(res);
    PQfinish(conn);
    return versions;
}

std::vector<domain::models::IcaoVersion> IcaoVersionRepository::getAllVersions() {
    PGconn* conn = PQconnectdb(connInfo_.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::error("[IcaoVersionRepository] DB connection failed");
        PQfinish(conn);
        return {};
    }

    const char* query =
        "SELECT id, collection_type, file_name, file_version, "
        "       detected_at, downloaded_at, imported_at, status, "
        "       notification_sent, notification_sent_at, "
        "       import_upload_id, certificate_count, error_message "
        "FROM icao_pkd_versions "
        "ORDER BY collection_type, file_version DESC";

    PGresult* res = PQexec(conn, query);

    std::vector<domain::models::IcaoVersion> versions;

    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        int rows = PQntuples(res);
        for (int i = 0; i < rows; i++) {
            versions.push_back(resultToVersion(res, i));
        }
    }

    PQclear(res);
    PQfinish(conn);
    return versions;
}

// Private helper methods

domain::models::IcaoVersion IcaoVersionRepository::resultToVersion(PGresult* res, int row) {
    domain::models::IcaoVersion version;

    version.id = std::stoi(PQgetvalue(res, row, 0));
    version.collectionType = PQgetvalue(res, row, 1);
    version.fileName = PQgetvalue(res, row, 2);
    version.fileVersion = std::stoi(PQgetvalue(res, row, 3));
    version.detectedAt = PQgetvalue(res, row, 4);
    version.downloadedAt = getOptionalString(res, row, 5);
    version.importedAt = getOptionalString(res, row, 6);
    version.status = PQgetvalue(res, row, 7);
    version.notificationSent = (std::string(PQgetvalue(res, row, 8)) == "t");
    version.notificationSentAt = getOptionalString(res, row, 9);
    version.importUploadId = getOptionalString(res, row, 10);  // UUID as string
    version.certificateCount = getOptionalInt(res, row, 11);
    version.errorMessage = getOptionalString(res, row, 12);

    return version;
}

std::optional<std::string> IcaoVersionRepository::getOptionalString(PGresult* res, int row, int col) {
    if (PQgetisnull(res, row, col)) {
        return std::nullopt;
    }
    return std::string(PQgetvalue(res, row, col));
}

std::optional<int> IcaoVersionRepository::getOptionalInt(PGresult* res, int row, int col) {
    if (PQgetisnull(res, row, col)) {
        return std::nullopt;
    }
    return std::stoi(PQgetvalue(res, row, col));
}

std::vector<std::tuple<std::string, int, int, std::string>>
IcaoVersionRepository::getVersionComparison() {
    PGconn* conn = PQconnectdb(connInfo_.c_str());
    if (PQstatus(conn) != CONNECTION_OK) {
        spdlog::error("[IcaoVersionRepository] DB connection failed");
        PQfinish(conn);
        return {};
    }

    // Query to join icao_pkd_versions with uploaded_file to compare versions
    const char* query =
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

    PGresult* res = PQexec(conn, query);

    std::vector<std::tuple<std::string, int, int, std::string>> comparisons;

    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        int rows = PQntuples(res);
        spdlog::info("[IcaoVersionRepository] Version comparison returned {} rows", rows);

        for (int i = 0; i < rows; i++) {
            std::string collectionType = PQgetvalue(res, i, 0);
            int detectedVersion = std::stoi(PQgetvalue(res, i, 1));
            int uploadedVersion = std::stoi(PQgetvalue(res, i, 2));
            std::string uploadTimestamp = PQgetvalue(res, i, 3);

            comparisons.push_back(std::make_tuple(
                collectionType,
                detectedVersion,
                uploadedVersion,
                uploadTimestamp
            ));

            spdlog::debug("[IcaoVersionRepository] {}: detected={}, uploaded={}, timestamp={}",
                         collectionType, detectedVersion, uploadedVersion, uploadTimestamp);
        }
    } else {
        spdlog::error("[IcaoVersionRepository] Version comparison query failed: {}",
                     PQerrorMessage(conn));
    }

    PQclear(res);
    PQfinish(conn);
    return comparisons;
}

} // namespace repositories
