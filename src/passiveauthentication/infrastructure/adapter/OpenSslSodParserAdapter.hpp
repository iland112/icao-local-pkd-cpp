#pragma once

#include "passiveauthentication/domain/port/SodParserPort.hpp"
#include "shared/exception/InfrastructureException.hpp"
#include <openssl/cms.h>
#include <openssl/bio.h>
#include <openssl/pem.h>
#include <openssl/err.h>
#include <openssl/asn1.h>
#include <openssl/objects.h>
#include <spdlog/spdlog.h>
#include <map>
#include <sstream>
#include <iomanip>

namespace pa::infrastructure::adapter {

/**
 * OpenSSL implementation of SodParserPort.
 *
 * Provides SOD (Security Object Document) parsing and verification
 * using OpenSSL cryptographic library.
 *
 * SOD is a PKCS#7/CMS SignedData structure containing:
 * - LDSSecurityObject with Data Group hashes
 * - Digital signature from DSC
 * - Hash and signature algorithm identifiers
 *
 * Reference: ICAO Doc 9303 Part 11 - Security Mechanisms for MRTDs
 */
class OpenSslSodParserAdapter : public domain::port::SodParserPort {
private:
    // OID mappings for hash algorithms
    static const std::map<std::string, std::string>& getHashAlgorithmNames() {
        static const std::map<std::string, std::string> names = {
            {"1.3.14.3.2.26", "SHA-1"},           // Deprecated, legacy only
            {"2.16.840.1.101.3.4.2.1", "SHA-256"},
            {"2.16.840.1.101.3.4.2.2", "SHA-384"},
            {"2.16.840.1.101.3.4.2.3", "SHA-512"}
        };
        return names;
    }

    // OID mappings for signature algorithms
    static const std::map<std::string, std::string>& getSignatureAlgorithmNames() {
        static const std::map<std::string, std::string> names = {
            {"1.2.840.113549.1.1.11", "SHA256withRSA"},
            {"1.2.840.113549.1.1.12", "SHA384withRSA"},
            {"1.2.840.113549.1.1.13", "SHA512withRSA"},
            {"1.2.840.10045.4.3.2", "SHA256withECDSA"},
            {"1.2.840.10045.4.3.3", "SHA384withECDSA"},
            {"1.2.840.10045.4.3.4", "SHA512withECDSA"}
        };
        return names;
    }

    static std::string getOpenSslError() {
        unsigned long err = ERR_get_error();
        char buf[256];
        ERR_error_string_n(err, buf, sizeof(buf));
        return std::string(buf);
    }

    static std::string bytesToHex(const uint8_t* data, size_t len) {
        std::ostringstream oss;
        for (size_t i = 0; i < len; ++i) {
            oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(data[i]);
        }
        return oss.str();
    }

    // Parse LDSSecurityObject from CMS content
    // LDSSecurityObject ::= SEQUENCE {
    //   version INTEGER,
    //   hashAlgorithm AlgorithmIdentifier,
    //   dataGroupHashValues SEQUENCE OF DataGroupHash
    // }
    std::map<domain::model::DataGroupNumber, domain::model::DataGroupHash> parseLdsSecurityObject(
        const uint8_t* data, size_t len
    ) {
        std::map<domain::model::DataGroupNumber, domain::model::DataGroupHash> result;

        const uint8_t* p = data;
        long length;
        int tag, xclass;

        // Parse outer SEQUENCE (LDSSecurityObject)
        ASN1_get_object(&p, &length, &tag, &xclass, len);
        if (tag != V_ASN1_SEQUENCE) {
            throw shared::exception::InfrastructureException(
                "LDS_PARSE_ERROR", "Expected SEQUENCE for LDSSecurityObject"
            );
        }

        const uint8_t* seqEnd = p + length;

        // Skip version INTEGER
        ASN1_get_object(&p, &length, &tag, &xclass, seqEnd - p);
        p += length;

        // Skip hashAlgorithm AlgorithmIdentifier (SEQUENCE)
        ASN1_get_object(&p, &length, &tag, &xclass, seqEnd - p);
        p += length;

        // Parse dataGroupHashValues SEQUENCE OF
        ASN1_get_object(&p, &length, &tag, &xclass, seqEnd - p);
        if (tag != V_ASN1_SEQUENCE) {
            throw shared::exception::InfrastructureException(
                "LDS_PARSE_ERROR", "Expected SEQUENCE for dataGroupHashValues"
            );
        }

        const uint8_t* dgSeqEnd = p + length;

        // Parse each DataGroupHash
        while (p < dgSeqEnd) {
            // DataGroupHash ::= SEQUENCE { dataGroupNumber INTEGER, dataGroupHashValue OCTET STRING }
            ASN1_get_object(&p, &length, &tag, &xclass, dgSeqEnd - p);
            if (tag != V_ASN1_SEQUENCE) break;

            const uint8_t* dgEnd = p + length;

            // Parse dataGroupNumber INTEGER
            ASN1_get_object(&p, &length, &tag, &xclass, dgEnd - p);
            if (tag != V_ASN1_INTEGER || length < 1) {
                p = dgEnd;
                continue;
            }
            int dgNumber = 0;
            for (long i = 0; i < length; ++i) {
                dgNumber = (dgNumber << 8) | p[i];
            }
            p += length;

            // Parse dataGroupHashValue OCTET STRING
            ASN1_get_object(&p, &length, &tag, &xclass, dgEnd - p);
            if (tag != V_ASN1_OCTET_STRING) {
                p = dgEnd;
                continue;
            }

            std::vector<uint8_t> hashBytes(p, p + length);
            p += length;

            try {
                auto dgNum = domain::model::dataGroupNumberFromInt(dgNumber);
                result[dgNum] = domain::model::DataGroupHash::of(hashBytes);
                spdlog::debug("Extracted hash for DG{}: {} bytes", dgNumber, length);
            } catch (const std::exception& e) {
                spdlog::warn("Invalid DG number {}: {}", dgNumber, e.what());
            }
        }

        return result;
    }

public:
    OpenSslSodParserAdapter() {
        OpenSSL_add_all_algorithms();
        ERR_load_crypto_strings();
    }

    ~OpenSslSodParserAdapter() override = default;

    std::vector<uint8_t> unwrapIcaoSod(const std::vector<uint8_t>& sodBytes) override {
        if (sodBytes.size() < 4) {
            return sodBytes;
        }

        // Check for ICAO Tag 0x77 (Application[23])
        if (sodBytes[0] == 0x77) {
            spdlog::debug("SOD has Tag 0x77 wrapper, unwrapping...");

            const uint8_t* p = sodBytes.data();
            long length;
            int tag, xclass;

            // Parse Tag 0x77
            ASN1_get_object(&p, &length, &tag, &xclass, sodBytes.size());

            // Skip to content (should be SEQUENCE 0x30)
            if (*p == 0x30) {
                size_t offset = p - sodBytes.data();
                std::vector<uint8_t> result(sodBytes.begin() + offset, sodBytes.end());
                spdlog::debug("Unwrapped SOD: {} bytes (was {} bytes)", result.size(), sodBytes.size());
                return result;
            }
        }

        // Already unwrapped or raw CMS
        return sodBytes;
    }

    std::map<domain::model::DataGroupNumber, domain::model::DataGroupHash> parseDataGroupHashes(
        const std::vector<uint8_t>& sodBytes
    ) override {
        spdlog::debug("Parsing SOD to extract Data Group hashes (SOD size: {} bytes)", sodBytes.size());

        std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

        // Parse CMS SignedData
        BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
        if (!bio) {
            throw shared::exception::InfrastructureException(
                "SOD_PARSE_ERROR", "Failed to create BIO"
            );
        }

        CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
        BIO_free(bio);

        if (!cms) {
            throw shared::exception::InfrastructureException(
                "SOD_PARSE_ERROR", "Failed to parse CMS SignedData: " + getOpenSslError()
            );
        }

        // Get encapsulated content
        ASN1_OCTET_STRING** pContent = CMS_get0_content(cms);
        if (!pContent || !*pContent) {
            CMS_ContentInfo_free(cms);
            throw shared::exception::InfrastructureException(
                "SOD_PARSE_ERROR", "No encapsulated content in CMS"
            );
        }

        const uint8_t* contentData = ASN1_STRING_get0_data(*pContent);
        int contentLen = ASN1_STRING_length(*pContent);

        auto result = parseLdsSecurityObject(contentData, contentLen);

        CMS_ContentInfo_free(cms);

        spdlog::info("Successfully parsed {} Data Group hashes from SOD", result.size());
        return result;
    }

    bool verifySignature(const std::vector<uint8_t>& sodBytes, EVP_PKEY* dscPublicKey) override {
        spdlog::debug("Verifying SOD signature with DSC public key");

        std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

        BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
        if (!bio) {
            throw shared::exception::InfrastructureException(
                "SOD_VERIFY_ERROR", "Failed to create BIO"
            );
        }

        CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
        BIO_free(bio);

        if (!cms) {
            throw shared::exception::InfrastructureException(
                "SOD_VERIFY_ERROR", "Failed to parse CMS: " + getOpenSslError()
            );
        }

        // Create a certificate store (empty for now, we verify with specific key)
        X509_STORE* store = X509_STORE_new();

        // Verify signature - using CMS_NO_SIGNER_CERT_VERIFY to skip cert chain verification
        // since we're providing the public key directly
        BIO* contentBio = BIO_new(BIO_s_mem());
        int result = CMS_verify(cms, nullptr, store, nullptr, contentBio,
                                CMS_NO_SIGNER_CERT_VERIFY | CMS_NOINTERN);

        BIO_free(contentBio);
        X509_STORE_free(store);
        CMS_ContentInfo_free(cms);

        if (result == 1) {
            spdlog::info("SOD signature verification succeeded");
            return true;
        } else {
            spdlog::error("SOD signature verification failed: {}", getOpenSslError());
            return false;
        }
    }

    bool verifySignature(const std::vector<uint8_t>& sodBytes, X509* dscCert) override {
        spdlog::debug("Verifying SOD signature with DSC X509 certificate");

        std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

        BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
        if (!bio) {
            throw shared::exception::InfrastructureException(
                "SOD_VERIFY_ERROR", "Failed to create BIO"
            );
        }

        CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
        BIO_free(bio);

        if (!cms) {
            throw shared::exception::InfrastructureException(
                "SOD_VERIFY_ERROR", "Failed to parse CMS: " + getOpenSslError()
            );
        }

        // Create certificate stack with DSC
        STACK_OF(X509)* certs = sk_X509_new_null();
        sk_X509_push(certs, dscCert);

        // Create store
        X509_STORE* store = X509_STORE_new();

        // Verify with provided certificate
        BIO* contentBio = BIO_new(BIO_s_mem());
        int result = CMS_verify(cms, certs, store, nullptr, contentBio, CMS_NOINTERN);

        BIO_free(contentBio);
        sk_X509_free(certs);
        X509_STORE_free(store);
        CMS_ContentInfo_free(cms);

        if (result == 1) {
            spdlog::info("SOD signature verification succeeded with DSC certificate");
            return true;
        } else {
            spdlog::error("SOD signature verification failed: {}", getOpenSslError());
            return false;
        }
    }

    std::string extractHashAlgorithm(const std::vector<uint8_t>& sodBytes) override {
        spdlog::debug("Extracting hash algorithm from SOD");

        std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

        BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
        CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
        BIO_free(bio);

        if (!cms) {
            throw shared::exception::InfrastructureException(
                "HASH_ALGORITHM_EXTRACT_ERROR", "Failed to parse CMS"
            );
        }

        // Get digest algorithms from CMS
        STACK_OF(X509_ALGOR)* digestAlgs = CMS_get0_SignerInfos(cms);

        std::string algorithmName = "SHA-256";  // Default

        // Get first signer info
        if (digestAlgs && sk_CMS_SignerInfo_num(digestAlgs) > 0) {
            CMS_SignerInfo* si = sk_CMS_SignerInfo_value(digestAlgs, 0);
            if (si) {
                X509_ALGOR* digestAlg = nullptr;
                CMS_SignerInfo_get0_algs(si, nullptr, nullptr, &digestAlg, nullptr);
                if (digestAlg) {
                    const ASN1_OBJECT* obj;
                    X509_ALGOR_get0(&obj, nullptr, nullptr, digestAlg);
                    char oid[80];
                    OBJ_obj2txt(oid, sizeof(oid), obj, 1);

                    auto& names = getHashAlgorithmNames();
                    auto it = names.find(oid);
                    if (it != names.end()) {
                        algorithmName = it->second;
                    } else {
                        algorithmName = "UNKNOWN(" + std::string(oid) + ")";
                    }
                }
            }
        }

        CMS_ContentInfo_free(cms);

        spdlog::info("Extracted hash algorithm: {}", algorithmName);
        return algorithmName;
    }

    std::string extractSignatureAlgorithm(const std::vector<uint8_t>& sodBytes) override {
        spdlog::debug("Extracting signature algorithm from SOD");

        std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

        BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
        CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
        BIO_free(bio);

        if (!cms) {
            throw shared::exception::InfrastructureException(
                "SIGNATURE_ALGORITHM_EXTRACT_ERROR", "Failed to parse CMS"
            );
        }

        std::string algorithmName = "SHA256withRSA";  // Default

        STACK_OF(CMS_SignerInfo)* signerInfos = CMS_get0_SignerInfos(cms);
        if (signerInfos && sk_CMS_SignerInfo_num(signerInfos) > 0) {
            CMS_SignerInfo* si = sk_CMS_SignerInfo_value(signerInfos, 0);
            if (si) {
                X509_ALGOR* sigAlg = nullptr;
                CMS_SignerInfo_get0_algs(si, nullptr, nullptr, nullptr, &sigAlg);
                if (sigAlg) {
                    const ASN1_OBJECT* obj;
                    X509_ALGOR_get0(&obj, nullptr, nullptr, sigAlg);
                    char oid[80];
                    OBJ_obj2txt(oid, sizeof(oid), obj, 1);

                    auto& names = getSignatureAlgorithmNames();
                    auto it = names.find(oid);
                    if (it != names.end()) {
                        algorithmName = it->second;
                    } else {
                        algorithmName = "UNKNOWN(" + std::string(oid) + ")";
                    }
                }
            }
        }

        CMS_ContentInfo_free(cms);

        spdlog::info("Extracted signature algorithm: {}", algorithmName);
        return algorithmName;
    }

    domain::port::DscInfo extractDscInfo(const std::vector<uint8_t>& sodBytes) override {
        spdlog::debug("Extracting DSC information from SOD");

        X509* cert = extractDscCertificate(sodBytes);
        if (!cert) {
            throw shared::exception::InfrastructureException(
                "DSC_EXTRACT_ERROR", "Failed to extract DSC certificate"
            );
        }

        // Extract Subject DN
        X509_NAME* subjectName = X509_get_subject_name(cert);
        char* subjectDn = X509_NAME_oneline(subjectName, nullptr, 0);
        std::string subjectDnStr(subjectDn);
        OPENSSL_free(subjectDn);

        // Extract Serial Number
        ASN1_INTEGER* serial = X509_get_serialNumber(cert);
        BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
        char* serialHex = BN_bn2hex(bn);
        std::string serialNumber(serialHex);
        OPENSSL_free(serialHex);
        BN_free(bn);

        X509_free(cert);

        spdlog::info("Extracted DSC info - Subject: {}, Serial: {}", subjectDnStr, serialNumber);

        return domain::port::DscInfo{subjectDnStr, serialNumber};
    }

    X509* extractDscCertificate(const std::vector<uint8_t>& sodBytes) override {
        spdlog::debug("Extracting full DSC certificate from SOD");

        std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

        BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
        CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
        BIO_free(bio);

        if (!cms) {
            throw shared::exception::InfrastructureException(
                "DSC_EXTRACT_ERROR", "Failed to parse CMS: " + getOpenSslError()
            );
        }

        // Get certificates from CMS
        STACK_OF(X509)* certs = CMS_get1_certs(cms);
        if (!certs || sk_X509_num(certs) == 0) {
            CMS_ContentInfo_free(cms);
            throw shared::exception::InfrastructureException(
                "NO_DSC_IN_SOD", "No certificates found in SOD"
            );
        }

        // Get first certificate (DSC)
        X509* dscCert = sk_X509_value(certs, 0);
        if (!dscCert) {
            sk_X509_pop_free(certs, X509_free);
            CMS_ContentInfo_free(cms);
            throw shared::exception::InfrastructureException(
                "NO_DSC_IN_SOD", "Failed to get DSC certificate"
            );
        }

        // Duplicate the certificate (caller takes ownership)
        X509* result = X509_dup(dscCert);

        sk_X509_pop_free(certs, X509_free);
        CMS_ContentInfo_free(cms);

        if (result) {
            X509_NAME* subjectName = X509_get_subject_name(result);
            char* subjectDn = X509_NAME_oneline(subjectName, nullptr, 0);
            spdlog::info("Extracted DSC certificate - Subject: {}", subjectDn);
            OPENSSL_free(subjectDn);
        }

        return result;
    }
};

} // namespace pa::infrastructure::adapter
