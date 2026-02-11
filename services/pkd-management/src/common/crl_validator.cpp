/**
 * @file crl_validator.cpp
 * @brief CRL-based certificate revocation checking implementation
 *
 * Sprint 2: Link Certificate Validation Core
 * Implements RFC 5280 CRL validation for CSCA/DSC/LC certificates
 *
 * @version 1.0.0
 * @date 2026-01-24
 */

#include "crl_validator.h"
#include <spdlog/spdlog.h>
#include <openssl/bn.h>
#include <openssl/bio.h>
#include <ctime>
#include <sstream>
#include <iomanip>

namespace crl {

// Helper: Convert hex string (with optional \x prefix) to binary bytes
static std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::string data = hex;
    // Remove PostgreSQL BYTEA prefix if present
    if (data.size() >= 2 && data[0] == '\\' && data[1] == 'x') {
        data = data.substr(2);
    }
    std::vector<uint8_t> bytes;
    bytes.reserve(data.length() / 2);
    for (size_t i = 0; i + 1 < data.length(); i += 2) {
        char hexByte[3] = {data[i], data[i + 1], 0};
        bytes.push_back(static_cast<uint8_t>(strtol(hexByte, nullptr, 16)));
    }
    return bytes;
}

CrlValidator::CrlValidator(common::IQueryExecutor* executor) : executor_(executor) {
    if (!executor_) {
        throw std::runtime_error("Invalid query executor (nullptr)");
    }
}

RevocationCheckResult CrlValidator::checkRevocation(
    const std::string& certificateId,
    const std::string& certificateType,
    const std::string& serialNumber,
    const std::string& fingerprint,
    const std::string& issuerDn
) {
    auto startTime = std::chrono::high_resolution_clock::now();

    RevocationCheckResult result;
    result.status = RevocationStatus::UNKNOWN;
    result.crlIssuerDn = issuerDn;
    result.message = "CRL check not performed";

    // Step 1: Find latest CRL for issuer
    std::string dbType = executor_->getDatabaseType();
    std::string query =
        "SELECT id, crl_binary, this_update, next_update "
        "FROM crl "
        "WHERE issuer_dn = $1 "
        "ORDER BY this_update DESC ";
    if (dbType == "oracle") {
        query += "FETCH FIRST 1 ROWS ONLY";
    } else {
        query += "LIMIT 1";
    }

    Json::Value rows;
    try {
        rows = executor_->executeQuery(query, {issuerDn});
    } catch (const std::exception& e) {
        spdlog::error("[CrlValidator] Query failed: {}", e.what());
        rows = Json::Value(Json::arrayValue);
    }

    if (rows.empty()) {
        spdlog::warn("[CrlValidator] No CRL found for issuer: {}", issuerDn);

        result.status = RevocationStatus::UNKNOWN;
        result.message = "No CRL available for issuer";

        auto endTime = std::chrono::high_resolution_clock::now();
        result.checkDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();

        logRevocationCheck(result, certificateId, certificateType,
                           serialNumber, fingerprint, "", "");
        return result;
    }

    // Extract CRL metadata
    std::string crlId = rows[0].get("id", "").asString();
    result.crlThisUpdate = rows[0].get("this_update", "").asString();
    result.crlNextUpdate = rows[0].get("next_update", "").asString();

    // Step 2: Parse CRL binary from hex-encoded string
    std::string crlHex = rows[0].get("crl_binary", "").asString();
    std::vector<uint8_t> crlBytes = hexToBytes(crlHex);

    if (crlBytes.empty()) {
        spdlog::error("[CrlValidator] Failed to decode CRL binary from hex");

        result.status = RevocationStatus::UNKNOWN;
        result.message = "Failed to parse CRL binary";

        auto endTime = std::chrono::high_resolution_clock::now();
        result.checkDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();

        logRevocationCheck(result, certificateId, certificateType,
                           serialNumber, fingerprint, "", crlId);
        return result;
    }

    const unsigned char* p = crlBytes.data();
    X509_CRL* crl = d2i_X509_CRL(nullptr, &p, static_cast<long>(crlBytes.size()));

    if (!crl) {
        spdlog::error("[CrlValidator] Failed to parse CRL (d2i_X509_CRL failed)");

        result.status = RevocationStatus::UNKNOWN;
        result.message = "Failed to parse CRL structure";

        auto endTime = std::chrono::high_resolution_clock::now();
        result.checkDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();

        logRevocationCheck(result, certificateId, certificateType,
                           serialNumber, fingerprint, "", crlId);
        return result;
    }

    // Step 3: Check if certificate serial is in revoked list
    STACK_OF(X509_REVOKED)* revokedList = X509_CRL_get_REVOKED(crl);

    if (!revokedList || sk_X509_REVOKED_num(revokedList) == 0) {
        // CRL exists but no revoked certificates
        X509_CRL_free(crl);

        result.status = RevocationStatus::GOOD;
        result.message = "Certificate not revoked (CRL has no revoked entries)";

        auto endTime = std::chrono::high_resolution_clock::now();
        result.checkDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();

        logRevocationCheck(result, certificateId, certificateType,
                           serialNumber, fingerprint, "", crlId);
        return result;
    }

    // Convert serial number hex string to ASN1_INTEGER
    ASN1_INTEGER* serialAsn1 = hexSerialToAsn1(serialNumber);
    if (!serialAsn1) {
        X509_CRL_free(crl);

        spdlog::error("[CrlValidator] Failed to convert serial to ASN1: {}", serialNumber);

        result.status = RevocationStatus::UNKNOWN;
        result.message = "Failed to parse certificate serial number";

        auto endTime = std::chrono::high_resolution_clock::now();
        result.checkDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();

        logRevocationCheck(result, certificateId, certificateType,
                           serialNumber, fingerprint, "", crlId);
        return result;
    }

    // Step 4: Search for serial in revoked list
    bool isRevoked = false;
    RevocationReason revocationReason = RevocationReason::UNKNOWN;
    std::string revocationDate;

    for (int i = 0; i < sk_X509_REVOKED_num(revokedList); i++) {
        X509_REVOKED* revoked = sk_X509_REVOKED_value(revokedList, i);
        const ASN1_INTEGER* revokedSerial = X509_REVOKED_get0_serialNumber(revoked);

        if (ASN1_INTEGER_cmp(serialAsn1, revokedSerial) == 0) {
            // Certificate is revoked!
            isRevoked = true;

            // Get revocation date
            const ASN1_TIME* revDate = X509_REVOKED_get0_revocationDate(revoked);
            if (revDate) {
                revocationDate = asn1TimeToString(revDate);
            }

            // Get revocation reason (optional extension)
            int criticalFlag;
            ASN1_ENUMERATED* reasonExt = static_cast<ASN1_ENUMERATED*>(
                X509_REVOKED_get_ext_d2i(revoked, NID_crl_reason, &criticalFlag, nullptr)
            );

            if (reasonExt) {
                int opensslReason = ASN1_ENUMERATED_get(reasonExt);
                revocationReason = opensslReasonToEnum(opensslReason);
                ASN1_ENUMERATED_free(reasonExt);
            } else {
                revocationReason = RevocationReason::UNSPECIFIED;
            }

            spdlog::warn("[CrlValidator] Certificate REVOKED - Serial: {}, Reason: {}, Date: {}",
                         serialNumber,
                         revocationReasonToString(revocationReason),
                         revocationDate);
            break;
        }
    }

    ASN1_INTEGER_free(serialAsn1);
    X509_CRL_free(crl);

    // Step 5: Set result
    if (isRevoked) {
        result.status = RevocationStatus::REVOKED;
        result.reason = revocationReason;
        result.revocationDate = revocationDate;
        result.message = "Certificate is revoked: " + revocationReasonToString(revocationReason);
    } else {
        result.status = RevocationStatus::GOOD;
        result.message = "Certificate not found in CRL revoked list";
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    result.checkDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime).count();

    // Step 6: Log result
    logRevocationCheck(result, certificateId, certificateType,
                       serialNumber, fingerprint, "", crlId);

    return result;
}

bool CrlValidator::isCrlExpired(const std::string& issuerDn) {
    auto metadata = getLatestCrlMetadata(issuerDn);
    if (!metadata) {
        return true;  // No CRL = expired
    }

    auto [thisUpdate, nextUpdate, crlId] = *metadata;

    // Simple check: nextUpdate < NOW()
    std::string dbType = executor_->getDatabaseType();
    std::string query;
    if (dbType == "oracle") {
        query = "SELECT CASE WHEN SYSTIMESTAMP > TO_TIMESTAMP($1, 'YYYY-MM-DD HH24:MI:SS') THEN 1 ELSE 0 END AS expired FROM DUAL";
    } else {
        query = "SELECT NOW() > $1::timestamp AS expired";
    }

    try {
        Json::Value rows = executor_->executeQuery(query, {nextUpdate});
        if (!rows.empty()) {
            std::string val = rows[0].get("expired", "").asString();
            // PostgreSQL returns "t"/"f", Oracle returns "1"/"0"
            return (val == "t" || val == "1" || val == "true");
        }
    } catch (const std::exception& e) {
        spdlog::error("[CrlValidator] isCrlExpired query failed: {}", e.what());
    }

    return true;  // On error, consider expired
}

std::optional<std::tuple<std::string, std::string, std::string>>
CrlValidator::getLatestCrlMetadata(const std::string& issuerDn) {
    std::string dbType = executor_->getDatabaseType();
    std::string query =
        "SELECT this_update, next_update, id "
        "FROM crl "
        "WHERE issuer_dn = $1 "
        "ORDER BY this_update DESC ";
    if (dbType == "oracle") {
        query += "FETCH FIRST 1 ROWS ONLY";
    } else {
        query += "LIMIT 1";
    }

    try {
        Json::Value rows = executor_->executeQuery(query, {issuerDn});
        if (rows.empty()) {
            return std::nullopt;
        }

        std::string thisUpdate = rows[0].get("this_update", "").asString();
        std::string nextUpdate = rows[0].get("next_update", "").asString();
        std::string crlId = rows[0].get("id", "").asString();

        return std::make_tuple(thisUpdate, nextUpdate, crlId);
    } catch (const std::exception& e) {
        spdlog::error("[CrlValidator] getLatestCrlMetadata query failed: {}", e.what());
        return std::nullopt;
    }
}

void CrlValidator::logRevocationCheck(
    const RevocationCheckResult& result,
    const std::string& certificateId,
    const std::string& certificateType,
    const std::string& serialNumber,
    const std::string& fingerprint,
    const std::string& subjectDn,
    const std::string& crlId
) {
    std::string dbType = executor_->getDatabaseType();
    std::string nowFunc = (dbType == "oracle") ? "SYSTIMESTAMP" : "NOW()";

    std::string query =
        "INSERT INTO crl_revocation_log ("
        "    certificate_id, certificate_type, serial_number, fingerprint_sha256, "
        "    subject_dn, revocation_status, revocation_reason, revocation_date, "
        "    crl_id, crl_issuer_dn, crl_this_update, crl_next_update, "
        "    checked_at, check_duration_ms"
        ") VALUES ("
        "    $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, " + nowFunc + ", $13"
        ")";

    std::string statusStr = revocationStatusToString(result.status);
    std::string reasonStr = result.reason.has_value() ?
        revocationReasonToString(*result.reason) : "";
    std::string revDateStr = result.revocationDate.value_or("");
    std::string checkDurationStr = std::to_string(result.checkDurationMs);
    std::string crlIdParam = crlId.empty() ? "" : crlId;

    std::vector<std::string> params = {
        certificateId,          // $1
        certificateType,        // $2
        serialNumber,           // $3
        fingerprint,            // $4
        subjectDn,              // $5
        statusStr,              // $6
        reasonStr,              // $7
        revDateStr,             // $8
        crlIdParam,             // $9
        result.crlIssuerDn,     // $10
        result.crlThisUpdate,   // $11
        result.crlNextUpdate,   // $12
        checkDurationStr        // $13
    };

    try {
        executor_->executeCommand(query, params);
    } catch (const std::exception& e) {
        spdlog::error("[CrlValidator] Failed to log revocation check: {}", e.what());
    }
}

// ============================================================================
// Private Helper Methods
// ============================================================================

std::string CrlValidator::revocationReasonToString(RevocationReason reason) {
    switch (reason) {
        case RevocationReason::UNSPECIFIED: return "unspecified";
        case RevocationReason::KEY_COMPROMISE: return "keyCompromise";
        case RevocationReason::CA_COMPROMISE: return "cACompromise";
        case RevocationReason::AFFILIATION_CHANGED: return "affiliationChanged";
        case RevocationReason::SUPERSEDED: return "superseded";
        case RevocationReason::CESSATION_OF_OPERATION: return "cessationOfOperation";
        case RevocationReason::CERTIFICATE_HOLD: return "certificateHold";
        case RevocationReason::REMOVE_FROM_CRL: return "removeFromCRL";
        case RevocationReason::PRIVILEGE_WITHDRAWN: return "privilegeWithdrawn";
        case RevocationReason::AA_COMPROMISE: return "aACompromise";
        case RevocationReason::UNKNOWN:
        default:
            return "unknown";
    }
}

RevocationReason CrlValidator::opensslReasonToEnum(int opensslReason) {
    switch (opensslReason) {
        case 0: return RevocationReason::UNSPECIFIED;
        case 1: return RevocationReason::KEY_COMPROMISE;
        case 2: return RevocationReason::CA_COMPROMISE;
        case 3: return RevocationReason::AFFILIATION_CHANGED;
        case 4: return RevocationReason::SUPERSEDED;
        case 5: return RevocationReason::CESSATION_OF_OPERATION;
        case 6: return RevocationReason::CERTIFICATE_HOLD;
        case 8: return RevocationReason::REMOVE_FROM_CRL;
        case 9: return RevocationReason::PRIVILEGE_WITHDRAWN;
        case 10: return RevocationReason::AA_COMPROMISE;
        default: return RevocationReason::UNKNOWN;
    }
}

std::string CrlValidator::asn1TimeToString(const ASN1_TIME* asn1Time) {
    if (!asn1Time) {
        return "";
    }

    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) {
        return "";
    }

    ASN1_TIME_print(bio, asn1Time);

    char* data;
    long len = BIO_get_mem_data(bio, &data);

    std::string result(data, len);
    BIO_free(bio);

    // Convert "Jan  1 00:00:00 2025 GMT" to ISO 8601
    // Simple approach: just return the OpenSSL format for now
    // TODO: Full ISO 8601 conversion if needed
    return result;
}

ASN1_INTEGER* CrlValidator::hexSerialToAsn1(const std::string& serialHex) {
    BIGNUM* serialBn = nullptr;
    if (BN_hex2bn(&serialBn, serialHex.c_str()) == 0) {
        return nullptr;
    }

    ASN1_INTEGER* serialAsn1 = BN_to_ASN1_INTEGER(serialBn, nullptr);
    BN_free(serialBn);

    return serialAsn1;
}

} // namespace crl
