/**
 * @file OpenSslCertificateParser.hpp
 * @brief OpenSSL-based X.509 certificate parsing utilities
 */

#pragma once

#include "../../domain/model/CertificateData.hpp"
#include "../../domain/model/CrlData.hpp"
#include "shared/exception/InfrastructureException.hpp"
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/pem.h>
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/asn1.h>
#include <memory>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <regex>
#include <spdlog/spdlog.h>

namespace fileparsing::infrastructure::adapter {

using namespace fileparsing::domain::model;

/**
 * @brief RAII wrapper for OpenSSL X509
 */
struct X509Deleter {
    void operator()(X509* x) const { if (x) X509_free(x); }
};
using X509Ptr = std::unique_ptr<X509, X509Deleter>;

/**
 * @brief RAII wrapper for OpenSSL X509_CRL
 */
struct X509CrlDeleter {
    void operator()(X509_CRL* crl) const { if (crl) X509_CRL_free(crl); }
};
using X509CrlPtr = std::unique_ptr<X509_CRL, X509CrlDeleter>;

/**
 * @brief OpenSSL certificate parsing utilities
 */
class OpenSslCertificateParser {
private:
    /**
     * @brief Get OpenSSL error string
     */
    static std::string getOpenSslError() {
        char buf[256];
        ERR_error_string_n(ERR_get_error(), buf, sizeof(buf));
        return std::string(buf);
    }

    /**
     * @brief Convert ASN1_TIME to time_point
     */
    static std::chrono::system_clock::time_point asn1TimeToTimePoint(const ASN1_TIME* asn1Time) {
        if (!asn1Time) {
            return std::chrono::system_clock::time_point{};
        }

        struct tm tm = {};
        int offset = 0;

        // Parse ASN1 time (UTC or Generalized)
        const char* str = reinterpret_cast<const char*>(asn1Time->data);
        size_t len = asn1Time->length;

        if (asn1Time->type == V_ASN1_UTCTIME) {
            // YYMMDDHHMMSSZ
            if (len >= 12) {
                int year = (str[0] - '0') * 10 + (str[1] - '0');
                tm.tm_year = (year >= 50 ? 1900 : 2000) + year - 1900;
                tm.tm_mon = (str[2] - '0') * 10 + (str[3] - '0') - 1;
                tm.tm_mday = (str[4] - '0') * 10 + (str[5] - '0');
                tm.tm_hour = (str[6] - '0') * 10 + (str[7] - '0');
                tm.tm_min = (str[8] - '0') * 10 + (str[9] - '0');
                tm.tm_sec = (str[10] - '0') * 10 + (str[11] - '0');
            }
        } else if (asn1Time->type == V_ASN1_GENERALIZEDTIME) {
            // YYYYMMDDHHMMSSZ
            if (len >= 14) {
                tm.tm_year = (str[0] - '0') * 1000 + (str[1] - '0') * 100 +
                             (str[2] - '0') * 10 + (str[3] - '0') - 1900;
                tm.tm_mon = (str[4] - '0') * 10 + (str[5] - '0') - 1;
                tm.tm_mday = (str[6] - '0') * 10 + (str[7] - '0');
                tm.tm_hour = (str[8] - '0') * 10 + (str[9] - '0');
                tm.tm_min = (str[10] - '0') * 10 + (str[11] - '0');
                tm.tm_sec = (str[12] - '0') * 10 + (str[13] - '0');
            }
        }

        time_t time = timegm(&tm);
        return std::chrono::system_clock::from_time_t(time);
    }

    /**
     * @brief Convert X509_NAME to string
     */
    static std::string x509NameToString(X509_NAME* name) {
        if (!name) return "";

        BIO* bio = BIO_new(BIO_s_mem());
        if (!bio) return "";

        X509_NAME_print_ex(bio, name, 0, XN_FLAG_RFC2253);

        char* data = nullptr;
        long len = BIO_get_mem_data(bio, &data);
        std::string result(data, len);

        BIO_free(bio);
        return result;
    }

    /**
     * @brief Convert ASN1_INTEGER to hex string
     */
    static std::string asn1IntegerToHex(const ASN1_INTEGER* asn1Int) {
        if (!asn1Int) return "";

        BIGNUM* bn = ASN1_INTEGER_to_BN(asn1Int, nullptr);
        if (!bn) return "";

        char* hex = BN_bn2hex(bn);
        std::string result(hex);
        OPENSSL_free(hex);
        BN_free(bn);

        return result;
    }

    /**
     * @brief Compute SHA-256 fingerprint
     */
    static std::string computeFingerprint(const uint8_t* data, size_t len) {
        unsigned char hash[EVP_MAX_MD_SIZE];
        unsigned int hashLen;

        EVP_MD_CTX* ctx = EVP_MD_CTX_new();
        EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
        EVP_DigestUpdate(ctx, data, len);
        EVP_DigestFinal_ex(ctx, hash, &hashLen);
        EVP_MD_CTX_free(ctx);

        std::ostringstream ss;
        ss << std::hex << std::setfill('0');
        for (unsigned int i = 0; i < hashLen; i++) {
            ss << std::setw(2) << static_cast<int>(hash[i]);
        }
        return ss.str();
    }

public:
    /**
     * @brief Parse X.509 certificate from DER binary
     */
    static CertificateData parseCertificate(
        const std::vector<uint8_t>& derBytes,
        const std::string& ldapDn = "",
        const std::map<std::string, std::vector<std::string>>& attributes = {}
    ) {
        const uint8_t* data = derBytes.data();
        X509* cert = d2i_X509(nullptr, &data, static_cast<long>(derBytes.size()));

        if (!cert) {
            throw shared::exception::InfrastructureException(
                "CERTIFICATE_PARSE_ERROR",
                "Failed to parse X.509 certificate: " + getOpenSslError()
            );
        }

        X509Ptr certPtr(cert);

        // Extract certificate info
        std::string subjectDn = x509NameToString(X509_get_subject_name(cert));
        std::string issuerDn = x509NameToString(X509_get_issuer_name(cert));
        std::string serialNumber = asn1IntegerToHex(X509_get_serialNumber(cert));

        auto notBefore = asn1TimeToTimePoint(X509_get0_notBefore(cert));
        auto notAfter = asn1TimeToTimePoint(X509_get0_notAfter(cert));

        // Compute fingerprint
        std::string fingerprint = computeFingerprint(derBytes.data(), derBytes.size());

        // Determine certificate type
        CertificateType certType;
        if (subjectDn == issuerDn) {
            certType = CertificateType::CSCA;
        } else if (ldapDn.find("nc-data") != std::string::npos) {
            certType = CertificateType::DSC_NC;
        } else {
            certType = CertificateType::DSC;
        }

        // Extract country code
        std::string countryCode = extractCountryCode(subjectDn);
        if (countryCode.empty()) {
            countryCode = extractCountryCode(issuerDn);
        }
        if (countryCode.empty() && !ldapDn.empty()) {
            countryCode = extractCountryCodeFromDn(ldapDn);
        }

        // Build result
        auto builder = CertificateData::builder()
            .certificateType(certType)
            .countryCode(countryCode)
            .subjectDn(subjectDn)
            .issuerDn(issuerDn)
            .serialNumber(serialNumber)
            .notBefore(notBefore)
            .notAfter(notAfter)
            .certificateBinary(derBytes)
            .fingerprintSha256(fingerprint)
            .allAttributes(attributes);

        // Add conformance info if DSC_NC
        if (certType == CertificateType::DSC_NC) {
            auto it = attributes.find("pkdConformanceText");
            if (it != attributes.end() && !it->second.empty()) {
                builder.conformanceText(it->second[0]);
            }
            it = attributes.find("pkdConformanceCode");
            if (it != attributes.end() && !it->second.empty()) {
                builder.conformanceCode(it->second[0]);
            }
            it = attributes.find("pkdConformancePolicy");
            if (it != attributes.end() && !it->second.empty()) {
                builder.conformancePolicy(it->second[0]);
            }
        }

        return builder.build();
    }

    /**
     * @brief Parse CRL from DER binary
     */
    static CrlData parseCrl(const std::vector<uint8_t>& derBytes) {
        const uint8_t* data = derBytes.data();
        X509_CRL* crl = d2i_X509_CRL(nullptr, &data, static_cast<long>(derBytes.size()));

        if (!crl) {
            throw shared::exception::InfrastructureException(
                "CRL_PARSE_ERROR",
                "Failed to parse CRL: " + getOpenSslError()
            );
        }

        X509CrlPtr crlPtr(crl);

        // Extract CRL info
        std::string issuerDn = x509NameToString(X509_CRL_get_issuer(crl));
        auto thisUpdate = asn1TimeToTimePoint(X509_CRL_get0_lastUpdate(crl));

        std::optional<std::chrono::system_clock::time_point> nextUpdate;
        const ASN1_TIME* nextUpdateAsn1 = X509_CRL_get0_nextUpdate(crl);
        if (nextUpdateAsn1) {
            nextUpdate = asn1TimeToTimePoint(nextUpdateAsn1);
        }

        // Get CRL number extension
        std::string crlNumber;
        ASN1_INTEGER* crlNumAsn1 = static_cast<ASN1_INTEGER*>(
            X509_CRL_get_ext_d2i(crl, NID_crl_number, nullptr, nullptr)
        );
        if (crlNumAsn1) {
            crlNumber = asn1IntegerToHex(crlNumAsn1);
            ASN1_INTEGER_free(crlNumAsn1);
        }

        // Compute fingerprint
        std::string fingerprint = computeFingerprint(derBytes.data(), derBytes.size());

        // Extract country code
        std::string countryCode = extractCountryCode(issuerDn);

        // Get revoked certificates
        std::vector<RevokedCertificate> revokedCerts;
        STACK_OF(X509_REVOKED)* revokedStack = X509_CRL_get_REVOKED(crl);
        if (revokedStack) {
            int count = sk_X509_REVOKED_num(revokedStack);
            for (int i = 0; i < count; i++) {
                X509_REVOKED* revoked = sk_X509_REVOKED_value(revokedStack, i);
                if (revoked) {
                    RevokedCertificate rc;
                    rc.serialNumber = asn1IntegerToHex(X509_REVOKED_get0_serialNumber(revoked));
                    rc.revocationDate = asn1TimeToTimePoint(X509_REVOKED_get0_revocationDate(revoked));

                    // Get revocation reason if available
                    ASN1_ENUMERATED* reason = static_cast<ASN1_ENUMERATED*>(
                        X509_REVOKED_get_ext_d2i(revoked, NID_crl_reason, nullptr, nullptr)
                    );
                    if (reason) {
                        long reasonCode = ASN1_ENUMERATED_get(reason);
                        rc.revocationReason = getRevocationReasonString(reasonCode);
                        ASN1_ENUMERATED_free(reason);
                    }

                    revokedCerts.push_back(std::move(rc));
                }
            }
        }

        // Build result
        auto builder = CrlData::builder()
            .countryCode(countryCode)
            .issuerDn(issuerDn)
            .crlNumber(crlNumber)
            .thisUpdate(thisUpdate)
            .crlBinary(derBytes)
            .fingerprintSha256(fingerprint)
            .revokedCertificates(std::move(revokedCerts));

        if (nextUpdate) {
            builder.nextUpdate(*nextUpdate);
        }

        return builder.build();
    }

    /**
     * @brief Extract country code from DN string
     */
    static std::string extractCountryCode(const std::string& dn) {
        // Match C=XX or c=XX pattern
        static const std::regex countryRegex(R"((?:^|,\s*)C=([A-Z]{2,3})(?:,|$))", std::regex::icase);
        std::smatch match;
        if (std::regex_search(dn, match, countryRegex)) {
            std::string code = match[1].str();
            // Convert to uppercase
            for (char& c : code) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            return code;
        }
        return "";
    }

    /**
     * @brief Extract country code from LDAP DN
     */
    static std::string extractCountryCodeFromDn(const std::string& ldapDn) {
        // Match c=XX pattern in LDAP DN
        static const std::regex countryRegex(R"((?:^|,)c=([A-Z]{2,3})(?:,|$))", std::regex::icase);
        std::smatch match;
        if (std::regex_search(ldapDn, match, countryRegex)) {
            std::string code = match[1].str();
            for (char& c : code) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            return code;
        }
        return "";
    }

private:
    /**
     * @brief Get revocation reason string
     */
    static std::string getRevocationReasonString(long reason) {
        switch (reason) {
            case 0: return "unspecified";
            case 1: return "keyCompromise";
            case 2: return "cACompromise";
            case 3: return "affiliationChanged";
            case 4: return "superseded";
            case 5: return "cessationOfOperation";
            case 6: return "certificateHold";
            case 8: return "removeFromCRL";
            case 9: return "privilegeWithdrawn";
            case 10: return "aACompromise";
            default: return "unknown";
        }
    }
};

} // namespace fileparsing::infrastructure::adapter
