/**
 * @file sod_parser_service.cpp
 * @brief Implementation of SodParserService
 */

#include "sod_parser_service.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/objects.h>

namespace services {

// Algorithm OID mappings (static initialization)
static const std::map<std::string, std::string> HASH_ALGORITHM_NAMES = {
    {"1.3.14.3.2.26", "SHA-1"},
    {"2.16.840.1.101.3.4.2.1", "SHA-256"},
    {"2.16.840.1.101.3.4.2.2", "SHA-384"},
    {"2.16.840.1.101.3.4.2.3", "SHA-512"}
};

static const std::map<std::string, std::string> SIGNATURE_ALGORITHM_NAMES = {
    {"1.2.840.113549.1.1.11", "SHA256withRSA"},
    {"1.2.840.113549.1.1.12", "SHA384withRSA"},
    {"1.2.840.113549.1.1.13", "SHA512withRSA"},
    {"1.2.840.10045.4.3.2", "SHA256withECDSA"},
    {"1.2.840.10045.4.3.3", "SHA384withECDSA"},
    {"1.2.840.10045.4.3.4", "SHA512withECDSA"}
};

SodParserService::SodParserService() {
    spdlog::debug("SodParserService initialized");
}

// ==========================================================================
// Main SOD Parsing Operations
// ==========================================================================

domain::models::SodData SodParserService::parseSod(const std::vector<uint8_t>& sodBytes) {
    spdlog::debug("Parsing SOD ({} bytes)", sodBytes.size());

    domain::models::SodData sodData;

    try {
        // Extract algorithms
        sodData.signatureAlgorithm = extractSignatureAlgorithm(sodBytes);
        sodData.signatureAlgorithmOid = extractSignatureAlgorithmOid(sodBytes);
        sodData.hashAlgorithm = extractHashAlgorithm(sodBytes);
        sodData.hashAlgorithmOid = extractHashAlgorithmOid(sodBytes);

        // Extract DSC certificate
        sodData.dscCertificate = extractDscCertificate(sodBytes);

        // Extract data group hashes
        sodData.dataGroupHashes = extractDataGroupHashes(sodBytes);

        // Set LDS version (assume V0 for now, can be extracted from encapsulated content)
        sodData.ldsSecurityObjectVersion = "V0";

        sodData.parsingSuccess = true;
        spdlog::info("SOD parsing successful: {} data groups, algorithm: {}",
            sodData.dataGroupHashes.size(), sodData.signatureAlgorithm);

    } catch (const std::exception& e) {
        spdlog::error("SOD parsing failed: {}", e.what());
        sodData.parsingSuccess = false;
        sodData.parsingErrors = e.what();
    }

    return sodData;
}

X509* SodParserService::extractDscCertificate(const std::vector<uint8_t>& sodBytes) {
    spdlog::debug("Extracting DSC certificate from SOD");

    std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

    BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (!cms) {
        spdlog::error("Failed to parse CMS structure");
        return nullptr;
    }

    // Get signer certificates
    STACK_OF(X509)* certs = CMS_get1_certs(cms);
    X509* dscCert = nullptr;

    if (certs && sk_X509_num(certs) > 0) {
        // Take first certificate (DSC)
        dscCert = X509_dup(sk_X509_value(certs, 0));
        spdlog::debug("Extracted DSC certificate from SOD");
    } else {
        spdlog::warn("No certificates found in SOD");
    }

    if (certs) sk_X509_pop_free(certs, X509_free);
    CMS_ContentInfo_free(cms);

    return dscCert;
}

std::map<std::string, std::string> SodParserService::extractDataGroupHashes(
    const std::vector<uint8_t>& sodBytes)
{
    spdlog::debug("Extracting data group hashes from SOD");

    std::map<int, std::vector<uint8_t>> rawHashes = parseDataGroupHashesRaw(sodBytes);
    std::map<std::string, std::string> hexHashes;

    for (const auto& [dgNum, hashBytes] : rawHashes) {
        std::string dgKey = "DG" + std::to_string(dgNum);
        hexHashes[dgKey] = hashToHexString(hashBytes);
    }

    spdlog::info("Extracted {} data group hashes", hexHashes.size());
    return hexHashes;
}

bool SodParserService::verifySodSignature(
    const std::vector<uint8_t>& sodBytes,
    X509* dscCert)
{
    if (!dscCert) {
        spdlog::error("DSC certificate is null, cannot verify SOD signature");
        return false;
    }

    spdlog::debug("Verifying SOD signature");

    std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

    BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (!cms) {
        spdlog::error("Failed to parse CMS for signature verification");
        return false;
    }

    // Create certificate store with DSC
    X509_STORE* store = X509_STORE_new();
    STACK_OF(X509)* certs = sk_X509_new_null();
    sk_X509_push(certs, dscCert);

    // Verify signature
    int verifyResult = CMS_verify(cms, certs, store, nullptr, nullptr,
                                   CMS_NO_SIGNER_CERT_VERIFY | CMS_NO_ATTR_VERIFY);

    bool valid = (verifyResult == 1);

    if (!valid) {
        unsigned long err = ERR_get_error();
        char errBuf[256];
        ERR_error_string_n(err, errBuf, sizeof(errBuf));
        spdlog::warn("SOD signature verification failed: {}", errBuf);
    } else {
        spdlog::info("SOD signature verification succeeded");
    }

    sk_X509_free(certs);
    X509_STORE_free(store);
    CMS_ContentInfo_free(cms);

    return valid;
}

// ==========================================================================
// Algorithm Extraction
// ==========================================================================

std::string SodParserService::extractSignatureAlgorithm(const std::vector<uint8_t>& sodBytes) {
    std::string oid = extractSignatureAlgorithmOid(sodBytes);
    return getAlgorithmName(oid, false);
}

std::string SodParserService::extractHashAlgorithm(const std::vector<uint8_t>& sodBytes) {
    std::string oid = extractHashAlgorithmOid(sodBytes);
    return getAlgorithmName(oid, true);
}

std::string SodParserService::extractSignatureAlgorithmOid(const std::vector<uint8_t>& sodBytes) {
    std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

    BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (!cms) return "";

    STACK_OF(CMS_SignerInfo)* signerInfos = CMS_get0_SignerInfos(cms);
    if (signerInfos && sk_CMS_SignerInfo_num(signerInfos) > 0) {
        CMS_SignerInfo* si = sk_CMS_SignerInfo_value(signerInfos, 0);
        X509_ALGOR* signatureAlg = nullptr;
        CMS_SignerInfo_get0_algs(si, nullptr, nullptr, nullptr, &signatureAlg);

        if (signatureAlg) {
            const ASN1_OBJECT* obj = nullptr;
            X509_ALGOR_get0(&obj, nullptr, nullptr, signatureAlg);
            char oidBuf[80];
            OBJ_obj2txt(oidBuf, sizeof(oidBuf), obj, 1);
            CMS_ContentInfo_free(cms);
            return std::string(oidBuf);
        }
    }

    CMS_ContentInfo_free(cms);
    return "";
}

std::string SodParserService::extractHashAlgorithmOid(const std::vector<uint8_t>& sodBytes) {
    std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

    BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (!cms) return "";

    STACK_OF(CMS_SignerInfo)* signerInfos = CMS_get0_SignerInfos(cms);
    if (signerInfos && sk_CMS_SignerInfo_num(signerInfos) > 0) {
        CMS_SignerInfo* si = sk_CMS_SignerInfo_value(signerInfos, 0);
        X509_ALGOR* digestAlg = nullptr;
        CMS_SignerInfo_get0_algs(si, nullptr, nullptr, &digestAlg, nullptr);

        if (digestAlg) {
            const ASN1_OBJECT* obj = nullptr;
            X509_ALGOR_get0(&obj, nullptr, nullptr, digestAlg);
            char oidBuf[80];
            OBJ_obj2txt(oidBuf, sizeof(oidBuf), obj, 1);
            CMS_ContentInfo_free(cms);
            return std::string(oidBuf);
        }
    }

    CMS_ContentInfo_free(cms);
    return "";
}

// ==========================================================================
// Helper Methods
// ==========================================================================

std::vector<uint8_t> SodParserService::unwrapIcaoSod(const std::vector<uint8_t>& sodBytes) {
    // Check if SOD has ICAO wrapper tag (0x77)
    if (sodBytes.size() > 4 && sodBytes[0] == 0x77) {
        // Skip tag and length bytes
        size_t offset = 1;

        // Parse length (can be short or long form)
        if (sodBytes[offset] & 0x80) {
            int numLengthBytes = sodBytes[offset] & 0x7F;
            offset += numLengthBytes + 1;
        } else {
            offset += 1;
        }

        // Return unwrapped content
        return std::vector<uint8_t>(sodBytes.begin() + offset, sodBytes.end());
    }

    // No wrapper, return as-is
    return sodBytes;
}

std::map<int, std::vector<uint8_t>> SodParserService::parseDataGroupHashesRaw(
    const std::vector<uint8_t>& sodBytes)
{
    std::map<int, std::vector<uint8_t>> result;

    std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

    BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (!cms) {
        spdlog::error("Failed to parse CMS for DG hashes");
        return result;
    }

    // Get encapsulated content (LDSSecurityObject)
    ASN1_OCTET_STRING** contentPtr = CMS_get0_content(cms);
    if (!contentPtr || !*contentPtr) {
        spdlog::error("No encapsulated content in CMS");
        CMS_ContentInfo_free(cms);
        return result;
    }

    // Parse LDSSecurityObject (simplified parsing)
    const unsigned char* p = (*contentPtr)->data;
    long len = (*contentPtr)->length;

    // This is a simplified parser - full implementation would use ASN.1 parser
    // For now, return empty map (can be enhanced later)

    CMS_ContentInfo_free(cms);
    return result;
}

std::string SodParserService::hashToHexString(const std::vector<uint8_t>& hashBytes) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint8_t byte : hashBytes) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

std::string SodParserService::getAlgorithmName(const std::string& oid, bool isHash) {
    const auto& nameMap = isHash ? HASH_ALGORITHM_NAMES : SIGNATURE_ALGORITHM_NAMES;
    auto it = nameMap.find(oid);
    if (it != nameMap.end()) {
        return it->second;
    }

    // Default fallbacks
    if (isHash) {
        return "SHA-256";
    } else {
        return "SHA256withRSA";
    }
}

const std::map<std::string, std::string>& SodParserService::getHashAlgorithmNames() {
    return HASH_ALGORITHM_NAMES;
}

const std::map<std::string, std::string>& SodParserService::getSignatureAlgorithmNames() {
    return SIGNATURE_ALGORITHM_NAMES;
}

} // namespace services
