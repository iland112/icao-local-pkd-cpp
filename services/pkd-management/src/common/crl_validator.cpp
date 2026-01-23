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

CrlValidator::CrlValidator(PGconn* conn) : conn_(conn) {
    if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
        throw std::runtime_error("Invalid PostgreSQL connection");
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
    const char* query =
        "SELECT id, crl_binary, this_update, next_update "
        "FROM crl "
        "WHERE issuer_dn = $1 "
        "ORDER BY this_update DESC "
        "LIMIT 1";

    const char* paramValues[1] = {issuerDn.c_str()};
    PGresult* res = PQexecParams(conn_, query, 1, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
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
    std::string crlId = PQgetvalue(res, 0, 0);
    result.crlThisUpdate = PQgetvalue(res, 0, 2);
    result.crlNextUpdate = PQgetvalue(res, 0, 3);

    // Step 2: Parse CRL binary
    size_t crlLen;
    unsigned char* crlData = PQunescapeBytea(
        reinterpret_cast<const unsigned char*>(PQgetvalue(res, 0, 1)),
        &crlLen
    );

    if (!crlData) {
        PQclear(res);
        spdlog::error("[CrlValidator] Failed to unescape CRL binary");

        result.status = RevocationStatus::UNKNOWN;
        result.message = "Failed to parse CRL binary";

        auto endTime = std::chrono::high_resolution_clock::now();
        result.checkDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();

        logRevocationCheck(result, certificateId, certificateType,
                           serialNumber, fingerprint, "", crlId);
        return result;
    }

    const unsigned char* p = crlData;
    X509_CRL* crl = d2i_X509_CRL(nullptr, &p, crlLen);
    PQfreemem(crlData);
    PQclear(res);

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
    const char* query = "SELECT NOW() > $1::timestamp";
    const char* paramValues[1] = {nextUpdate.c_str()};

    PGresult* res = PQexecParams(conn_, query, 1, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    bool expired = false;
    if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
        std::string result = PQgetvalue(res, 0, 0);
        expired = (result == "t");  // PostgreSQL boolean true
    }

    PQclear(res);
    return expired;
}

std::optional<std::tuple<std::string, std::string, std::string>>
CrlValidator::getLatestCrlMetadata(const std::string& issuerDn) {
    const char* query =
        "SELECT this_update, next_update, id "
        "FROM crl "
        "WHERE issuer_dn = $1 "
        "ORDER BY this_update DESC "
        "LIMIT 1";

    const char* paramValues[1] = {issuerDn.c_str()};
    PGresult* res = PQexecParams(conn_, query, 1, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return std::nullopt;
    }

    std::string thisUpdate = PQgetvalue(res, 0, 0);
    std::string nextUpdate = PQgetvalue(res, 0, 1);
    std::string crlId = PQgetvalue(res, 0, 2);

    PQclear(res);

    return std::make_tuple(thisUpdate, nextUpdate, crlId);
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
    const char* query =
        "INSERT INTO crl_revocation_log ("
        "    certificate_id, certificate_type, serial_number, fingerprint_sha256, "
        "    subject_dn, revocation_status, revocation_reason, revocation_date, "
        "    crl_id, crl_issuer_dn, crl_this_update, crl_next_update, "
        "    checked_at, check_duration_ms"
        ") VALUES ("
        "    $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12, NOW(), $13"
        ")";

    std::string statusStr = revocationStatusToString(result.status);
    std::string reasonStr = result.reason.has_value() ?
        revocationReasonToString(*result.reason) : "";
    std::string revDateStr = result.revocationDate.value_or("");
    std::string checkDurationStr = std::to_string(result.checkDurationMs);
    std::string crlIdParam = crlId.empty() ? "" : crlId;

    const char* paramValues[13] = {
        certificateId.c_str(),
        certificateType.c_str(),
        serialNumber.c_str(),
        fingerprint.c_str(),
        subjectDn.c_str(),
        statusStr.c_str(),
        reasonStr.c_str(),
        revDateStr.c_str(),
        crlIdParam.empty() ? nullptr : crlIdParam.c_str(),
        result.crlIssuerDn.c_str(),
        result.crlThisUpdate.c_str(),
        result.crlNextUpdate.c_str(),
        checkDurationStr.c_str()
    };

    PGresult* res = PQexecParams(conn_, query, 13, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("[CrlValidator] Failed to log revocation check: {}",
                      PQerrorMessage(conn_));
    }

    PQclear(res);
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
