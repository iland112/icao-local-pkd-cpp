/**
 * @file lc_validator.cpp
 * @brief Link Certificate (LC) trust chain validation implementation
 *
 * Sprint 2: Link Certificate Validation Core
 * Implements ICAO Doc 9303 Part 12 Link Certificate validation
 *
 * @version 1.0.0
 * @date 2026-01-24
 */

#include "lc_validator.h"
#include <spdlog/spdlog.h>
#include <openssl/bn.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <regex>

namespace lc {

LcValidator::LcValidator(PGconn* conn) : conn_(conn) {
    if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
        throw std::runtime_error("Invalid PostgreSQL connection");
    }

    // Initialize CRL validator
    crlValidator_ = std::make_unique<crl::CrlValidator>(conn_);
}

LcValidationResult LcValidator::validateLinkCertificate(
    const std::vector<uint8_t>& linkCertBinary
) {
    // Parse DER binary to X509
    const unsigned char* p = linkCertBinary.data();
    X509* linkCert = d2i_X509(nullptr, &p, linkCertBinary.size());

    if (!linkCert) {
        spdlog::error("[LcValidator] Failed to parse LC DER binary");

        LcValidationResult result{};
        result.trustChainValid = false;
        result.validationMessage = "Failed to parse certificate binary";
        return result;
    }

    // Validate using X509 object
    LcValidationResult result = validateLinkCertificate(linkCert);

    X509_free(linkCert);
    return result;
}

LcValidationResult LcValidator::validateLinkCertificate(X509* linkCert) {
    auto startTime = std::chrono::high_resolution_clock::now();

    LcValidationResult result{};
    result.trustChainValid = false;
    result.oldCscaSignatureValid = false;
    result.newCscaSignatureValid = false;
    result.validityPeriodValid = false;
    result.extensionsValid = false;
    result.revocationStatus = crl::RevocationStatus::UNKNOWN;

    // Step 1: Extract LC metadata
    std::string subjectDn = extractSubjectDn(linkCert);
    std::string issuerDn = extractIssuerDn(linkCert);
    std::string serialNumber = extractSerialNumber(linkCert);
    std::string fingerprint = extractFingerprint(linkCert);

    spdlog::info("[LcValidator] Validating LC: Subject={}, Issuer={}, Serial={}",
                 subjectDn, issuerDn, serialNumber);

    // Step 2: Find old CSCA (issuer of LC)
    X509* oldCsca = findCscaBySubjectDn(issuerDn);
    if (!oldCsca) {
        result.validationMessage = "Old CSCA not found (issuer: " + issuerDn + ")";
        spdlog::warn("[LcValidator] {}", result.validationMessage);

        auto endTime = std::chrono::high_resolution_clock::now();
        result.validationDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();
        return result;
    }

    result.oldCscaSubjectDn = extractSubjectDn(oldCsca);
    result.oldCscaFingerprint = extractFingerprint(oldCsca);

    spdlog::info("[LcValidator] Found old CSCA: {}", result.oldCscaSubjectDn);

    // Step 3: Verify LC signature by old CSCA
    EVP_PKEY* oldCscaPubKey = X509_get_pubkey(oldCsca);
    if (!oldCscaPubKey) {
        X509_free(oldCsca);
        result.validationMessage = "Failed to extract old CSCA public key";
        spdlog::error("[LcValidator] {}", result.validationMessage);

        auto endTime = std::chrono::high_resolution_clock::now();
        result.validationDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();
        return result;
    }

    result.oldCscaSignatureValid = verifyCertificateSignature(linkCert, oldCscaPubKey);
    EVP_PKEY_free(oldCscaPubKey);

    if (!result.oldCscaSignatureValid) {
        X509_free(oldCsca);
        result.validationMessage = "LC signature verification failed (old CSCA)";
        spdlog::warn("[LcValidator] {}", result.validationMessage);

        auto endTime = std::chrono::high_resolution_clock::now();
        result.validationDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();
        return result;
    }

    spdlog::info("[LcValidator] ✓ LC signature valid (verified by old CSCA)");

    // Step 4: Find new CSCA (certificate signed by LC)
    X509* newCsca = findCscaByIssuerDn(subjectDn);
    if (!newCsca) {
        X509_free(oldCsca);
        result.validationMessage = "New CSCA not found (issuer should be: " + subjectDn + ")";
        spdlog::warn("[LcValidator] {}", result.validationMessage);

        auto endTime = std::chrono::high_resolution_clock::now();
        result.validationDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();
        return result;
    }

    result.newCscaSubjectDn = extractSubjectDn(newCsca);
    result.newCscaFingerprint = extractFingerprint(newCsca);

    spdlog::info("[LcValidator] Found new CSCA: {}", result.newCscaSubjectDn);

    // Step 5: Verify new CSCA signature by LC
    EVP_PKEY* linkCertPubKey = X509_get_pubkey(linkCert);
    if (!linkCertPubKey) {
        X509_free(oldCsca);
        X509_free(newCsca);
        result.validationMessage = "Failed to extract LC public key";
        spdlog::error("[LcValidator] {}", result.validationMessage);

        auto endTime = std::chrono::high_resolution_clock::now();
        result.validationDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();
        return result;
    }

    result.newCscaSignatureValid = verifyCertificateSignature(newCsca, linkCertPubKey);
    EVP_PKEY_free(linkCertPubKey);

    if (!result.newCscaSignatureValid) {
        X509_free(oldCsca);
        X509_free(newCsca);
        result.validationMessage = "New CSCA signature verification failed (LC)";
        spdlog::warn("[LcValidator] {}", result.validationMessage);

        auto endTime = std::chrono::high_resolution_clock::now();
        result.validationDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();
        return result;
    }

    spdlog::info("[LcValidator] ✓ New CSCA signature valid (verified by LC)");

    X509_free(oldCsca);
    X509_free(newCsca);

    // Step 6: Check validity period
    result.validityPeriodValid = checkValidityPeriod(linkCert);
    result.notBefore = asn1TimeToIso8601(X509_getm_notBefore(linkCert));
    result.notAfter = asn1TimeToIso8601(X509_getm_notAfter(linkCert));

    if (!result.validityPeriodValid) {
        result.validationMessage = "LC expired or not yet valid";
        spdlog::warn("[LcValidator] {} (notBefore={}, notAfter={})",
                     result.validationMessage, result.notBefore, result.notAfter);

        auto endTime = std::chrono::high_resolution_clock::now();
        result.validationDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();
        return result;
    }

    spdlog::info("[LcValidator] ✓ Validity period valid");

    // Step 7: Validate certificate extensions
    result.extensionsValid = validateLcExtensions(linkCert);

    // Extract extension details
    auto basicConstraints = getBasicConstraints(linkCert);
    if (basicConstraints) {
        auto [isCa, pathlen] = *basicConstraints;
        result.basicConstraintsCa = isCa;
        result.basicConstraintsPathlen = pathlen;
    }

    result.keyUsage = getKeyUsage(linkCert);
    result.extendedKeyUsage = getExtendedKeyUsage(linkCert);

    if (!result.extensionsValid) {
        result.validationMessage = "LC extensions validation failed";
        spdlog::warn("[LcValidator] {}", result.validationMessage);

        auto endTime = std::chrono::high_resolution_clock::now();
        result.validationDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();
        return result;
    }

    spdlog::info("[LcValidator] ✓ Extensions valid (CA={}, pathlen={}, keyUsage={})",
                 result.basicConstraintsCa, result.basicConstraintsPathlen, result.keyUsage);

    // Step 8: Check CRL revocation status
    auto crlResult = crlValidator_->checkRevocation(
        "",  // certificateId (empty for now - not yet stored)
        "LC",
        serialNumber,
        fingerprint,
        issuerDn
    );

    result.revocationStatus = crlResult.status;
    result.revocationMessage = crlResult.message;

    if (result.revocationStatus == crl::RevocationStatus::REVOKED) {
        result.validationMessage = "LC is revoked: " + result.revocationMessage;
        spdlog::warn("[LcValidator] {}", result.validationMessage);

        auto endTime = std::chrono::high_resolution_clock::now();
        result.validationDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            endTime - startTime).count();
        return result;
    }

    spdlog::info("[LcValidator] ✓ CRL check: {}", result.revocationMessage);

    // Step 9: Final result
    result.trustChainValid =
        result.oldCscaSignatureValid &&
        result.newCscaSignatureValid &&
        result.validityPeriodValid &&
        result.extensionsValid &&
        (result.revocationStatus != crl::RevocationStatus::REVOKED);

    if (result.trustChainValid) {
        result.validationMessage = "LC trust chain valid";
        spdlog::info("[LcValidator] ✓✓✓ {} ✓✓✓", result.validationMessage);
    } else {
        result.validationMessage = "LC trust chain validation failed";
        spdlog::warn("[LcValidator] {}", result.validationMessage);
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    result.validationDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime).count();

    return result;
}

std::string LcValidator::storeLinkCertificate(
    X509* linkCert,
    const LcValidationResult& validationResult,
    const std::string& uploadId
) {
    // Extract certificate metadata
    std::string subjectDn = extractSubjectDn(linkCert);
    std::string issuerDn = extractIssuerDn(linkCert);
    std::string serialNumber = extractSerialNumber(linkCert);
    std::string fingerprint = extractFingerprint(linkCert);
    std::string countryCode = extractCountryCode(subjectDn);
    std::string notBefore = asn1TimeToIso8601(X509_getm_notBefore(linkCert));
    std::string notAfter = asn1TimeToIso8601(X509_getm_notAfter(linkCert));

    // Get certificate binary
    std::vector<uint8_t> certBinary = getCertificateDer(linkCert);

    // Escape bytea
    unsigned char* byteaEscaped = PQescapeByteaConn(
        conn_,
        reinterpret_cast<const unsigned char*>(certBinary.data()),
        certBinary.size(),
        nullptr
    );

    if (!byteaEscaped) {
        spdlog::error("[LcValidator] Failed to escape certificate binary");
        return "";
    }

    std::string byteaStr = reinterpret_cast<const char*>(byteaEscaped);
    PQfreemem(byteaEscaped);

    // Prepare INSERT query
    const char* query =
        "INSERT INTO link_certificate ("
        "    upload_id, subject_dn, issuer_dn, serial_number, fingerprint_sha256, "
        "    not_before, not_after, country_code, "
        "    old_csca_subject_dn, old_csca_fingerprint, "
        "    new_csca_subject_dn, new_csca_fingerprint, "
        "    trust_chain_valid, old_csca_signature_valid, new_csca_signature_valid, "
        "    validity_period_valid, extensions_valid, "
        "    revocation_status, validation_message, validation_timestamp, "
        "    basic_constraints_ca, basic_constraints_pathlen, key_usage, extended_key_usage, "
        "    certificate_binary, created_at"
        ") VALUES ("
        "    $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, "
        "    $11, $12, $13, $14, $15, $16, $17, $18, $19, NOW(), "
        "    $20, $21, $22, $23, $24, NOW()"
        ") RETURNING id";

    std::string trustChainValidStr = validationResult.trustChainValid ? "true" : "false";
    std::string oldCscaSigValidStr = validationResult.oldCscaSignatureValid ? "true" : "false";
    std::string newCscaSigValidStr = validationResult.newCscaSignatureValid ? "true" : "false";
    std::string validityValidStr = validationResult.validityPeriodValid ? "true" : "false";
    std::string extensionsValidStr = validationResult.extensionsValid ? "true" : "false";
    std::string revocationStatusStr = crl::revocationStatusToString(validationResult.revocationStatus);
    std::string basicConstraintsCaStr = validationResult.basicConstraintsCa ? "true" : "false";
    std::string basicConstraintsPathlenStr = std::to_string(validationResult.basicConstraintsPathlen);

    const char* paramValues[24] = {
        uploadId.empty() ? nullptr : uploadId.c_str(),
        subjectDn.c_str(),
        issuerDn.c_str(),
        serialNumber.c_str(),
        fingerprint.c_str(),
        notBefore.c_str(),
        notAfter.c_str(),
        countryCode.empty() ? nullptr : countryCode.c_str(),
        validationResult.oldCscaSubjectDn.c_str(),
        validationResult.oldCscaFingerprint.c_str(),
        validationResult.newCscaSubjectDn.c_str(),
        validationResult.newCscaFingerprint.c_str(),
        trustChainValidStr.c_str(),
        oldCscaSigValidStr.c_str(),
        newCscaSigValidStr.c_str(),
        validityValidStr.c_str(),
        extensionsValidStr.c_str(),
        revocationStatusStr.c_str(),
        validationResult.validationMessage.c_str(),
        basicConstraintsCaStr.c_str(),
        basicConstraintsPathlenStr.c_str(),
        validationResult.keyUsage.c_str(),
        validationResult.extendedKeyUsage.c_str(),
        byteaStr.c_str()
    };

    PGresult* res = PQexecParams(conn_, query, 24, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        spdlog::error("[LcValidator] Failed to store LC: {}", PQerrorMessage(conn_));
        PQclear(res);
        return "";
    }

    std::string lcId = PQgetvalue(res, 0, 0);
    PQclear(res);

    spdlog::info("[LcValidator] Stored LC in database: id={}, fingerprint={}",
                 lcId, fingerprint);

    return lcId;
}

// ============================================================================
// Private Helper Methods - Certificate Lookup
// ============================================================================

X509* LcValidator::findCscaBySubjectDn(const std::string& subjectDn) {
    const char* query =
        "SELECT certificate_binary FROM certificate "
        "WHERE certificate_type = 'CSCA' AND subject_dn = $1 "
        "LIMIT 1";

    const char* paramValues[1] = {subjectDn.c_str()};
    PGresult* res = PQexecParams(conn_, query, 1, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return nullptr;
    }

    // Parse certificate binary
    size_t certLen;
    unsigned char* certData = PQunescapeBytea(
        reinterpret_cast<const unsigned char*>(PQgetvalue(res, 0, 0)),
        &certLen
    );

    if (!certData) {
        PQclear(res);
        return nullptr;
    }

    const unsigned char* p = certData;
    X509* cert = d2i_X509(nullptr, &p, certLen);
    PQfreemem(certData);
    PQclear(res);

    return cert;
}

X509* LcValidator::findCscaByIssuerDn(const std::string& issuerDn) {
    const char* query =
        "SELECT certificate_binary FROM certificate "
        "WHERE certificate_type = 'CSCA' AND issuer_dn = $1 "
        "LIMIT 1";

    const char* paramValues[1] = {issuerDn.c_str()};
    PGresult* res = PQexecParams(conn_, query, 1, nullptr, paramValues,
                                 nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        return nullptr;
    }

    // Parse certificate binary
    size_t certLen;
    unsigned char* certData = PQunescapeBytea(
        reinterpret_cast<const unsigned char*>(PQgetvalue(res, 0, 0)),
        &certLen
    );

    if (!certData) {
        PQclear(res);
        return nullptr;
    }

    const unsigned char* p = certData;
    X509* cert = d2i_X509(nullptr, &p, certLen);
    PQfreemem(certData);
    PQclear(res);

    return cert;
}

// ============================================================================
// Private Helper Methods - Certificate Validation
// ============================================================================

bool LcValidator::verifyCertificateSignature(X509* cert, EVP_PKEY* issuerPubKey) {
    int result = X509_verify(cert, issuerPubKey);
    return (result == 1);
}

bool LcValidator::checkValidityPeriod(X509* cert) {
    time_t now = time(nullptr);

    // Check notBefore
    if (X509_cmp_time(X509_getm_notBefore(cert), &now) > 0) {
        return false;  // Not yet valid
    }

    // Check notAfter
    if (X509_cmp_time(X509_getm_notAfter(cert), &now) < 0) {
        return false;  // Expired
    }

    return true;
}

bool LcValidator::validateLcExtensions(X509* cert) {
    // Check BasicConstraints
    auto basicConstraints = getBasicConstraints(cert);
    if (!basicConstraints) {
        spdlog::warn("[LcValidator] BasicConstraints extension missing");
        return false;
    }

    auto [isCa, pathlen] = *basicConstraints;
    if (!isCa) {
        spdlog::warn("[LcValidator] BasicConstraints: CA must be TRUE for LC");
        return false;
    }

    // pathlen:0 is typical for LC (can only sign end-entity certs)
    // but not strictly required, so just log it
    if (pathlen != 0 && pathlen != -1) {
        spdlog::info("[LcValidator] BasicConstraints: pathlen={} (atypical for LC)", pathlen);
    }

    // Check KeyUsage
    std::string keyUsage = getKeyUsage(cert);
    if (keyUsage.find("Certificate Sign") == std::string::npos) {
        spdlog::warn("[LcValidator] KeyUsage: 'Certificate Sign' required for LC");
        return false;
    }

    // SubjectKeyIdentifier and AuthorityKeyIdentifier are recommended but not required
    // Just log if present

    return true;
}

// ============================================================================
// Private Helper Methods - Extension Extraction
// ============================================================================

std::optional<std::tuple<bool, int>> LcValidator::getBasicConstraints(X509* cert) {
    BASIC_CONSTRAINTS* bc = static_cast<BASIC_CONSTRAINTS*>(
        X509_get_ext_d2i(cert, NID_basic_constraints, nullptr, nullptr)
    );

    if (!bc) {
        return std::nullopt;
    }

    bool isCa = (bc->ca != 0);
    int pathlen = -1;

    if (bc->pathlen) {
        pathlen = ASN1_INTEGER_get(bc->pathlen);
    }

    BASIC_CONSTRAINTS_free(bc);

    return std::make_tuple(isCa, pathlen);
}

std::string LcValidator::getKeyUsage(X509* cert) {
    ASN1_BIT_STRING* usage = static_cast<ASN1_BIT_STRING*>(
        X509_get_ext_d2i(cert, NID_key_usage, nullptr, nullptr)
    );

    if (!usage) {
        return "";
    }

    std::vector<std::string> usageList;

    if (ASN1_BIT_STRING_get_bit(usage, 0)) usageList.push_back("Digital Signature");
    if (ASN1_BIT_STRING_get_bit(usage, 1)) usageList.push_back("Non Repudiation");
    if (ASN1_BIT_STRING_get_bit(usage, 2)) usageList.push_back("Key Encipherment");
    if (ASN1_BIT_STRING_get_bit(usage, 3)) usageList.push_back("Data Encipherment");
    if (ASN1_BIT_STRING_get_bit(usage, 4)) usageList.push_back("Key Agreement");
    if (ASN1_BIT_STRING_get_bit(usage, 5)) usageList.push_back("Certificate Sign");
    if (ASN1_BIT_STRING_get_bit(usage, 6)) usageList.push_back("CRL Sign");

    ASN1_BIT_STRING_free(usage);

    std::ostringstream oss;
    for (size_t i = 0; i < usageList.size(); i++) {
        if (i > 0) oss << ", ";
        oss << usageList[i];
    }

    return oss.str();
}

std::string LcValidator::getExtendedKeyUsage(X509* cert) {
    EXTENDED_KEY_USAGE* eku = static_cast<EXTENDED_KEY_USAGE*>(
        X509_get_ext_d2i(cert, NID_ext_key_usage, nullptr, nullptr)
    );

    if (!eku) {
        return "";
    }

    std::vector<std::string> ekuList;

    for (int i = 0; i < sk_ASN1_OBJECT_num(eku); i++) {
        ASN1_OBJECT* obj = sk_ASN1_OBJECT_value(eku, i);
        char buf[128];
        OBJ_obj2txt(buf, sizeof(buf), obj, 0);
        ekuList.push_back(buf);
    }

    EXTENDED_KEY_USAGE_free(eku);

    std::ostringstream oss;
    for (size_t i = 0; i < ekuList.size(); i++) {
        if (i > 0) oss << ", ";
        oss << ekuList[i];
    }

    return oss.str();
}

// ============================================================================
// Private Helper Methods - Certificate Metadata Extraction
// ============================================================================

std::string LcValidator::extractSubjectDn(X509* cert) {
    X509_NAME* name = X509_get_subject_name(cert);
    if (!name) return "";

    BIO* bio = BIO_new(BIO_s_mem());
    X509_NAME_print_ex(bio, name, 0, XN_FLAG_RFC2253);

    char* data;
    long len = BIO_get_mem_data(bio, &data);
    std::string dn(data, len);

    BIO_free(bio);
    return dn;
}

std::string LcValidator::extractIssuerDn(X509* cert) {
    X509_NAME* name = X509_get_issuer_name(cert);
    if (!name) return "";

    BIO* bio = BIO_new(BIO_s_mem());
    X509_NAME_print_ex(bio, name, 0, XN_FLAG_RFC2253);

    char* data;
    long len = BIO_get_mem_data(bio, &data);
    std::string dn(data, len);

    BIO_free(bio);
    return dn;
}

std::string LcValidator::extractSerialNumber(X509* cert) {
    ASN1_INTEGER* serial = X509_get_serialNumber(cert);
    if (!serial) return "";

    BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
    if (!bn) return "";

    char* hex = BN_bn2hex(bn);
    std::string serialHex(hex);
    OPENSSL_free(hex);
    BN_free(bn);

    return serialHex;
}

std::string LcValidator::extractFingerprint(X509* cert) {
    unsigned char md[SHA256_DIGEST_LENGTH];
    unsigned int len;

    if (!X509_digest(cert, EVP_sha256(), md, &len)) {
        return "";
    }

    std::ostringstream oss;
    for (unsigned int i = 0; i < len; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(md[i]);
    }

    return oss.str();
}

std::string LcValidator::extractCountryCode(const std::string& subjectDn) {
    // Parse C= component from DN
    std::regex countryRegex(R"(,?\s*C=([A-Z]{2,3}))", std::regex::icase);
    std::smatch match;

    if (std::regex_search(subjectDn, match, countryRegex)) {
        std::string country = match[1].str();
        std::transform(country.begin(), country.end(), country.begin(), ::toupper);
        return country;
    }

    return "";
}

std::string LcValidator::asn1TimeToIso8601(const ASN1_TIME* asn1Time) {
    if (!asn1Time) return "";

    BIO* bio = BIO_new(BIO_s_mem());
    ASN1_TIME_print(bio, asn1Time);

    char* data;
    long len = BIO_get_mem_data(bio, &data);
    std::string timeStr(data, len);

    BIO_free(bio);

    // OpenSSL format: "Jan  1 00:00:00 2025 GMT"
    // TODO: Convert to ISO 8601 format (YYYY-MM-DDTHH:MM:SSZ)
    // For now, return OpenSSL format
    return timeStr;
}

std::vector<uint8_t> LcValidator::getCertificateDer(X509* cert) {
    int derLen = i2d_X509(cert, nullptr);
    if (derLen <= 0) {
        return {};
    }

    std::vector<uint8_t> derData(derLen);
    unsigned char* derPtr = derData.data();
    i2d_X509(cert, &derPtr);

    return derData;
}

} // namespace lc
