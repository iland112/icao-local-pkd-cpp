#include "certificate_utils.h"
#include "x509_metadata_extractor.h"
#include <spdlog/spdlog.h>
#include <openssl/x509.h>
#include <cstring>
#include <sstream>

namespace certificate_utils {

std::pair<std::string, bool> saveCertificateWithDuplicateCheck(
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
        return std::make_pair(std::string(""), false);
    }

    // If certificate exists, return existing ID and isDuplicate=true
    if (PQntuples(checkRes) > 0) {
        std::string existingId = PQgetvalue(checkRes, 0, 0);
        PQclear(checkRes);

        spdlog::debug("[CertUtils] Duplicate certificate found: id={}, fingerprint={}",
                     existingId.substr(0, 8) + "...", fingerprint.substr(0, 16) + "...");

        return std::make_pair(existingId, true);
    }

    PQclear(checkRes);

    // Step 2: Extract X.509 metadata from certificate
    const unsigned char* certPtr = certData.data();
    X509* x509cert = d2i_X509(nullptr, &certPtr, static_cast<long>(certData.size()));

    x509::CertificateMetadata x509meta;
    std::string versionStr, sigAlg, sigHashAlg, pubKeyAlg, pubKeySizeStr;
    std::string pubKeyCurve, keyUsageStr, extKeyUsageStr, isCaStr;
    std::string pathLenStr, ski, aki, crlDpStr, ocspUrl, isSelfSignedStr;

    if (x509cert) {
        x509meta = x509::extractMetadata(x509cert);
        X509_free(x509cert);

        // Convert metadata to SQL strings
        versionStr = std::to_string(x509meta.version);
        sigAlg = x509meta.signatureAlgorithm;
        sigHashAlg = x509meta.signatureHashAlgorithm;
        pubKeyAlg = x509meta.publicKeyAlgorithm;
        pubKeySizeStr = std::to_string(x509meta.publicKeySize);
        pubKeyCurve = x509meta.publicKeyCurve.value_or("");

        // Convert arrays to PostgreSQL array format
        std::ostringstream kuStream, ekuStream, crlStream;
        kuStream << "{";
        for (size_t i = 0; i < x509meta.keyUsage.size(); i++) {
            kuStream << "\"" << x509meta.keyUsage[i] << "\"";
            if (i < x509meta.keyUsage.size() - 1) kuStream << ",";
        }
        kuStream << "}";
        keyUsageStr = kuStream.str();

        ekuStream << "{";
        for (size_t i = 0; i < x509meta.extendedKeyUsage.size(); i++) {
            ekuStream << "\"" << x509meta.extendedKeyUsage[i] << "\"";
            if (i < x509meta.extendedKeyUsage.size() - 1) ekuStream << ",";
        }
        ekuStream << "}";
        extKeyUsageStr = ekuStream.str();

        crlStream << "{";
        for (size_t i = 0; i < x509meta.crlDistributionPoints.size(); i++) {
            crlStream << "\"" << x509meta.crlDistributionPoints[i] << "\"";
            if (i < x509meta.crlDistributionPoints.size() - 1) crlStream << ",";
        }
        crlStream << "}";
        crlDpStr = crlStream.str();

        isCaStr = x509meta.isCA ? "TRUE" : "FALSE";
        pathLenStr = x509meta.pathLenConstraint.has_value() ?
                     std::to_string(x509meta.pathLenConstraint.value()) : "";
        ski = x509meta.subjectKeyIdentifier.value_or("");
        aki = x509meta.authorityKeyIdentifier.value_or("");
        ocspUrl = x509meta.ocspResponderUrl.value_or("");
        isSelfSignedStr = x509meta.isSelfSigned ? "TRUE" : "FALSE";
    } else {
        spdlog::warn("[CertUtils] Failed to parse X509 certificate for metadata extraction");
        versionStr = "2";  // Default to v3
        isCaStr = "FALSE";
        isSelfSignedStr = "FALSE";
        keyUsageStr = "{}";
        extKeyUsageStr = "{}";
        crlDpStr = "{}";
    }

    // Step 3: Insert new certificate with X.509 metadata
    // Convert DER bytes to PostgreSQL bytea format
    size_t byteaLen;
    unsigned char* byteaEscaped = PQescapeByteaConn(conn,
        reinterpret_cast<const unsigned char*>(certData.data()),
        certData.size(), &byteaLen);

    if (!byteaEscaped) {
        spdlog::error("[CertUtils] Failed to escape bytea data");
        return std::make_pair(std::string(""), false);
    }

    std::string byteaStr = reinterpret_cast<const char*>(byteaEscaped);
    PQfreemem(byteaEscaped);

    const char* insertQuery =
        "INSERT INTO certificate ("
        "upload_id, certificate_type, country_code, "
        "subject_dn, issuer_dn, serial_number, fingerprint_sha256, "
        "not_before, not_after, certificate_data, "
        "validation_status, validation_message, "
        "duplicate_count, first_upload_id, created_at, "
        "version, signature_algorithm, signature_hash_algorithm, "
        "public_key_algorithm, public_key_size, public_key_curve, "
        "key_usage, extended_key_usage, "
        "is_ca, path_len_constraint, "
        "subject_key_identifier, authority_key_identifier, "
        "crl_distribution_points, ocsp_responder_url, is_self_signed"
        ") VALUES ("
        "$1::uuid, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, 0, $1::uuid, NOW(), "
        "$13, NULLIF($14, ''), NULLIF($15, ''), "
        "NULLIF($16, ''), NULLIF($17, '0')::integer, NULLIF($18, ''), "
        "NULLIF($19, '{}')::text[], NULLIF($20, '{}')::text[], "
        "$21, NULLIF($22, '')::integer, "
        "NULLIF($23, ''), NULLIF($24, ''), "
        "NULLIF($25, '{}')::text[], NULLIF($26, ''), $27"
        ") RETURNING id";

    const char* insertParams[27] = {
        uploadId.c_str(),                    // $1
        certType.c_str(),                    // $2
        countryCode.c_str(),                 // $3
        subjectDn.c_str(),                   // $4
        issuerDn.c_str(),                    // $5
        serialNumber.c_str(),                // $6
        fingerprint.c_str(),                 // $7
        notBefore.c_str(),                   // $8
        notAfter.c_str(),                    // $9
        byteaStr.c_str(),                    // $10
        validationStatus.c_str(),            // $11
        validationMessage.c_str(),           // $12
        versionStr.c_str(),                  // $13
        sigAlg.c_str(),                      // $14
        sigHashAlg.c_str(),                  // $15
        pubKeyAlg.c_str(),                   // $16
        pubKeySizeStr.c_str(),               // $17
        pubKeyCurve.c_str(),                 // $18
        keyUsageStr.c_str(),                 // $19
        extKeyUsageStr.c_str(),              // $20
        isCaStr.c_str(),                     // $21
        pathLenStr.c_str(),                  // $22
        ski.c_str(),                         // $23
        aki.c_str(),                         // $24
        crlDpStr.c_str(),                    // $25
        ocspUrl.c_str(),                     // $26
        isSelfSignedStr.c_str()              // $27
    };

    PGresult* insertRes = PQexecParams(conn, insertQuery, 27, nullptr, insertParams,
                                       nullptr, nullptr, 0);

    if (PQresultStatus(insertRes) != PGRES_TUPLES_OK) {
        spdlog::error("[CertUtils] Failed to insert certificate: {}", PQerrorMessage(conn));
        PQclear(insertRes);
        return std::make_pair(std::string(""), false);
    }

    std::string newId = PQgetvalue(insertRes, 0, 0);
    PQclear(insertRes);

    spdlog::debug("[CertUtils] New certificate inserted: id={}, type={}, country={}, fingerprint={}",
                 newId.substr(0, 8) + "...", certType, countryCode, fingerprint.substr(0, 16) + "...");

    return std::make_pair(newId, false);
}

bool trackCertificateDuplicate(
    PGconn* conn,
    const std::string& certificateId,
    const std::string& uploadId,
    const std::string& sourceType,
    const std::string& sourceCountry,
    const std::string& sourceEntryDn,
    const std::string& sourceFileName
) {
    const char* query =
        "INSERT INTO certificate_duplicates ("
        "certificate_id, upload_id, source_type, source_country, "
        "source_entry_dn, source_file_name, detected_at"
        ") VALUES ("
        "$1::uuid, $2::uuid, $3, $4, $5, $6, NOW()"
        ") ON CONFLICT (certificate_id, upload_id, source_type) DO NOTHING";

    const char* params[6] = {
        certificateId.c_str(),
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
                 certificateId.substr(0, 8) + "...", uploadId.substr(0, 8) + "...", sourceType, sourceCountry);

    return true;
}

bool incrementDuplicateCount(
    PGconn* conn,
    const std::string& certificateId,
    const std::string& uploadId
) {
    const char* query =
        "UPDATE certificate "
        "SET duplicate_count = duplicate_count + 1, "
        "    last_seen_upload_id = $2::uuid, "
        "    last_seen_at = NOW() "
        "WHERE id = $1::uuid";

    const char* params[2] = {
        certificateId.c_str(),
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
                 certificateId.substr(0, 8) + "...", uploadId.substr(0, 8) + "...");

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
        "SET csca_extracted_from_ml = csca_extracted_from_ml + $2, "
        "    csca_duplicates = csca_duplicates + $3 "
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

bool updateCertificateLdapStatus(
    PGconn* conn,
    const std::string& certificateId,
    const std::string& ldapDn
) {
    const char* query =
        "UPDATE certificate "
        "SET stored_in_ldap = TRUE, "
        "    ldap_dn_v2 = $2, "
        "    stored_at = NOW() "
        "WHERE id = $1::uuid";

    const char* params[2] = {
        certificateId.c_str(),
        ldapDn.c_str()
    };

    PGresult* res = PQexecParams(conn, query, 2, nullptr, params,
                                 nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("[CertUtils] Failed to update LDAP status: {}", PQerrorMessage(conn));
        PQclear(res);
        return false;
    }

    PQclear(res);

    spdlog::debug("[CertUtils] Updated LDAP status: cert_id={}, ldap_dn={}",
                 certificateId.substr(0, 8) + "...", ldapDn);

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
