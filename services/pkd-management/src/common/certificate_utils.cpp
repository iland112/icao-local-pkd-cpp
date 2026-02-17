/**
 * @file certificate_utils.cpp
 * @brief X.509 certificate parsing, database management, and LDAP storage implementation
 */

#include "certificate_utils.h"
#include "x509_metadata_extractor.h"
#include "../infrastructure/service_container.h"
#include "../repositories/certificate_repository.h"
#include <spdlog/spdlog.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/cms.h>
#include <openssl/err.h>
#include <cstring>
#include <sstream>
#include <regex>
#include <algorithm>
#include <iomanip>

// Global service container (defined in main.cpp)
extern infrastructure::ServiceContainer* g_services;

namespace certificate_utils {

std::pair<std::string, bool> saveCertificateWithDuplicateCheck(
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
    // Delegate to CertificateRepository
    if (!g_services->certificateRepository()) {
        spdlog::error("[CertUtils] certificateRepository is null");
        return std::make_pair(std::string(""), false);
    }

    return g_services->certificateRepository()->saveCertificateWithDuplicateCheck(
        uploadId, certType, countryCode, subjectDn, issuerDn,
        serialNumber, fingerprint, notBefore, notAfter, certData,
        validationStatus, validationMessage
    );
}

bool trackCertificateDuplicate(
    const std::string& certificateId,
    const std::string& uploadId,
    const std::string& sourceType,
    const std::string& sourceCountry,
    const std::string& sourceEntryDn,
    const std::string& sourceFileName
) {
    // Delegate to CertificateRepository
    if (!g_services->certificateRepository()) {
        spdlog::error("[CertUtils] certificateRepository is null");
        return false;
    }

    return g_services->certificateRepository()->trackCertificateDuplicate(
        certificateId, uploadId, sourceType, sourceCountry,
        sourceEntryDn, sourceFileName
    );
}

bool incrementDuplicateCount(
    const std::string& certificateId,
    const std::string& uploadId
) {
    // Delegate to CertificateRepository
    if (!g_services->certificateRepository()) {
        spdlog::error("[CertUtils] certificateRepository is null");
        return false;
    }

    return g_services->certificateRepository()->incrementDuplicateCount(certificateId, uploadId);
}

bool updateCscaExtractionStats(
    const std::string& uploadId,
    int extractedCount,
    int duplicateCount
) {
    // TODO: Add updateCscaExtractionStats() method to UploadRepository
    // This method updates csca_extracted_from_ml and csca_duplicates fields
    // SQL: UPDATE uploaded_file
    //      SET csca_extracted_from_ml = csca_extracted_from_ml + extractedCount,
    //          csca_duplicates = csca_duplicates + duplicateCount
    //      WHERE id = uploadId

    if (!g_services->uploadRepository()) {
        spdlog::error("[CertUtils] uploadRepository is null");
        return false;
    }

    // Stub implementation - method needs to be added to UploadRepository
    spdlog::warn("[CertUtils] updateCscaExtractionStats stub called - needs UploadRepository::updateCscaExtractionStats() method");
    spdlog::info("[CertUtils] Would update CSCA extraction stats: upload={}, extracted={}, duplicates={}",
                uploadId.substr(0, 8) + "...", extractedCount, duplicateCount);

    return true;  // Return true to avoid breaking existing flow
}

bool updateCertificateLdapStatus(
    const std::string& certificateId,
    const std::string& ldapDn
) {
    // Delegate to CertificateRepository
    // This method updates stored_in_ldap, ldap_dn_v2, and stored_at fields

    if (!g_services->certificateRepository()) {
        spdlog::error("[CertUtils] certificateRepository is null");
        return false;
    }

    return g_services->certificateRepository()->updateCertificateLdapStatus(certificateId, ldapDn);
}

std::string getSourceType(const std::string& fileFormat) {
    if (fileFormat == "LDIF_001") return "LDIF_001";
    if (fileFormat == "LDIF_002") return "LDIF_002";
    if (fileFormat == "LDIF_003") return "LDIF_003";
    if (fileFormat == "MASTERLIST") return "ML_FILE";
    return "UNKNOWN";
}

// --- X.509 Certificate Parsing Utilities Implementation ---

std::string x509NameToString(X509_NAME* name) {
    if (!name) return "";

    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) return "";

    // Use RFC 2253 format (CN=Test,O=Org,C=US)
    X509_NAME_print_ex(bio, name, 0, XN_FLAG_RFC2253);

    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string result(data, len);
    BIO_free(bio);

    return result;
}

std::string asn1IntegerToHex(const ASN1_INTEGER* asn1Int) {
    if (!asn1Int) return "";

    BIGNUM* bn = ASN1_INTEGER_to_BN(asn1Int, nullptr);
    if (!bn) return "";

    char* hex = BN_bn2hex(bn);
    std::string result(hex);
    OPENSSL_free(hex);
    BN_free(bn);

    return result;
}

std::string asn1TimeToIso8601(const ASN1_TIME* asn1Time) {
    if (!asn1Time) return "";

    struct tm tm = {};
    const char* str = reinterpret_cast<const char*>(asn1Time->data);
    size_t len = asn1Time->length;

    // Parse UTCTIME (YYMMDDHHMMSSZ)
    if (asn1Time->type == V_ASN1_UTCTIME && len >= 12) {
        int year = (str[0] - '0') * 10 + (str[1] - '0');
        tm.tm_year = (year >= 50 ? 1900 : 2000) + year - 1900;
        tm.tm_mon = (str[2] - '0') * 10 + (str[3] - '0') - 1;
        tm.tm_mday = (str[4] - '0') * 10 + (str[5] - '0');
        tm.tm_hour = (str[6] - '0') * 10 + (str[7] - '0');
        tm.tm_min = (str[8] - '0') * 10 + (str[9] - '0');
        tm.tm_sec = (str[10] - '0') * 10 + (str[11] - '0');
    }
    // Parse GENERALIZEDTIME (YYYYMMDDHHMMSSZ)
    else if (asn1Time->type == V_ASN1_GENERALIZEDTIME && len >= 14) {
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

    // Format as ISO 8601 (YYYY-MM-DDTHH:MM:SS)
    char buffer[32];
    strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &tm);
    return std::string(buffer);
}

std::string extractCountryCode(const std::string& dn) {
    // Match C= followed by 2-3 letter country code
    // Supports: /C=KR/, C=KR,, or C=KR at end/start
    static const std::regex countryRegex(R"((?:^|[/,]\s*)C=([A-Z]{2,3})(?:[/,\s]|$))", std::regex::icase);
    std::smatch match;
    if (std::regex_search(dn, match, countryRegex)) {
        std::string country = match[1].str();
        // Convert to uppercase
        std::transform(country.begin(), country.end(), country.begin(), ::toupper);
        return country;
    }
    return "XX";  // Default unknown country
}

std::string computeSha256Fingerprint(X509* cert) {
    if (!cert) return "";

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;

    if (!X509_digest(cert, EVP_sha256(), digest, &digestLen)) {
        return "";
    }

    // Convert to hex string
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digestLen; i++) {
        ss << std::setw(2) << static_cast<int>(digest[i]);
    }

    return ss.str();
}

std::string computeSha1Fingerprint(X509* cert) {
    if (!cert) return "";

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;

    if (!X509_digest(cert, EVP_sha1(), digest, &digestLen)) {
        return "";
    }

    // Convert to hex string
    std::stringstream ss;
    ss << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < digestLen; i++) {
        ss << std::setw(2) << static_cast<int>(digest[i]);
    }

    return ss.str();
}

bool isExpired(X509* cert) {
    if (!cert) return true;

    // Get current time
    time_t now = time(nullptr);

    // Check if certificate is expired
    const ASN1_TIME* notAfter = X509_get0_notAfter(cert);
    if (X509_cmp_time(notAfter, &now) < 0) {
        return true;  // notAfter is before now → expired
    }

    return false;
}

bool isLinkCertificate(X509* cert) {
    if (!cert) return false;

    // Link certificate must be CA
    BASIC_CONSTRAINTS* bc = (BASIC_CONSTRAINTS*)X509_get_ext_d2i(
        cert, NID_basic_constraints, nullptr, nullptr);

    if (!bc) return false;

    bool is_ca = (bc->ca != 0);
    BASIC_CONSTRAINTS_free(bc);

    if (!is_ca) return false;

    // Link certificate must NOT be self-signed
    X509_NAME* subject = X509_get_subject_name(cert);
    X509_NAME* issuer = X509_get_issuer_name(cert);

    bool is_self_signed = (X509_NAME_cmp(subject, issuer) == 0);

    return !is_self_signed;  // CA && not self-signed = link certificate
}

std::string extractAsn1Text(X509* cert) {
    if (!cert) return "";

    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) return "";

    // Print certificate in human-readable format
    // This includes all fields, extensions, signature, etc.
    X509_print_ex(bio, cert, XN_FLAG_COMPAT, X509_FLAG_COMPAT);

    char* data = nullptr;
    long len = BIO_get_mem_data(bio, &data);
    std::string result(data, len);
    BIO_free(bio);

    return result;
}

std::string extractAsn1TextFromPem(const std::vector<uint8_t>& pemData) {
    if (pemData.empty()) {
        return "Error: Empty PEM data";
    }

    // Create BIO from memory buffer
    BIO* bio = BIO_new_mem_buf(pemData.data(), static_cast<int>(pemData.size()));
    if (!bio) {
        return "Error: Failed to create BIO from PEM data";
    }

    // Read PEM-encoded certificate
    X509* cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    BIO_free(bio);

    if (!cert) {
        unsigned long err = ERR_get_error();
        char errBuf[256];
        ERR_error_string_n(err, errBuf, sizeof(errBuf));
        return std::string("Error: Failed to parse PEM certificate - ") + errBuf;
    }

    // Extract ASN.1 text using existing function
    std::string result = extractAsn1Text(cert);
    X509_free(cert);

    return result;
}

std::string extractAsn1TextFromDer(const std::vector<uint8_t>& derData) {
    if (derData.empty()) {
        return "Error: Empty DER data";
    }

    // Parse DER-encoded certificate
    const unsigned char* p = derData.data();
    X509* cert = d2i_X509(nullptr, &p, static_cast<long>(derData.size()));

    if (!cert) {
        unsigned long err = ERR_get_error();
        char errBuf[256];
        ERR_error_string_n(err, errBuf, sizeof(errBuf));
        return std::string("Error: Failed to parse DER certificate - ") + errBuf;
    }

    // Extract ASN.1 text using existing function
    std::string result = extractAsn1Text(cert);
    X509_free(cert);

    return result;
}

std::string extractCmsAsn1Text(const std::vector<uint8_t>& cmsData) {
    if (cmsData.empty()) {
        return "Error: Empty CMS data";
    }

    // Create BIO from memory buffer
    BIO* bio = BIO_new_mem_buf(cmsData.data(), static_cast<int>(cmsData.size()));
    if (!bio) {
        return "Error: Failed to create BIO from CMS data";
    }

    // Try to parse as CMS ContentInfo (DER format)
    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (!cms) {
        // Try parsing as PEM-encoded CMS
        bio = BIO_new_mem_buf(cmsData.data(), static_cast<int>(cmsData.size()));
        cms = PEM_read_bio_CMS(bio, nullptr, nullptr, nullptr);
        BIO_free(bio);

        if (!cms) {
            unsigned long err = ERR_get_error();
            char errBuf[256];
            ERR_error_string_n(err, errBuf, sizeof(errBuf));
            return std::string("Error: Failed to parse CMS SignedData - ") + errBuf;
        }
    }

    // Create output BIO for CMS structure
    BIO* outBio = BIO_new(BIO_s_mem());
    if (!outBio) {
        CMS_ContentInfo_free(cms);
        return "Error: Failed to create output BIO";
    }

    // Print CMS structure in human-readable format
    CMS_ContentInfo_print_ctx(outBio, cms, 0, nullptr);

    // Also print embedded certificates
    STACK_OF(X509)* certs = CMS_get1_certs(cms);
    if (certs && sk_X509_num(certs) > 0) {
        BIO_printf(outBio, "\n\n=== Embedded Certificates (%d) ===\n\n", sk_X509_num(certs));

        for (int i = 0; i < sk_X509_num(certs); i++) {
            X509* cert = sk_X509_value(certs, i);
            if (cert) {
                BIO_printf(outBio, "--- Certificate %d ---\n", i + 1);
                X509_print_ex(outBio, cert, XN_FLAG_COMPAT, X509_FLAG_COMPAT);
                BIO_printf(outBio, "\n");
            }
        }
        sk_X509_pop_free(certs, X509_free);
    }

    // Extract result
    char* data = nullptr;
    long len = BIO_get_mem_data(outBio, &data);
    std::string result(data, len);

    BIO_free(outBio);
    CMS_ContentInfo_free(cms);

    return result;
}

std::string extractAsn1TextAuto(const std::vector<uint8_t>& fileData) {
    if (fileData.empty()) {
        return "Error: Empty file data";
    }

    // Detection 1: Check for PEM markers
    std::string dataStr(fileData.begin(), fileData.end());
    if (dataStr.find("-----BEGIN CERTIFICATE-----") != std::string::npos ||
        dataStr.find("-----BEGIN PKCS7-----") != std::string::npos ||
        dataStr.find("-----BEGIN CMS-----") != std::string::npos) {

        // Try PEM format
        std::string result = extractAsn1TextFromPem(fileData);
        if (result.find("Error:") != 0) {
            return "Format: PEM\n\n" + result;
        }
    }

    // Detection 2: Try CMS SignedData (Master List)
    std::string cmsResult = extractCmsAsn1Text(fileData);
    if (cmsResult.find("Error:") != 0) {
        return "Format: CMS SignedData (Master List)\n\n" + cmsResult;
    }

    // Detection 3: Try DER-encoded X.509 certificate
    std::string derResult = extractAsn1TextFromDer(fileData);
    if (derResult.find("Error:") != 0) {
        return "Format: DER/CER/BIN\n\n" + derResult;
    }

    // All formats failed
    return "Error: Unable to detect format. Tried PEM, CMS, and DER formats.\n"
           "File size: " + std::to_string(fileData.size()) + " bytes\n"
           "First 16 bytes (hex): " + [&]() {
               std::stringstream ss;
               ss << std::hex << std::setfill('0');
               int limit = std::min(16, static_cast<int>(fileData.size()));
               for (int i = 0; i < limit; i++) {
                   ss << std::setw(2) << static_cast<int>(fileData[i]) << " ";
               }
               return ss.str();
           }();
}

} // namespace certificate_utils
