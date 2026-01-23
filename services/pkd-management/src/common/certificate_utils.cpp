#include "certificate_utils.h"
#include <spdlog/spdlog.h>
#include <cstring>

namespace certificate_utils {

std::pair<int, bool> saveCertificateWithDuplicateCheck(
    PGconn* conn,
    const std::string& uploadId,
    const std::string& certType,
    const std::string& countryCode,
    const std::string& subjectDn,
    const std::string& issuerDn,
    const std::string& serialNumber,
    const std::string& fingerprint,
    const std::string& notBefore,
    const std::string& notAfter,
    const std::vector<uint8_t>& certData,
    const std::string& validationStatus,
    const std::string& validationMessage
) {
    // Step 1: Check if certificate already exists
    const char* checkQuery =
        "SELECT id, first_upload_id FROM certificate "
        "WHERE certificate_type = $1 AND fingerprint_sha256 = $2";

    const char* checkParams[2] = {
        certType.c_str(),
        fingerprint.c_str()
    };

    PGresult* checkRes = PQexecParams(conn, checkQuery, 2, nullptr, checkParams,
                                      nullptr, nullptr, 0);

    if (PQresultStatus(checkRes) != PGRES_TUPLES_OK) {
        spdlog::error("[CertUtils] Failed to check duplicate: {}", PQerrorMessage(conn));
        PQclear(checkRes);
        return {-1, false};
    }

    // If certificate exists, return existing ID and isDuplicate=true
    if (PQntuples(checkRes) > 0) {
        int existingId = std::atoi(PQgetvalue(checkRes, 0, 0));
        PQclear(checkRes);

        spdlog::debug("[CertUtils] Duplicate certificate found: id={}, fingerprint={}",
                     existingId, fingerprint.substr(0, 16) + "...");

        return {existingId, true};
    }

    PQclear(checkRes);

    // Step 2: Insert new certificate
    // Convert DER bytes to PostgreSQL bytea format
    size_t byteaLen;
    unsigned char* byteaEscaped = PQescapeByteaConn(conn,
        reinterpret_cast<const unsigned char*>(certData.data()),
        certData.size(), &byteaLen);

    if (!byteaEscaped) {
        spdlog::error("[CertUtils] Failed to escape bytea data");
        return {-1, false};
    }

    std::string byteaStr = reinterpret_cast<const char*>(byteaEscaped);
    PQfreemem(byteaEscaped);

    const char* insertQuery =
        "INSERT INTO certificate ("
        "upload_id, certificate_type, country_code, "
        "subject_dn, issuer_dn, serial_number, fingerprint_sha256, "
        "not_before, not_after, certificate_data, "
        "validation_status, validation_message, "
        "duplicate_count, first_upload_id, created_at"
        ") VALUES ("
        "$1::uuid, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, 0, $1::uuid, NOW()"
        ") RETURNING id";

    const char* insertParams[12] = {
        uploadId.c_str(),
        certType.c_str(),
        countryCode.c_str(),
        subjectDn.c_str(),
        issuerDn.c_str(),
        serialNumber.c_str(),
        fingerprint.c_str(),
        notBefore.c_str(),
        notAfter.c_str(),
        byteaStr.c_str(),
        validationStatus.c_str(),
        validationMessage.c_str()
    };

    PGresult* insertRes = PQexecParams(conn, insertQuery, 12, nullptr, insertParams,
                                       nullptr, nullptr, 0);

    if (PQresultStatus(insertRes) != PGRES_TUPLES_OK) {
        spdlog::error("[CertUtils] Failed to insert certificate: {}", PQerrorMessage(conn));
        PQclear(insertRes);
        return {-1, false};
    }

    int newId = std::atoi(PQgetvalue(insertRes, 0, 0));
    PQclear(insertRes);

    spdlog::debug("[CertUtils] New certificate inserted: id={}, type={}, country={}, fingerprint={}",
                 newId, certType, countryCode, fingerprint.substr(0, 16) + "...");

    return {newId, false};
}

bool trackCertificateDuplicate(
    PGconn* conn,
    int certificateId,
    const std::string& uploadId,
    const std::string& sourceType,
    const std::string& sourceCountry,
    const std::string& sourceEntryDn,
    const std::string& sourceFileName
) {
    std::string certIdStr = std::to_string(certificateId);

    const char* query =
        "INSERT INTO certificate_duplicates ("
        "certificate_id, upload_id, source_type, source_country, "
        "source_entry_dn, source_file_name, detected_at"
        ") VALUES ("
        "$1, $2::uuid, $3, $4, $5, $6, NOW()"
        ") ON CONFLICT (certificate_id, upload_id, source_type) DO NOTHING";

    const char* params[6] = {
        certIdStr.c_str(),
        uploadId.c_str(),
        sourceType.c_str(),
        !sourceCountry.empty() ? sourceCountry.c_str() : nullptr,
        !sourceEntryDn.empty() ? sourceEntryDn.c_str() : nullptr,
        !sourceFileName.empty() ? sourceFileName.c_str() : nullptr
    };

    PGresult* res = PQexecParams(conn, query, 6, nullptr, params,
                                 nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("[CertUtils] Failed to track duplicate: {}", PQerrorMessage(conn));
        PQclear(res);
        return false;
    }

    PQclear(res);

    spdlog::debug("[CertUtils] Tracked duplicate: cert_id={}, upload={}, source_type={}, country={}",
                 certificateId, uploadId.substr(0, 8) + "...", sourceType, sourceCountry);

    return true;
}

bool incrementDuplicateCount(
    PGconn* conn,
    int certificateId,
    const std::string& uploadId
) {
    std::string certIdStr = std::to_string(certificateId);

    const char* query =
        "UPDATE certificate "
        "SET duplicate_count = duplicate_count + 1, "
        "    last_seen_upload_id = $2::uuid, "
        "    last_seen_at = NOW() "
        "WHERE id = $1";

    const char* params[2] = {
        certIdStr.c_str(),
        uploadId.c_str()
    };

    PGresult* res = PQexecParams(conn, query, 2, nullptr, params,
                                 nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("[CertUtils] Failed to increment duplicate count: {}", PQerrorMessage(conn));
        PQclear(res);
        return false;
    }

    PQclear(res);

    spdlog::debug("[CertUtils] Incremented duplicate count: cert_id={}, upload={}",
                 certificateId, uploadId.substr(0, 8) + "...");

    return true;
}

bool updateCscaExtractionStats(
    PGconn* conn,
    const std::string& uploadId,
    int extractedCount,
    int duplicateCount
) {
    std::string extractedStr = std::to_string(extractedCount);
    std::string duplicateStr = std::to_string(duplicateCount);

    const char* query =
        "UPDATE uploaded_file "
        "SET csca_extracted_from_ml = $2, "
        "    csca_duplicates = $3 "
        "WHERE id = $1::uuid";

    const char* params[3] = {
        uploadId.c_str(),
        extractedStr.c_str(),
        duplicateStr.c_str()
    };

    PGresult* res = PQexecParams(conn, query, 3, nullptr, params,
                                 nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("[CertUtils] Failed to update CSCA extraction stats: {}", PQerrorMessage(conn));
        PQclear(res);
        return false;
    }

    PQclear(res);

    spdlog::info("[CertUtils] Updated CSCA extraction stats: upload={}, extracted={}, duplicates={}",
                uploadId.substr(0, 8) + "...", extractedCount, duplicateCount);

    return true;
}

std::string getSourceType(const std::string& fileFormat) {
    if (fileFormat == "LDIF_001") return "LDIF_001";
    if (fileFormat == "LDIF_002") return "LDIF_002";
    if (fileFormat == "LDIF_003") return "LDIF_003";
    if (fileFormat == "MASTERLIST") return "ML_FILE";
    return "UNKNOWN";
}

} // namespace certificate_utils
