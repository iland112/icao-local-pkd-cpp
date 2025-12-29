/**
 * @file MasterListParser.hpp
 * @brief ICAO Master List (CMS SignedData) parser
 */

#pragma once

#include "OpenSslCertificateParser.hpp"
#include "../../domain/model/CertificateData.hpp"
#include "shared/exception/InfrastructureException.hpp"
#include <openssl/cms.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/x509.h>
#include <spdlog/spdlog.h>
#include <vector>
#include <memory>

namespace fileparsing::infrastructure::adapter {

using namespace fileparsing::domain::model;

/**
 * @brief RAII wrapper for CMS_ContentInfo
 */
struct CmsDeleter {
    void operator()(CMS_ContentInfo* cms) const { if (cms) CMS_ContentInfo_free(cms); }
};
using CmsPtr = std::unique_ptr<CMS_ContentInfo, CmsDeleter>;

/**
 * @brief RAII wrapper for BIO
 */
struct BioDeleter {
    void operator()(BIO* bio) const { if (bio) BIO_free(bio); }
};
using BioPtr = std::unique_ptr<BIO, BioDeleter>;

/**
 * @brief Master List data
 */
struct MasterListData {
    std::string signerCountry;
    std::string signerDn;
    std::vector<uint8_t> mlBinary;
    std::string fingerprintSha256;
    bool signatureValid = false;
    std::vector<CertificateData> cscaCertificates;
};

/**
 * @brief ICAO Master List parser (CMS SignedData)
 */
class MasterListParser {
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

    /**
     * @brief Convert X509 to DER bytes
     */
    static std::vector<uint8_t> x509ToDer(X509* cert) {
        unsigned char* buf = nullptr;
        int len = i2d_X509(cert, &buf);
        if (len < 0 || !buf) {
            return {};
        }
        std::vector<uint8_t> result(buf, buf + len);
        OPENSSL_free(buf);
        return result;
    }

    /**
     * @brief Check if certificate is a CA (not a Master List Signer)
     */
    static bool isCaCertificate(X509* cert) {
        // Check Basic Constraints extension
        BASIC_CONSTRAINTS* bc = static_cast<BASIC_CONSTRAINTS*>(
            X509_get_ext_d2i(cert, NID_basic_constraints, nullptr, nullptr)
        );

        if (!bc) {
            // No basic constraints - might be CSCA (old format)
            // Check if self-signed
            X509_NAME* subject = X509_get_subject_name(cert);
            X509_NAME* issuer = X509_get_issuer_name(cert);
            return X509_NAME_cmp(subject, issuer) == 0;
        }

        bool isCA = bc->ca != 0;
        BASIC_CONSTRAINTS_free(bc);
        return isCA;
    }

public:
    /**
     * @brief Parse Master List from binary CMS SignedData
     */
    static MasterListData parse(const std::vector<uint8_t>& cmsBytes) {
        MasterListData result;
        result.mlBinary = cmsBytes;
        result.fingerprintSha256 = computeFingerprint(cmsBytes.data(), cmsBytes.size());

        // Create BIO from binary data
        BioPtr bio(BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size())));
        if (!bio) {
            throw shared::exception::InfrastructureException(
                "ML_PARSE_ERROR",
                "Failed to create BIO for Master List: " + getOpenSslError()
            );
        }

        // Parse CMS structure (DER format)
        CMS_ContentInfo* cms = d2i_CMS_bio(bio.get(), nullptr);
        if (!cms) {
            throw shared::exception::InfrastructureException(
                "ML_PARSE_ERROR",
                "Failed to parse CMS SignedData: " + getOpenSslError()
            );
        }

        CmsPtr cmsPtr(cms);

        // Get certificates from CMS
        STACK_OF(X509)* certs = CMS_get1_certs(cms);
        if (!certs) {
            spdlog::warn("No certificates found in Master List");
            return result;
        }

        spdlog::debug("Found {} certificates in Master List", sk_X509_num(certs));

        // Process each certificate
        int count = sk_X509_num(certs);
        for (int i = 0; i < count; i++) {
            X509* cert = sk_X509_value(certs, i);
            if (!cert) continue;

            try {
                // Check if this is a CA certificate (not ML signer)
                if (!isCaCertificate(cert)) {
                    // This is the Master List Signer - extract signer info
                    X509_NAME* subject = X509_get_subject_name(cert);
                    if (subject) {
                        BIO* nameBio = BIO_new(BIO_s_mem());
                        X509_NAME_print_ex(nameBio, subject, 0, XN_FLAG_RFC2253);
                        char* data = nullptr;
                        long len = BIO_get_mem_data(nameBio, &data);
                        result.signerDn = std::string(data, len);
                        result.signerCountry = OpenSslCertificateParser::extractCountryCode(result.signerDn);
                        BIO_free(nameBio);
                    }
                    continue;
                }

                // Convert to DER and parse as CSCA
                std::vector<uint8_t> derBytes = x509ToDer(cert);
                if (derBytes.empty()) {
                    continue;
                }

                auto certData = OpenSslCertificateParser::parseCertificate(derBytes, "", {});

                // Ensure it's marked as CSCA
                if (certData.getCertificateType() != CertificateType::CSCA) {
                    // Force CSCA type for Master List certificates
                    certData = CertificateData::builder()
                        .certificateType(CertificateType::CSCA)
                        .countryCode(certData.getCountryCode())
                        .subjectDn(certData.getSubjectDn())
                        .issuerDn(certData.getIssuerDn())
                        .serialNumber(certData.getSerialNumber())
                        .notBefore(certData.getNotBefore())
                        .notAfter(certData.getNotAfter())
                        .certificateBinary(derBytes)
                        .fingerprintSha256(certData.getFingerprintSha256())
                        .build();
                }

                result.cscaCertificates.push_back(std::move(certData));

            } catch (const std::exception& e) {
                spdlog::warn("Error parsing certificate {} from Master List: {}", i, e.what());
            }
        }

        // Free certificate stack
        sk_X509_pop_free(certs, X509_free);

        spdlog::info("Parsed Master List with {} CSCA certificates from {}",
            result.cscaCertificates.size(),
            result.signerCountry
        );

        return result;
    }

    /**
     * @brief Parse Master List from base64 encoded string
     */
    static MasterListData parseBase64(const std::string& base64Content) {
        // Decode base64
        static const std::string base64Chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::vector<uint8_t> decoded;
        std::vector<int> decodingTable(256, -1);
        for (size_t i = 0; i < base64Chars.size(); i++) {
            decodingTable[static_cast<unsigned char>(base64Chars[i])] = static_cast<int>(i);
        }

        int val = 0;
        int valb = -8;
        for (unsigned char c : base64Content) {
            if (decodingTable[c] == -1) continue;
            val = (val << 6) + decodingTable[c];
            valb += 6;
            if (valb >= 0) {
                decoded.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
                valb -= 8;
            }
        }

        return parse(decoded);
    }
};

} // namespace fileparsing::infrastructure::adapter
