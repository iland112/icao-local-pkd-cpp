/**
 * @file dsc_auto_registration_service.cpp
 * @brief DSC Auto-Registration from PA Verification
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
    spdlog::info("[DscAutoReg] DSC auto-registration service initialized");
}

DscRegistrationResult DscAutoRegistrationService::registerDscFromSod(
    X509* dscCert,
    const std::string& countryCode,
    const std::string& verificationId,
    const std::string& verificationStatus)
{
    DscRegistrationResult result;

    if (!dscCert) {
        spdlog::warn("[DscAutoReg] DSC certificate is null, skipping registration");
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

        // 2. Check if DSC already exists (by type + fingerprint)
        const char* checkQuery =
            "SELECT id FROM certificate "
            "WHERE certificate_type = 'DSC' AND fingerprint_sha256 = $1 "
            "FETCH FIRST 1 ROWS ONLY";

        std::vector<std::string> checkParams = {result.fingerprint};
        Json::Value checkResult = queryExecutor_->executeQuery(checkQuery, checkParams);

        if (!checkResult.empty()) {
            // DSC already registered
            result.success = true;
            result.newlyRegistered = false;
            result.certificateId = checkResult[0]["id"].asString();
            spdlog::debug("[DscAutoReg] DSC already registered: id={}, fingerprint={}...",
                result.certificateId.substr(0, 8), result.fingerprint.substr(0, 16));
            return result;
        }

        // 3. Extract certificate fields
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

        // Convert DER bytes to hex string for bytea storage
        // PostgreSQL: \x for hex bytea; Oracle: \\x as BLOB marker for OracleQueryExecutor
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

        // Source context JSON
        std::string sourceContext = "{\"verificationId\":\"" + verificationId +
            "\",\"verificationStatus\":\"" + verificationStatus + "\"}";

        // 4. Insert new DSC certificate
        // dbType already declared above for hex prefix selection
        std::string newId;

        if (dbType == "oracle") {
            newId = generateUuid();

            std::string insertQuery =
                "INSERT INTO certificate ("
                "id, certificate_type, country_code, "
                "subject_dn, issuer_dn, serial_number, fingerprint_sha256, "
                "not_before, not_after, certificate_data, "
                "validation_status, stored_in_ldap, is_self_signed, "
                "signature_algorithm, public_key_algorithm, public_key_size, "
                "duplicate_count, created_at, "
                "source_type, source_context, extracted_from, registered_at"
                ") VALUES ("
                "$1, 'DSC', $2, $3, $4, $5, $6, "
                "CASE WHEN $7 IS NULL OR $7 = '' THEN NULL ELSE TO_TIMESTAMP($7, 'YYYY-MM-DD HH24:MI:SS') END, "
                "CASE WHEN $8 IS NULL OR $8 = '' THEN NULL ELSE TO_TIMESTAMP($8, 'YYYY-MM-DD HH24:MI:SS') END, "
                "$9, $10, 0, $11, "
                "$12, $13, $14, "
                "0, SYSTIMESTAMP, "
                "'PA_EXTRACTED', $15, $16, SYSTIMESTAMP"
                ")";

            std::vector<std::string> insertParams = {
                newId,           // $1
                countryCode,     // $2
                subjectDn,       // $3
                issuerDn,        // $4
                serialNumber,    // $5
                result.fingerprint, // $6
                notBefore,       // $7
                notAfter,        // $8
                certDataHex,     // $9
                validationStatus, // $10
                common::db::boolLiteral("oracle", isSelfSigned), // $11
                signatureAlgorithm, // $12
                publicKeyAlgorithm, // $13
                std::to_string(publicKeySize), // $14
                sourceContext,   // $15
                verificationId   // $16
            };

            queryExecutor_->executeCommand(insertQuery, insertParams);

        } else {
            // PostgreSQL
            const char* insertQuery =
                "INSERT INTO certificate ("
                "certificate_type, country_code, "
                "subject_dn, issuer_dn, serial_number, fingerprint_sha256, "
                "not_before, not_after, certificate_data, "
                "validation_status, stored_in_ldap, is_self_signed, "
                "signature_algorithm, public_key_algorithm, public_key_size, "
                "duplicate_count, created_at, "
                "source_type, source_context, extracted_from, registered_at"
                ") VALUES ("
                "'DSC', $1, $2, $3, $4, $5, "
                "$6, $7, $8, "
                "$9, FALSE, $10, "
                "$11, $12, $13, "
                "0, CURRENT_TIMESTAMP, "
                "'PA_EXTRACTED', $14::jsonb, $15, CURRENT_TIMESTAMP"
                ") RETURNING id";

            std::vector<std::string> insertParams = {
                countryCode,        // $1
                subjectDn,          // $2
                issuerDn,           // $3
                serialNumber,       // $4
                result.fingerprint, // $5
                notBefore,          // $6
                notAfter,           // $7
                certDataHex,        // $8
                validationStatus,   // $9
                common::db::boolLiteral("postgres", isSelfSigned),  // $10
                signatureAlgorithm, // $11
                publicKeyAlgorithm, // $12
                std::to_string(publicKeySize), // $13
                sourceContext,      // $14
                verificationId      // $15
            };

            Json::Value insertResult = queryExecutor_->executeQuery(insertQuery, insertParams);
            if (!insertResult.empty()) {
                newId = insertResult[0]["id"].asString();
            }
        }

        result.success = true;
        result.newlyRegistered = true;
        result.certificateId = newId;

        spdlog::info("[DscAutoReg] DSC registered: id={}, country={}, fingerprint={}..., source=PA_EXTRACTED, verificationId={}",
            newId.substr(0, 8), countryCode, result.fingerprint.substr(0, 16), verificationId.substr(0, 8));

    } catch (const std::exception& e) {
        spdlog::error("[DscAutoReg] Failed to register DSC: {}", e.what());
        // Don't rethrow — registration failure should not affect PA verification
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

    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) return "";

    ASN1_TIME_print(bio, t);
    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string result(data, len);
    BIO_free(bio);

    // Convert "Jan 15 10:30:00 2024 GMT" → ISO-like for PostgreSQL TIMESTAMP
    // PostgreSQL can parse ASN1_TIME_print output directly
    return result;
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
