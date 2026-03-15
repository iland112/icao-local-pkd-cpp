/**
 * @file dsc_auto_registration_service.cpp
 * @brief DSC Pending Registration from PA Verification
 *
 * Changed from auto-registration to pending approval workflow (v2.31.0):
 * DSC certificates are saved to pending_dsc_registration table
 * instead of directly to certificate table.
 */

#include "dsc_auto_registration_service.h"
#include "query_helpers.h"
#include <spdlog/spdlog.h>
#include <openssl/evp.h>
#include <openssl/bn.h>
#include <openssl/bio.h>
#include <openssl/x509.h>
#include <openssl/objects.h>
#include <sstream>
#include <iomanip>
#include <random>
#include <cstring>

namespace services {

DscAutoRegistrationService::DscAutoRegistrationService(common::IQueryExecutor* queryExecutor)
    : queryExecutor_(queryExecutor)
{
    if (!queryExecutor_) {
        throw std::invalid_argument("QueryExecutor cannot be null");
    }
    spdlog::info("[DscAutoReg] DSC pending registration service initialized");
}

DscRegistrationResult DscAutoRegistrationService::registerDscFromSod(
    X509* dscCert,
    const std::string& countryCode,
    const std::string& verificationId,
    const std::string& verificationStatus)
{
    DscRegistrationResult result;

    if (!dscCert) {
        spdlog::warn("[DscAutoReg] DSC certificate is null, skipping");
        return result;
    }

    try {
        // 1. Compute SHA-256 fingerprint
        result.fingerprint = computeFingerprint(dscCert);
        if (result.fingerprint.empty()) {
            spdlog::error("[DscAutoReg] Failed to compute fingerprint");
            return result;
        }

        result.countryCode = countryCode;

        // 2. Check if DSC already exists in certificate table
        const char* checkCertQuery =
            "SELECT id FROM certificate "
            "WHERE certificate_type = 'DSC' AND fingerprint_sha256 = $1 "
            "FETCH FIRST 1 ROWS ONLY";

        std::vector<std::string> checkParams = {result.fingerprint};
        Json::Value certCheck = queryExecutor_->executeQuery(checkCertQuery, checkParams);

        if (!certCheck.empty()) {
            // DSC already registered in certificate table
            result.success = true;
            result.alreadyRegistered = true;
            result.certificateId = certCheck[0]["id"].asString();
            spdlog::debug("[DscAutoReg] DSC already registered: id={}, fingerprint={}...",
                result.certificateId.substr(0, 8), result.fingerprint.substr(0, 16));
            return result;
        }

        // 3. Check if already pending approval
        const char* checkPendingQuery =
            "SELECT id, status FROM pending_dsc_registration "
            "WHERE fingerprint_sha256 = $1 "
            "FETCH FIRST 1 ROWS ONLY";

        Json::Value pendingCheck = queryExecutor_->executeQuery(checkPendingQuery, checkParams);

        if (!pendingCheck.empty()) {
            std::string pendingStatus = pendingCheck[0]["status"].asString();
            result.success = true;
            result.pendingId = pendingCheck[0]["id"].asString();
            if (pendingStatus == "PENDING") {
                result.pendingApproval = true;
                spdlog::debug("[DscAutoReg] DSC already pending approval: id={}, fingerprint={}...",
                    result.pendingId.substr(0, 8), result.fingerprint.substr(0, 16));
            } else {
                // APPROVED or REJECTED — already processed
                spdlog::debug("[DscAutoReg] DSC pending entry exists (status={}): fingerprint={}...",
                    pendingStatus, result.fingerprint.substr(0, 16));
            }
            return result;
        }

        // 4. Extract certificate fields
        std::string subjectDn = x509NameToString(X509_get_subject_name(dscCert));
        std::string issuerDn = x509NameToString(X509_get_issuer_name(dscCert));

        // Serial number
        ASN1_INTEGER* serial = X509_get_serialNumber(dscCert);
        std::string serialNumber;
        if (serial) {
            BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
            if (bn) {
                char* hex = BN_bn2hex(bn);
                if (hex) {
                    serialNumber = hex;
                    OPENSSL_free(hex);
                }
                BN_free(bn);
            }
        }

        // Validity dates
        std::string notBefore = asn1TimeToString(X509_get0_notBefore(dscCert));
        std::string notAfter = asn1TimeToString(X509_get0_notAfter(dscCert));

        // DER-encoded certificate data
        std::vector<uint8_t> derBytes = getDerBytes(dscCert);
        if (derBytes.empty()) {
            spdlog::error("[DscAutoReg] Failed to DER-encode certificate");
            return result;
        }

        // Convert DER bytes to hex string for bytea/BLOB storage
        std::string dbType = queryExecutor_->getDatabaseType();
        std::ostringstream hexStream;
        hexStream << common::db::hexPrefix(dbType);
        for (auto b : derBytes) {
            hexStream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
        }
        std::string certDataHex = hexStream.str();

        // Extract X.509 metadata
        std::string signatureAlgorithm;
        int sigNid = X509_get_signature_nid(dscCert);
        if (sigNid != NID_undef) {
            signatureAlgorithm = OBJ_nid2sn(sigNid);
        }

        std::string publicKeyAlgorithm;
        int publicKeySize = 0;
        EVP_PKEY* pkey = X509_get0_pubkey(dscCert);
        if (pkey) {
            int pkeyType = EVP_PKEY_base_id(pkey);
            if (pkeyType == EVP_PKEY_RSA) publicKeyAlgorithm = "RSA";
            else if (pkeyType == EVP_PKEY_EC) publicKeyAlgorithm = "ECDSA";
            else if (pkeyType == EVP_PKEY_DSA) publicKeyAlgorithm = "DSA";
            else publicKeyAlgorithm = OBJ_nid2sn(pkeyType);
            publicKeySize = EVP_PKEY_bits(pkey);
        }

        bool isSelfSigned = (subjectDn == issuerDn);

        // Compute validation_status from dates
        std::string validationStatus = "UNKNOWN";
        {
            time_t now = time(nullptr);
            const ASN1_TIME* nb = X509_get0_notBefore(dscCert);
            const ASN1_TIME* na = X509_get0_notAfter(dscCert);
            if (nb && na) {
                int daysBefore = 0, secsBefore = 0;
                int daysAfter = 0, secsAfter = 0;
                ASN1_TIME* nowAsn1 = ASN1_TIME_set(nullptr, now);
                if (nowAsn1) {
                    ASN1_TIME_diff(&daysBefore, &secsBefore, nb, nowAsn1);
                    ASN1_TIME_diff(&daysAfter, &secsAfter, nowAsn1, na);
                    ASN1_STRING_free(nowAsn1);
                    if (daysBefore < 0 || (daysBefore == 0 && secsBefore < 0)) {
                        validationStatus = "NOT_YET_VALID";
                    } else if (daysAfter < 0 || (daysAfter == 0 && secsAfter < 0)) {
                        validationStatus = "EXPIRED";
                    } else {
                        validationStatus = "VALID";
                    }
                }
            }
        }

        // 5. Insert into pending_dsc_registration table
        std::string newId;

        if (dbType == "oracle") {
            newId = generateUuid();

            std::string insertQuery =
                "INSERT INTO pending_dsc_registration ("
                "id, fingerprint_sha256, country_code, "
                "subject_dn, issuer_dn, serial_number, "
                "not_before, not_after, certificate_data, "
                "signature_algorithm, public_key_algorithm, public_key_size, "
                "is_self_signed, validation_status, "
                "pa_verification_id, verification_status, "
                "status, created_at"
                ") VALUES ("
                "$1, $2, $3, $4, $5, $6, "
                "CASE WHEN $7 IS NULL OR $7 = '' THEN NULL ELSE TO_TIMESTAMP($7, 'YYYY-MM-DD HH24:MI:SS') END, "
                "CASE WHEN $8 IS NULL OR $8 = '' THEN NULL ELSE TO_TIMESTAMP($8, 'YYYY-MM-DD HH24:MI:SS') END, "
                "$9, $10, $11, $12, "
                "$13, $14, "
                "$15, $16, "
                "'PENDING', SYSTIMESTAMP"
                ")";

            std::vector<std::string> insertParams = {
                newId,               // $1
                result.fingerprint,  // $2
                countryCode,         // $3
                subjectDn,           // $4
                issuerDn,            // $5
                serialNumber,        // $6
                notBefore,           // $7
                notAfter,            // $8
                certDataHex,         // $9
                signatureAlgorithm,  // $10
                publicKeyAlgorithm,  // $11
                std::to_string(publicKeySize), // $12
                common::db::boolLiteral("oracle", isSelfSigned), // $13
                validationStatus,    // $14
                verificationId,      // $15
                verificationStatus   // $16
            };

            queryExecutor_->executeCommand(insertQuery, insertParams);

        } else {
            // PostgreSQL
            const char* insertQuery =
                "INSERT INTO pending_dsc_registration ("
                "fingerprint_sha256, country_code, "
                "subject_dn, issuer_dn, serial_number, "
                "not_before, not_after, certificate_data, "
                "signature_algorithm, public_key_algorithm, public_key_size, "
                "is_self_signed, validation_status, "
                "pa_verification_id, verification_status, "
                "status, created_at"
                ") VALUES ("
                "$1, $2, $3, $4, $5, "
                "$6, $7, $8, "
                "$9, $10, $11, "
                "$12, $13, "
                "$14, $15, "
                "'PENDING', CURRENT_TIMESTAMP"
                ") RETURNING id";

            std::vector<std::string> insertParams = {
                result.fingerprint,  // $1
                countryCode,         // $2
                subjectDn,           // $3
                issuerDn,            // $4
                serialNumber,        // $5
                notBefore,           // $6
                notAfter,            // $7
                certDataHex,         // $8
                signatureAlgorithm,  // $9
                publicKeyAlgorithm,  // $10
                std::to_string(publicKeySize), // $11
                common::db::boolLiteral("postgres", isSelfSigned),  // $12
                validationStatus,    // $13
                verificationId,      // $14
                verificationStatus   // $15
            };

            Json::Value insertResult = queryExecutor_->executeQuery(insertQuery, insertParams);
            if (!insertResult.empty()) {
                newId = insertResult[0]["id"].asString();
            }
        }

        result.success = true;
        result.newlyRegistered = true;
        result.pendingApproval = true;
        result.pendingId = newId;

        spdlog::info("[DscAutoReg] DSC saved to pending: id={}, country={}, fingerprint={}..., verificationId={}",
            newId.substr(0, 8), countryCode, result.fingerprint.substr(0, 16), verificationId.substr(0, 8));

    } catch (const std::exception& e) {
        std::string errMsg = e.what();
        // Race condition: concurrent PA requests may both pass the SELECT check and race to INSERT.
        // Detect unique violation and re-query to return the existing pending entry.
        bool isUniqueViolation = errMsg.find("ORA-00001") != std::string::npos ||
                                 errMsg.find("23505") != std::string::npos ||
                                 errMsg.find("unique constraint") != std::string::npos ||
                                 errMsg.find("UNIQUE constraint") != std::string::npos;

        if (isUniqueViolation && !result.fingerprint.empty()) {
            try {
                const char* recheck =
                    "SELECT id, status FROM pending_dsc_registration "
                    "WHERE fingerprint_sha256 = $1 "
                    "FETCH FIRST 1 ROWS ONLY";
                Json::Value existing = queryExecutor_->executeQuery(recheck, {result.fingerprint});
                if (!existing.empty()) {
                    result.success = true;
                    result.pendingId = existing[0]["id"].asString();
                    result.pendingApproval = (existing[0]["status"].asString() == "PENDING");
                    spdlog::debug("[DscAutoReg] Concurrent insert resolved: DSC already pending, id={}, fingerprint={}...",
                        result.pendingId.substr(0, 8), result.fingerprint.substr(0, 16));
                    return result;
                }
            } catch (...) {}
        }

        spdlog::error("[DscAutoReg] Failed to save pending DSC: {}", e.what());
        // Don't rethrow — pending save failure should not affect PA verification
    }

    return result;
}

std::string DscAutoRegistrationService::computeFingerprint(X509* cert) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;

    if (!X509_digest(cert, EVP_sha256(), digest, &digestLen)) {
        return "";
    }

    std::ostringstream ss;
    for (unsigned int i = 0; i < digestLen; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
    }
    return ss.str();
}

std::vector<uint8_t> DscAutoRegistrationService::getDerBytes(X509* cert) {
    unsigned char* buf = nullptr;
    int len = i2d_X509(cert, &buf);
    if (len <= 0 || !buf) {
        return {};
    }
    std::vector<uint8_t> result(buf, buf + len);
    OPENSSL_free(buf);
    return result;
}

std::string DscAutoRegistrationService::x509NameToString(X509_NAME* name) {
    if (!name) return "";
    char buf[2048];
    X509_NAME_oneline(name, buf, sizeof(buf));
    return std::string(buf);
}

std::string DscAutoRegistrationService::asn1TimeToString(const ASN1_TIME* t) {
    if (!t) return "";

    const char* str = reinterpret_cast<const char*>(t->data);
    size_t len = t->length;
    struct tm tm = {};

    if (t->type == V_ASN1_UTCTIME && len >= 12) {
        int year = (str[0] - '0') * 10 + (str[1] - '0');
        tm.tm_year = (year >= 50 ? 1900 : 2000) + year - 1900;
        tm.tm_mon = (str[2] - '0') * 10 + (str[3] - '0') - 1;
        tm.tm_mday = (str[4] - '0') * 10 + (str[5] - '0');
        tm.tm_hour = (str[6] - '0') * 10 + (str[7] - '0');
        tm.tm_min = (str[8] - '0') * 10 + (str[9] - '0');
        tm.tm_sec = (str[10] - '0') * 10 + (str[11] - '0');
    } else if (t->type == V_ASN1_GENERALIZEDTIME && len >= 14) {
        tm.tm_year = (str[0] - '0') * 1000 + (str[1] - '0') * 100 +
                     (str[2] - '0') * 10 + (str[3] - '0') - 1900;
        tm.tm_mon = (str[4] - '0') * 10 + (str[5] - '0') - 1;
        tm.tm_mday = (str[6] - '0') * 10 + (str[7] - '0');
        tm.tm_hour = (str[8] - '0') * 10 + (str[9] - '0');
        tm.tm_min = (str[10] - '0') * 10 + (str[11] - '0');
        tm.tm_sec = (str[12] - '0') * 10 + (str[13] - '0');
    } else {
        return "";
    }

    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &tm);
    return std::string(buffer);
}

std::string DscAutoRegistrationService::generateUuid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 15);
    std::uniform_int_distribution<> dis2(8, 11);

    std::ostringstream ss;
    ss << std::hex;
    for (int i = 0; i < 8; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 4; i++) ss << dis(gen);
    ss << "-4";
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    ss << dis2(gen);
    for (int i = 0; i < 3; i++) ss << dis(gen);
    ss << "-";
    for (int i = 0; i < 12; i++) ss << dis(gen);
    return ss.str();
}

} // namespace services
