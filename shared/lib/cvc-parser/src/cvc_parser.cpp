/**
 * @file cvc_parser.cpp
 * @brief CVC Certificate parser implementation
 */

#include "icao/cvc/cvc_parser.h"
#include "icao/cvc/chat_decoder.h"
#include "icao/cvc/eac_oids.h"
#include "icao/cvc/tlv.h"

#include <openssl/evp.h>

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace icao::cvc {

std::optional<CvcCertificate> CvcParser::parse(const uint8_t* data, size_t dataLen) {
    if (!data || dataLen < 4) return std::nullopt;

    // Parse outer CV Certificate [0x7F21]
    auto outerTlv = TlvParser::parse(data, dataLen);
    if (!outerTlv || outerTlv->tag != tag::CV_CERTIFICATE) {
        return std::nullopt;
    }

    CvcCertificate cert;
    cert.rawBinary.assign(data, data + outerTlv->totalLength);

    // Compute SHA-256 fingerprint
    cert.fingerprintSha256 = computeSha256(data, outerTlv->totalLength);

    // Parse children of CV Certificate: Body [0x7F4E] + Signature [0x5F37]
    auto children = TlvParser::parseChildren(outerTlv->valuePtr, outerTlv->valueLength);

    bool hasBody = false;
    for (const auto& child : children) {
        if (child.tag == tag::CERTIFICATE_BODY) {
            cert.bodyRaw = child.value;
            if (!parseBody(child.valuePtr, child.valueLength, cert)) {
                return std::nullopt;
            }
            hasBody = true;
        } else if (child.tag == tag::SIGNATURE) {
            cert.signature = child.value;
        }
    }

    if (!hasBody) return std::nullopt;

    // Infer CVC type from CAR/CHR
    cert.type = inferCvcType(cert.car, cert.chr);

    // Extract country code from CHR (first 2 chars)
    if (cert.chr.size() >= 2) {
        cert.countryCode = cert.chr.substr(0, 2);
    }

    return cert;
}

std::optional<CvcCertificate> CvcParser::parse(const std::vector<uint8_t>& data) {
    return parse(data.data(), data.size());
}

bool CvcParser::parseBody(const uint8_t* data, size_t dataLen, CvcCertificate& cert) {
    auto children = TlvParser::parseChildren(data, dataLen);

    for (const auto& child : children) {
        switch (child.tag) {
            case tag::CERTIFICATE_PROFILE_ID:
                if (!child.value.empty()) {
                    cert.profileIdentifier = child.value[0];
                }
                break;

            case tag::CAR:
                cert.car = std::string(child.value.begin(), child.value.end());
                break;

            case tag::CHR:
                cert.chr = std::string(child.value.begin(), child.value.end());
                break;

            case tag::PUBLIC_KEY:
                if (!parsePublicKey(child.valuePtr, child.valueLength, cert.publicKey)) {
                    return false;
                }
                break;

            case tag::CHAT:
                if (!parseChat(child.valuePtr, child.valueLength, cert.chat)) {
                    return false;
                }
                break;

            case tag::EFFECTIVE_DATE:
                cert.effectiveDate = TlvParser::decodeBcdDate(child.value);
                break;

            case tag::EXPIRATION_DATE:
                cert.expirationDate = TlvParser::decodeBcdDate(child.value);
                break;

            default:
                // Unknown tags are silently skipped (forward compatibility)
                break;
        }
    }

    // CAR and CHR are mandatory
    return !cert.car.empty() && !cert.chr.empty();
}

bool CvcParser::parsePublicKey(const uint8_t* data, size_t dataLen, CvcPublicKey& pk) {
    auto children = TlvParser::parseChildren(data, dataLen);

    for (const auto& child : children) {
        switch (child.tag) {
            case tag::OID:
                pk.algorithmOid = TlvParser::decodeOid(child.value);
                pk.algorithmName = getAlgorithmName(pk.algorithmOid);
                break;

            case tag::PK_MODULUS:
                // RSA: modulus; ECDSA: prime p
                if (isRsaAlgorithm(pk.algorithmOid)) {
                    pk.modulus = child.value;
                } else {
                    pk.prime = child.value;
                }
                break;

            case tag::PK_EXPONENT:
                // RSA: exponent; ECDSA: coefficient a
                if (isRsaAlgorithm(pk.algorithmOid)) {
                    pk.exponent = child.value;
                } else {
                    pk.coeffA = child.value;
                }
                break;

            case tag::PK_COEFF_B:
                pk.coeffB = child.value;
                break;

            case tag::PK_GENERATOR:
                pk.generator = child.value;
                break;

            case tag::PK_ORDER:
                pk.order = child.value;
                break;

            case tag::PK_PUBLIC_POINT:
                pk.publicPoint = child.value;
                break;

            case tag::PK_COFACTOR:
                pk.cofactor = child.value;
                break;

            default:
                break;
        }
    }

    return !pk.algorithmOid.empty();
}

bool CvcParser::parseChat(const uint8_t* data, size_t dataLen, ChatInfo& chat) {
    auto children = TlvParser::parseChildren(data, dataLen);

    for (const auto& child : children) {
        if (child.tag == tag::OID) {
            chat.roleOid = TlvParser::decodeOid(child.value);
            chat.role = ChatDecoder::decodeRole(chat.roleOid);
        } else if (child.tag == tag::DISCRETIONARY_DATA) {
            chat.authorizationBits = child.value;
            chat.permissions = ChatDecoder::decodePermissions(chat.role, child.value);
        }
    }

    return !chat.roleOid.empty();
}

CvcType CvcParser::inferCvcType(const std::string& car, const std::string& chr) {
    // CVCA: self-signed (CAR == CHR)
    if (car == chr) {
        return CvcType::CVCA;
    }

    // Extract country codes (first 2 chars)
    std::string carCountry = (car.size() >= 2) ? car.substr(0, 2) : "";
    std::string chrCountry = (chr.size() >= 2) ? chr.substr(0, 2) : "";

    // Check if CHR contains "DV" pattern (case-insensitive)
    std::string chrUpper = chr;
    std::transform(chrUpper.begin(), chrUpper.end(), chrUpper.begin(),
                   [](unsigned char c) { return std::toupper(c); });

    bool isDv = chrUpper.find("DV") != std::string::npos;

    if (isDv) {
        return (carCountry == chrCountry) ? CvcType::DV_DOMESTIC : CvcType::DV_FOREIGN;
    }

    return CvcType::IS;
}

std::string CvcParser::computeSha256(const uint8_t* data, size_t dataLen) {
    if (!data || dataLen == 0) return "";

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return "";

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hashLen = 0;

    bool ok = EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) == 1
           && EVP_DigestUpdate(ctx, data, dataLen) == 1
           && EVP_DigestFinal_ex(ctx, hash, &hashLen) == 1;

    EVP_MD_CTX_free(ctx);

    if (!ok || hashLen == 0) return "";

    std::ostringstream oss;
    for (unsigned int i = 0; i < hashLen; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return oss.str();
}

} // namespace icao::cvc
