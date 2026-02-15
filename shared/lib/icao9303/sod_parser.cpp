/**
 * @file sod_parser_service.cpp
 * @brief Implementation of SodParser
 */

#include "sod_parser.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>
#include <iomanip>
#include <json/json.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/objects.h>
#include <openssl/asn1.h>
#include <openssl/asn1t.h>

namespace icao {

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

SodParser::SodParser() {
    spdlog::debug("SodParser initialized");
}

/// --- Main SOD Parsing Operations ---

models::SodData SodParser::parseSod(const std::vector<uint8_t>& sodBytes) {
    spdlog::debug("Parsing SOD ({} bytes)", sodBytes.size());

    models::SodData sodData;

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

        // Extract signing time from CMS signed attributes (for point-in-time validation)
        sodData.signingTime = extractSigningTime(sodBytes);
        if (!sodData.signingTime.empty()) {
            spdlog::info("SOD signing time: {}", sodData.signingTime);
        }

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

X509* SodParser::extractDscCertificate(const std::vector<uint8_t>& sodBytes) {
    spdlog::debug("Extracting DSC certificate from SOD");

    std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

    BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
    if (!bio) {
        spdlog::error("Failed to create BIO for DSC extraction");
        return nullptr;
    }
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

std::map<std::string, std::string> SodParser::extractDataGroupHashes(
    const std::vector<uint8_t>& sodBytes)
{
    spdlog::debug("Extracting data group hashes from SOD");

    std::map<int, std::vector<uint8_t>> rawHashes = parseDataGroupHashesRaw(sodBytes);
    std::map<std::string, std::string> hexHashes;

    for (const auto& [dgNum, hashBytes] : rawHashes) {
        // Use number-only format to match frontend ("1", "2", "3" instead of "DG1", "DG2", "DG3")
        std::string dgKey = std::to_string(dgNum);
        hexHashes[dgKey] = hashToHexString(hashBytes);
    }

    spdlog::info("Extracted {} data group hashes", hexHashes.size());
    return hexHashes;
}

bool SodParser::verifySodSignature(
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
    if (!bio) {
        spdlog::error("Failed to create BIO for signature verification");
        return false;
    }
    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (!cms) {
        spdlog::error("Failed to parse CMS for signature verification");
        return false;
    }

    // Create certificate store with DSC
    X509_STORE* store = X509_STORE_new();
    if (!store) {
        spdlog::error("Failed to create X509 store");
        CMS_ContentInfo_free(cms);
        return false;
    }
    STACK_OF(X509)* certs = sk_X509_new_null();
    if (!certs) {
        spdlog::error("Failed to create certificate stack");
        X509_STORE_free(store);
        CMS_ContentInfo_free(cms);
        return false;
    }
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

/// --- Algorithm Extraction ---

std::string SodParser::extractSignatureAlgorithm(const std::vector<uint8_t>& sodBytes) {
    std::string oid = extractSignatureAlgorithmOid(sodBytes);
    return getAlgorithmName(oid, false);
}

std::string SodParser::extractHashAlgorithm(const std::vector<uint8_t>& sodBytes) {
    std::string oid = extractHashAlgorithmOid(sodBytes);
    return getAlgorithmName(oid, true);
}

std::string SodParser::extractSignatureAlgorithmOid(const std::vector<uint8_t>& sodBytes) {
    std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

    BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
    if (!bio) return "";
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

std::string SodParser::extractHashAlgorithmOid(const std::vector<uint8_t>& sodBytes) {
    std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

    BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
    if (!bio) return "";
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

std::string SodParser::extractSigningTime(const std::vector<uint8_t>& sodBytes) {
    std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

    BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
    if (!bio) return "";
    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (!cms) return "";

    STACK_OF(CMS_SignerInfo)* signerInfos = CMS_get0_SignerInfos(cms);
    if (!signerInfos || sk_CMS_SignerInfo_num(signerInfos) == 0) {
        CMS_ContentInfo_free(cms);
        return "";
    }

    CMS_SignerInfo* si = sk_CMS_SignerInfo_value(signerInfos, 0);
    if (!si) {
        CMS_ContentInfo_free(cms);
        return "";
    }

    // Get signing time from signed attributes (NID_pkcs9_signingTime)
    int idx = CMS_signed_get_attr_by_NID(si, NID_pkcs9_signingTime, -1);
    if (idx < 0) {
        spdlog::debug("No signingTime attribute found in SOD CMS signed attributes");
        CMS_ContentInfo_free(cms);
        return "";
    }

    X509_ATTRIBUTE* attr = CMS_signed_get_attr(si, idx);
    if (!attr) {
        CMS_ContentInfo_free(cms);
        return "";
    }

    ASN1_TYPE* attrVal = X509_ATTRIBUTE_get0_type(attr, 0);
    if (!attrVal) {
        CMS_ContentInfo_free(cms);
        return "";
    }

    ASN1_TIME* sigTime = nullptr;
    if (attrVal->type == V_ASN1_UTCTIME) {
        sigTime = attrVal->value.utctime;
    } else if (attrVal->type == V_ASN1_GENERALIZEDTIME) {
        sigTime = attrVal->value.generalizedtime;
    }

    std::string result;
    if (sigTime) {
        struct tm tmResult = {};
        if (ASN1_TIME_to_tm(sigTime, &tmResult) == 1) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                     tmResult.tm_year + 1900,
                     tmResult.tm_mon + 1,
                     tmResult.tm_mday,
                     tmResult.tm_hour,
                     tmResult.tm_min,
                     tmResult.tm_sec);
            result = std::string(buf);
        }
    }

    CMS_ContentInfo_free(cms);
    return result;
}

/// --- Helper Methods ---

std::vector<uint8_t> SodParser::unwrapIcaoSod(const std::vector<uint8_t>& sodBytes) {
    // Check if SOD has ICAO wrapper tag (0x77)
    if (sodBytes.size() > 4 && sodBytes[0] == 0x77) {
        // Skip tag and length bytes
        size_t offset = 1;

        // Parse length (can be short or long form)
        if (offset >= sodBytes.size()) return sodBytes;
        if (sodBytes[offset] & 0x80) {
            int numLengthBytes = sodBytes[offset] & 0x7F;
            if (offset + numLengthBytes + 1 > sodBytes.size()) {
                spdlog::error("SOD unwrap: length bytes exceed buffer");
                return sodBytes;
            }
            offset += numLengthBytes + 1;
        } else {
            offset += 1;
        }

        if (offset >= sodBytes.size()) {
            spdlog::error("SOD unwrap: offset exceeds buffer after length parse");
            return sodBytes;
        }

        // Return unwrapped content
        return std::vector<uint8_t>(sodBytes.begin() + offset, sodBytes.end());
    }

    // No wrapper, return as-is
    return sodBytes;
}

std::map<int, std::vector<uint8_t>> SodParser::parseDataGroupHashesRaw(
    const std::vector<uint8_t>& sodBytes)
{
    std::map<int, std::vector<uint8_t>> result;

    std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

    BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
    if (!bio) {
        spdlog::error("Failed to create BIO for DG hash parsing");
        return result;
    }
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

    // Parse LDSSecurityObject ASN.1 - Manual parsing (from v1.0)
    const unsigned char* p = ASN1_STRING_get0_data(*contentPtr);
    long dataLen = ASN1_STRING_length(*contentPtr);
    const unsigned char* contentData = p;
    const unsigned char* end = p + dataLen;  // Buffer boundary for all checks

    // Skip outer SEQUENCE tag and length
    if (contentData >= end || *contentData != 0x30) {
        spdlog::error("Expected SEQUENCE tag for LDSSecurityObject");
        CMS_ContentInfo_free(cms);
        return result;
    }
    contentData++;

    // Parse length
    if (contentData >= end) { CMS_ContentInfo_free(cms); return result; }
    size_t contentLen = 0;
    if (*contentData & 0x80) {
        int numBytes = *contentData & 0x7F;
        contentData++;
        if (contentData + numBytes > end) { CMS_ContentInfo_free(cms); return result; }
        for (int i = 0; i < numBytes; i++) {
            contentLen = (contentLen << 8) | *contentData++;
        }
    } else {
        contentLen = *contentData++;
    }

    // Skip version (INTEGER)
    if (contentData < end && *contentData == 0x02) {
        contentData++;
        if (contentData >= end) { CMS_ContentInfo_free(cms); return result; }
        size_t versionLen = *contentData++;
        if (contentData + versionLen > end) { CMS_ContentInfo_free(cms); return result; }
        contentData += versionLen;
    }

    // Skip hashAlgorithm (SEQUENCE - AlgorithmIdentifier)
    if (contentData < end && *contentData == 0x30) {
        contentData++;
        if (contentData >= end) { CMS_ContentInfo_free(cms); return result; }
        size_t algLen = 0;
        if (*contentData & 0x80) {
            int numBytes = *contentData & 0x7F;
            contentData++;
            if (contentData + numBytes > end) { CMS_ContentInfo_free(cms); return result; }
            for (int i = 0; i < numBytes; i++) {
                algLen = (algLen << 8) | *contentData++;
            }
        } else {
            algLen = *contentData++;
        }
        if (contentData + algLen > end) { CMS_ContentInfo_free(cms); return result; }
        contentData += algLen;
    }

    // Parse dataGroupHashValues (SEQUENCE OF DataGroupHash)
    if (contentData < end && *contentData == 0x30) {
        contentData++;
        if (contentData >= end) { CMS_ContentInfo_free(cms); return result; }
        size_t dgHashesLen = 0;
        if (*contentData & 0x80) {
            int numBytes = *contentData & 0x7F;
            contentData++;
            if (contentData + numBytes > end) { CMS_ContentInfo_free(cms); return result; }
            for (int i = 0; i < numBytes; i++) {
                dgHashesLen = (dgHashesLen << 8) | *contentData++;
            }
        } else {
            dgHashesLen = *contentData++;
        }

        // Clamp dgHashesEnd to buffer boundary
        const unsigned char* dgHashesEnd = contentData + dgHashesLen;
        if (dgHashesEnd > end) dgHashesEnd = end;

        // Parse each DataGroupHash
        while (contentData < dgHashesEnd) {
            if (*contentData != 0x30) break;
            contentData++;
            if (contentData >= dgHashesEnd) break;

            size_t dgHashLen = 0;
            if (*contentData & 0x80) {
                int numBytes = *contentData & 0x7F;
                contentData++;
                if (contentData + numBytes > dgHashesEnd) break;
                for (int i = 0; i < numBytes; i++) {
                    dgHashLen = (dgHashLen << 8) | *contentData++;
                }
            } else {
                dgHashLen = *contentData++;
            }

            const unsigned char* dgHashEnd = contentData + dgHashLen;
            if (dgHashEnd > dgHashesEnd) break;

            // Parse dataGroupNumber (INTEGER)
            int dgNumber = 0;
            if (contentData < dgHashEnd && *contentData == 0x02) {
                contentData++;
                if (contentData >= dgHashEnd) { contentData = dgHashEnd; continue; }
                size_t intLen = *contentData++;
                if (contentData + intLen > dgHashEnd) { contentData = dgHashEnd; continue; }
                for (size_t i = 0; i < intLen; i++) {
                    dgNumber = (dgNumber << 8) | *contentData++;
                }
            }

            // Parse dataGroupHashValue (OCTET STRING)
            if (contentData < dgHashEnd && *contentData == 0x04) {
                contentData++;
                if (contentData >= dgHashEnd) { contentData = dgHashEnd; continue; }
                size_t hashLen = 0;
                if (*contentData & 0x80) {
                    int numBytes = *contentData & 0x7F;
                    contentData++;
                    if (contentData + numBytes > dgHashEnd) { contentData = dgHashEnd; continue; }
                    for (int i = 0; i < numBytes; i++) {
                        hashLen = (hashLen << 8) | *contentData++;
                    }
                } else {
                    hashLen = *contentData++;
                }

                if (contentData + hashLen <= dgHashEnd) {
                    std::vector<uint8_t> hashValue(contentData, contentData + hashLen);
                    result[dgNumber] = hashValue;
                    contentData += hashLen;
                    spdlog::debug("Parsed DG{} hash: {} bytes", dgNumber, hashLen);
                }
            }

            contentData = dgHashEnd;
        }
    }

    CMS_ContentInfo_free(cms);
    spdlog::info("Parsed {} Data Group hashes from SOD", result.size());
    return result;
}

std::string SodParser::hashToHexString(const std::vector<uint8_t>& hashBytes) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (uint8_t byte : hashBytes) {
        oss << std::setw(2) << static_cast<int>(byte);
    }
    return oss.str();
}

std::string SodParser::getAlgorithmName(const std::string& oid, bool isHash) {
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

const std::map<std::string, std::string>& SodParser::getHashAlgorithmNames() {
    return HASH_ALGORITHM_NAMES;
}

const std::map<std::string, std::string>& SodParser::getSignatureAlgorithmNames() {
    return SIGNATURE_ALGORITHM_NAMES;
}

/// --- API-Specific Methods ---

Json::Value SodParser::parseSodForApi(const std::vector<uint8_t>& sodBytes) {
    spdlog::debug("Parsing SOD for API response ({} bytes)", sodBytes.size());

    Json::Value result;
    result["success"] = true;
    result["sodSize"] = static_cast<int>(sodBytes.size());

    try {
        // Extract hash algorithm
        std::string hashAlgorithm = extractHashAlgorithm(sodBytes);
        std::string hashAlgorithmOid = extractHashAlgorithmOid(sodBytes);
        result["hashAlgorithm"] = hashAlgorithm;
        result["hashAlgorithmOid"] = hashAlgorithmOid;

        // Extract signature algorithm
        std::string signatureAlgorithm = extractSignatureAlgorithm(sodBytes);
        result["signatureAlgorithm"] = signatureAlgorithm;

        // Extract DSC certificate info
        X509* dscCert = extractDscCertificate(sodBytes);
        if (dscCert) {
            Json::Value dscInfo;

            // Subject DN
            char* subjectDn = X509_NAME_oneline(X509_get_subject_name(dscCert), nullptr, 0);
            if (subjectDn) {
                dscInfo["subjectDn"] = subjectDn;
                OPENSSL_free(subjectDn);
            }

            // Issuer DN
            char* issuerDn = X509_NAME_oneline(X509_get_issuer_name(dscCert), nullptr, 0);
            if (issuerDn) {
                dscInfo["issuerDn"] = issuerDn;
                OPENSSL_free(issuerDn);
            }

            // Serial number
            ASN1_INTEGER* serialAsn1 = X509_get_serialNumber(dscCert);
            if (serialAsn1) {
                BIGNUM* bn = ASN1_INTEGER_to_BN(serialAsn1, nullptr);
                if (bn) {
                    char* serialHex = BN_bn2hex(bn);
                    if (serialHex) {
                        dscInfo["serialNumber"] = serialHex;
                        OPENSSL_free(serialHex);
                    }
                    BN_free(bn);
                }
            }

            // Validity period
            const ASN1_TIME* notBefore = X509_get0_notBefore(dscCert);
            const ASN1_TIME* notAfter = X509_get0_notAfter(dscCert);

            if (notBefore) {
                BIO* bio = BIO_new(BIO_s_mem());
                if (bio) {
                    ASN1_TIME_print(bio, notBefore);
                    char buf[256];
                    int len = BIO_read(bio, buf, sizeof(buf) - 1);
                    if (len > 0) {
                        buf[len] = '\0';
                        dscInfo["notBefore"] = buf;
                    }
                    BIO_free(bio);
                }
            }

            if (notAfter) {
                BIO* bio = BIO_new(BIO_s_mem());
                if (bio) {
                    ASN1_TIME_print(bio, notAfter);
                    char buf[256];
                    int len = BIO_read(bio, buf, sizeof(buf) - 1);
                    if (len > 0) {
                        buf[len] = '\0';
                        dscInfo["notAfter"] = buf;
                    }
                    BIO_free(bio);
                }
            }

            // Country code from issuer
            X509_NAME* issuerName = X509_get_issuer_name(dscCert);
            int countryIdx = X509_NAME_get_index_by_NID(issuerName, NID_countryName, -1);
            if (countryIdx >= 0) {
                X509_NAME_ENTRY* entry = X509_NAME_get_entry(issuerName, countryIdx);
                if (entry) {
                    ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
                    if (data) {
                        unsigned char* utf8 = nullptr;
                        int utf8len = ASN1_STRING_to_UTF8(&utf8, data);
                        if (utf8len > 0) {
                            dscInfo["countryCode"] = std::string(reinterpret_cast<char*>(utf8), utf8len);
                            OPENSSL_free(utf8);
                        }
                    }
                }
            }

            result["dscCertificate"] = dscInfo;
            X509_free(dscCert);
        } else {
            result["dscCertificate"] = Json::nullValue;
            result["warning"] = "Failed to extract DSC certificate from SOD";
        }

        // Extract contained data groups
        std::map<int, std::vector<uint8_t>> dgHashes = parseDataGroupHashesRaw(sodBytes);
        Json::Value containedDgs(Json::arrayValue);
        for (const auto& [dgNum, hash] : dgHashes) {
            Json::Value dgInfo;
            dgInfo["dgNumber"] = dgNum;
            dgInfo["dgName"] = "DG" + std::to_string(dgNum);
            dgInfo["hashValue"] = hashToHexString(hash);
            dgInfo["hashLength"] = static_cast<int>(hash.size());
            containedDgs.append(dgInfo);
        }
        result["containedDataGroups"] = containedDgs;
        result["dataGroupCount"] = static_cast<int>(dgHashes.size());

        // Check if ICAO wrapper (Tag 0x77) was present
        bool hasIcaoWrapper = (sodBytes.size() > 0 && sodBytes[0] == 0x77);
        result["hasIcaoWrapper"] = hasIcaoWrapper;

        // LDS version (if available from DG hashes)
        if (!dgHashes.empty()) {
            // Check for DG14 (Active Authentication) and DG15 (Extended Access Control)
            result["hasDg14"] = (dgHashes.find(14) != dgHashes.end());
            result["hasDg15"] = (dgHashes.find(15) != dgHashes.end());
        }

    } catch (const std::exception& e) {
        spdlog::error("Error parsing SOD for API: {}", e.what());
        result["success"] = false;
        result["error"] = std::string("Failed to parse SOD: ") + e.what();
    }

    return result;
}

} // namespace icao
