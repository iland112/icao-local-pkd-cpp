/**
 * @file sod_parser.cpp
 * @brief Implementation of SodParser — ICAO Doc 9303 compliant SOD parsing
 *
 * Key design decisions:
 * - LDSSecurityObject hashAlgorithm (for DG hashes) is distinct from
 *   CMS SignerInfo digestAlgorithm (for SOD signature). These CAN differ.
 *   Example: NL Specimen uses SHA-256 for DG hashes, SHA-512 for CMS signature.
 * - Unknown algorithm OIDs are returned as-is with a warning log,
 *   never silently replaced with a wrong fallback.
 * - LDSSecurityObject parsing is consolidated into a single helper method.
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

// ============================================================
// Algorithm OID mappings per ICAO Doc 9303 Part 11, Table 35-40
// ============================================================

static const std::map<std::string, std::string> HASH_ALGORITHM_NAMES = {
    // SHA-1 (deprecated but still used by some countries)
    {"1.3.14.3.2.26",             "SHA-1"},
    // SHA-2 family
    {"2.16.840.1.101.3.4.2.4",    "SHA-224"},
    {"2.16.840.1.101.3.4.2.1",    "SHA-256"},
    {"2.16.840.1.101.3.4.2.2",    "SHA-384"},
    {"2.16.840.1.101.3.4.2.3",    "SHA-512"},
};

static const std::map<std::string, std::string> SIGNATURE_ALGORITHM_NAMES = {
    // RSA PKCS#1 v1.5 (RFC 3447)
    {"1.2.840.113549.1.1.5",      "SHA1withRSA"},
    {"1.2.840.113549.1.1.11",     "SHA256withRSA"},
    {"1.2.840.113549.1.1.12",     "SHA384withRSA"},
    {"1.2.840.113549.1.1.13",     "SHA512withRSA"},
    {"1.2.840.113549.1.1.14",     "SHA224withRSA"},
    // RSA-PSS (RFC 4055) — parameters specify the actual hash algorithm
    {"1.2.840.113549.1.1.10",     "RSASSA-PSS"},
    // ECDSA (RFC 5758)
    {"1.2.840.10045.4.1",         "ECDSAwithSHA1"},
    {"1.2.840.10045.4.3.1",       "SHA224withECDSA"},
    {"1.2.840.10045.4.3.2",       "SHA256withECDSA"},
    {"1.2.840.10045.4.3.3",       "SHA384withECDSA"},
    {"1.2.840.10045.4.3.4",       "SHA512withECDSA"},
};

// ============================================================
// ASN.1 DER length parser helper
// ============================================================

bool SodParser::parseAsn1Length(const unsigned char*& p, const unsigned char* end, size_t& outLen) {
    if (p >= end) return false;

    if (!(*p & 0x80)) {
        // Short form: single byte
        outLen = *p++;
        return true;
    }

    // Long form
    int numBytes = *p & 0x7F;
    p++;
    if (numBytes == 0 || numBytes > 4 || p + numBytes > end) return false;

    outLen = 0;
    for (int i = 0; i < numBytes; i++) {
        outLen = (outLen << 8) | *p++;
    }
    return true;
}

// ============================================================
// Constructor
// ============================================================

SodParser::SodParser() {
    spdlog::debug("SodParser initialized");
}

// ============================================================
// Main SOD Parsing Operations
// ============================================================

models::SodData SodParser::parseSod(const std::vector<uint8_t>& sodBytes) {
    spdlog::debug("Parsing SOD ({} bytes)", sodBytes.size());

    models::SodData sodData;

    try {
        // Parse CMS once, extract all information from it
        std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

        BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
        if (!bio) throw std::runtime_error("Failed to create BIO for SOD parsing");

        CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
        BIO_free(bio);
        if (!cms) throw std::runtime_error("Failed to parse CMS structure");

        // --- Extract CMS-level algorithms (SignerInfo) ---
        STACK_OF(CMS_SignerInfo)* signerInfos = CMS_get0_SignerInfos(cms);
        if (signerInfos && sk_CMS_SignerInfo_num(signerInfos) > 0) {
            CMS_SignerInfo* si = sk_CMS_SignerInfo_value(signerInfos, 0);

            // Signature algorithm (4th param)
            X509_ALGOR* signatureAlg = nullptr;
            CMS_SignerInfo_get0_algs(si, nullptr, nullptr, nullptr, &signatureAlg);
            if (signatureAlg) {
                const ASN1_OBJECT* obj = nullptr;
                X509_ALGOR_get0(&obj, nullptr, nullptr, signatureAlg);
                char oidBuf[80];
                OBJ_obj2txt(oidBuf, sizeof(oidBuf), obj, 1);
                sodData.signatureAlgorithmOid = oidBuf;
                sodData.signatureAlgorithm = getAlgorithmName(oidBuf, false);
            }

            // CMS digest algorithm (3rd param) — NOT the DG hash algorithm
            X509_ALGOR* digestAlg = nullptr;
            CMS_SignerInfo_get0_algs(si, nullptr, nullptr, &digestAlg, nullptr);
            if (digestAlg) {
                const ASN1_OBJECT* obj = nullptr;
                X509_ALGOR_get0(&obj, nullptr, nullptr, digestAlg);
                char oidBuf[80];
                OBJ_obj2txt(oidBuf, sizeof(oidBuf), obj, 1);
                sodData.cmsDigestAlgorithmOid = oidBuf;
                sodData.cmsDigestAlgorithm = getAlgorithmName(oidBuf, true);
            }
        }

        // --- Extract LDSSecurityObject fields ---
        auto ldsOpt = parseLdsSecurityObject(cms);
        if (ldsOpt) {
            auto& lds = *ldsOpt;

            // LDS hash algorithm (for DG hashes)
            sodData.hashAlgorithmOid = lds.hashAlgorithmOid;
            sodData.hashAlgorithm = getAlgorithmName(lds.hashAlgorithmOid, true);

            // LDS version
            sodData.ldsSecurityObjectVersion = (lds.version == 1) ? "V1" : "V0";

            // Data group hashes
            for (const auto& [dgNum, hashBytes] : lds.dgHashes) {
                sodData.dataGroupHashes[std::to_string(dgNum)] = hashToHexString(hashBytes);
            }

            // Log if CMS digest and LDS hash algorithms differ
            if (!sodData.cmsDigestAlgorithmOid.empty() &&
                sodData.cmsDigestAlgorithmOid != sodData.hashAlgorithmOid) {
                spdlog::info("SOD uses different algorithms: LDS hash={} ({}), CMS digest={} ({})",
                    sodData.hashAlgorithm, sodData.hashAlgorithmOid,
                    sodData.cmsDigestAlgorithm, sodData.cmsDigestAlgorithmOid);
            }
        } else {
            spdlog::warn("Failed to parse LDSSecurityObject from SOD");
        }

        // --- Extract DSC certificate ---
        STACK_OF(X509)* certs = CMS_get1_certs(cms);
        if (certs && sk_X509_num(certs) > 0) {
            sodData.dscCertificate = X509_dup(sk_X509_value(certs, 0));
        }
        if (certs) sk_X509_pop_free(certs, X509_free);

        // --- Extract signing time ---
        sodData.signingTime = extractSigningTime(sodBytes);
        if (!sodData.signingTime.empty()) {
            spdlog::info("SOD signing time: {}", sodData.signingTime);
        }

        CMS_ContentInfo_free(cms);

        sodData.parsingSuccess = true;
        spdlog::info("SOD parsing successful: {} data groups, LDS hash={}, CMS sig={}, LDS version={}",
            sodData.dataGroupHashes.size(), sodData.hashAlgorithm,
            sodData.signatureAlgorithm, sodData.ldsSecurityObjectVersion);

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

// ============================================================
// Algorithm Extraction
// ============================================================

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
    // Extract hashAlgorithm from LDSSecurityObject (encapsulated content),
    // NOT from CMS SignerInfo digestAlgorithm.
    // CMS digestAlgorithm is for SOD signature (e.g., SHA-512),
    // while LDSSecurityObject hashAlgorithm is for DG hashes (e.g., SHA-256).
    std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

    BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
    if (!bio) return "";
    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (!cms) return "";

    auto ldsOpt = parseLdsSecurityObject(cms);
    CMS_ContentInfo_free(cms);

    if (ldsOpt) {
        return ldsOpt->hashAlgorithmOid;
    }
    return "";
}

std::string SodParser::extractCmsDigestAlgorithmOid(const std::vector<uint8_t>& sodBytes) {
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

// ============================================================
// LDSSecurityObject consolidated parser
// ============================================================

std::optional<SodParser::LdsSecurityObject> SodParser::parseLdsSecurityObject(CMS_ContentInfo* cms) {
    if (!cms) return std::nullopt;

    // Get encapsulated content
    ASN1_OCTET_STRING** contentPtr = CMS_get0_content(cms);
    if (!contentPtr || !*contentPtr) {
        spdlog::error("No encapsulated content in CMS");
        return std::nullopt;
    }

    const unsigned char* p = ASN1_STRING_get0_data(*contentPtr);
    long dataLen = ASN1_STRING_length(*contentPtr);
    const unsigned char* end = p + dataLen;

    LdsSecurityObject result;

    // LDSSecurityObject ::= SEQUENCE {
    //   version INTEGER,
    //   hashAlgorithm AlgorithmIdentifier,
    //   dataGroupHashValues SEQUENCE OF DataGroupHash,
    //   ldsVersionInfo LDSVersionInfo OPTIONAL  -- only if version == 1
    // }

    // Outer SEQUENCE
    if (p >= end || *p != 0x30) {
        spdlog::error("Expected SEQUENCE tag for LDSSecurityObject");
        return std::nullopt;
    }
    p++;
    size_t seqLen = 0;
    if (!parseAsn1Length(p, end, seqLen)) return std::nullopt;

    // --- version INTEGER ---
    if (p < end && *p == 0x02) {
        p++;  // skip tag
        size_t vLen = 0;
        if (!parseAsn1Length(p, end, vLen)) return std::nullopt;
        if (p + vLen > end) return std::nullopt;

        // Parse version value (typically 0 or 1)
        result.version = 0;
        for (size_t i = 0; i < vLen; i++) {
            result.version = (result.version << 8) | *p++;
        }
        spdlog::debug("LDSSecurityObject version: {}", result.version);
    }

    // --- hashAlgorithm AlgorithmIdentifier SEQUENCE ---
    if (p >= end || *p != 0x30) {
        spdlog::error("Expected AlgorithmIdentifier SEQUENCE in LDSSecurityObject");
        return std::nullopt;
    }
    p++;  // skip SEQUENCE tag
    size_t algIdLen = 0;
    if (!parseAsn1Length(p, end, algIdLen)) return std::nullopt;
    if (p + algIdLen > end) return std::nullopt;

    const unsigned char* algIdEnd = p + algIdLen;

    // First element inside AlgorithmIdentifier is the OID
    if (p < algIdEnd && *p == 0x06) {
        const unsigned char* oidTlvStart = p;
        p++;  // skip tag
        size_t oidLen = 0;
        if (!parseAsn1Length(p, algIdEnd, oidLen)) return std::nullopt;

        // Build OID TLV for d2i_ASN1_OBJECT
        long oidTlvLen = static_cast<long>((p - oidTlvStart) + oidLen);
        if (oidTlvStart + oidTlvLen > end) return std::nullopt;

        const unsigned char* bp = oidTlvStart;
        ASN1_OBJECT* obj = d2i_ASN1_OBJECT(nullptr, &bp, oidTlvLen);
        if (obj) {
            char oidBuf[80];
            OBJ_obj2txt(oidBuf, sizeof(oidBuf), obj, 1);
            result.hashAlgorithmOid = oidBuf;
            ASN1_OBJECT_free(obj);
            spdlog::debug("LDSSecurityObject hashAlgorithm OID: {}", result.hashAlgorithmOid);
        }
    }

    // Skip to end of AlgorithmIdentifier (may have optional parameters)
    p = algIdEnd;

    // --- dataGroupHashValues SEQUENCE OF DataGroupHash ---
    if (p >= end || *p != 0x30) {
        spdlog::error("Expected SEQUENCE OF DataGroupHash in LDSSecurityObject");
        return std::nullopt;
    }
    p++;  // skip tag
    size_t dgHashesLen = 0;
    if (!parseAsn1Length(p, end, dgHashesLen)) return std::nullopt;

    const unsigned char* dgHashesEnd = p + dgHashesLen;
    if (dgHashesEnd > end) dgHashesEnd = end;

    // Parse each DataGroupHash ::= SEQUENCE { dataGroupNumber INTEGER, dataGroupHashValue OCTET STRING }
    while (p < dgHashesEnd) {
        if (*p != 0x30) break;
        p++;
        size_t dgHashLen = 0;
        if (!parseAsn1Length(p, dgHashesEnd, dgHashLen)) break;

        const unsigned char* dgHashEnd = p + dgHashLen;
        if (dgHashEnd > dgHashesEnd) break;

        // dataGroupNumber INTEGER
        int dgNumber = 0;
        if (p < dgHashEnd && *p == 0x02) {
            p++;
            size_t intLen = 0;
            if (!parseAsn1Length(p, dgHashEnd, intLen) || p + intLen > dgHashEnd) {
                p = dgHashEnd;
                continue;
            }
            for (size_t i = 0; i < intLen; i++) {
                dgNumber = (dgNumber << 8) | *p++;
            }
        }

        // dataGroupHashValue OCTET STRING
        if (p < dgHashEnd && *p == 0x04) {
            p++;
            size_t hashLen = 0;
            if (!parseAsn1Length(p, dgHashEnd, hashLen) || p + hashLen > dgHashEnd) {
                p = dgHashEnd;
                continue;
            }
            result.dgHashes[dgNumber] = std::vector<uint8_t>(p, p + hashLen);
            p += hashLen;
            spdlog::debug("Parsed DG{} hash: {} bytes", dgNumber, hashLen);
        }

        p = dgHashEnd;
    }

    spdlog::info("Parsed LDSSecurityObject: version={}, hashAlg={}, {} DG hashes",
        result.version, result.hashAlgorithmOid, result.dgHashes.size());

    return result;
}

// ============================================================
// Helper Methods
// ============================================================

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
    std::vector<uint8_t> cmsBytes = unwrapIcaoSod(sodBytes);

    BIO* bio = BIO_new_mem_buf(cmsBytes.data(), static_cast<int>(cmsBytes.size()));
    if (!bio) {
        spdlog::error("Failed to create BIO for DG hash parsing");
        return {};
    }
    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (!cms) {
        spdlog::error("Failed to parse CMS for DG hashes");
        return {};
    }

    auto ldsOpt = parseLdsSecurityObject(cms);
    CMS_ContentInfo_free(cms);

    if (ldsOpt) {
        return ldsOpt->dgHashes;
    }
    return {};
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
    if (oid.empty()) return "UNKNOWN";

    const auto& nameMap = isHash ? HASH_ALGORITHM_NAMES : SIGNATURE_ALGORITHM_NAMES;
    auto it = nameMap.find(oid);
    if (it != nameMap.end()) {
        return it->second;
    }

    // Unknown OID — return OID as-is with warning instead of silently returning wrong algorithm
    spdlog::warn("Unknown {} algorithm OID: {} — returning OID as name",
        isHash ? "hash" : "signature", oid);
    return oid;
}

const std::map<std::string, std::string>& SodParser::getHashAlgorithmNames() {
    return HASH_ALGORITHM_NAMES;
}

const std::map<std::string, std::string>& SodParser::getSignatureAlgorithmNames() {
    return SIGNATURE_ALGORITHM_NAMES;
}

// ============================================================
// API-Specific Methods
// ============================================================

Json::Value SodParser::parseSodForApi(const std::vector<uint8_t>& sodBytes) {
    spdlog::debug("Parsing SOD for API response ({} bytes)", sodBytes.size());

    Json::Value result;
    result["success"] = true;
    result["sodSize"] = static_cast<int>(sodBytes.size());

    try {
        // Extract hash algorithm (from LDSSecurityObject)
        std::string hashAlgorithm = extractHashAlgorithm(sodBytes);
        std::string hashAlgorithmOid = extractHashAlgorithmOid(sodBytes);
        result["hashAlgorithm"] = hashAlgorithm;
        result["hashAlgorithmOid"] = hashAlgorithmOid;

        // Extract signature algorithm (from CMS SignerInfo)
        std::string signatureAlgorithm = extractSignatureAlgorithm(sodBytes);
        std::string signatureAlgorithmOid = extractSignatureAlgorithmOid(sodBytes);
        result["signatureAlgorithm"] = signatureAlgorithm;
        result["signatureAlgorithmOid"] = signatureAlgorithmOid;

        // Extract CMS digest algorithm (may differ from LDS hash algorithm)
        std::string cmsDigestOid = extractCmsDigestAlgorithmOid(sodBytes);
        if (!cmsDigestOid.empty()) {
            result["cmsDigestAlgorithm"] = getAlgorithmName(cmsDigestOid, true);
            result["cmsDigestAlgorithmOid"] = cmsDigestOid;
        }

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

        // Check for DG14 (Active Authentication) and DG15 (Extended Access Control)
        if (!dgHashes.empty()) {
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
