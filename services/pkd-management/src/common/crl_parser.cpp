/**
 * @file crl_parser.cpp
 * @brief CRL binary parser implementation using OpenSSL
 * @date 2026-02-18
 */

#include "crl_parser.h"
#include <spdlog/spdlog.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/bn.h>
#include <openssl/bio.h>
#include <openssl/objects.h>
#include <sstream>
#include <iomanip>

namespace crl {

static std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::string data = hex;
    if (data.size() >= 2 && data[0] == '\\' && data[1] == 'x') {
        data = data.substr(2);
    }
    std::vector<uint8_t> bytes;
    bytes.reserve(data.length() / 2);
    for (size_t i = 0; i + 1 < data.length(); i += 2) {
        char hexByte[3] = {data[i], data[i + 1], 0};
        bytes.push_back(static_cast<uint8_t>(strtol(hexByte, nullptr, 16)));
    }
    return bytes;
}

static std::string asn1TimeToString(const ASN1_TIME* asn1Time) {
    if (!asn1Time) return "";
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) return "";
    ASN1_TIME_print(bio, asn1Time);
    char* data;
    long len = BIO_get_mem_data(bio, &data);
    std::string result(data, len);
    BIO_free(bio);
    return result;
}

static std::string reasonCodeToString(int reason) {
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

CrlParsedInfo parseCrlBinary(const std::string& crlHex) {
    CrlParsedInfo info;
    if (crlHex.empty()) return info;

    std::vector<uint8_t> crlBytes = hexToBytes(crlHex);
    if (crlBytes.empty()) return info;

    const unsigned char* p = crlBytes.data();
    X509_CRL* crl = d2i_X509_CRL(nullptr, &p, static_cast<long>(crlBytes.size()));
    if (!crl) {
        spdlog::warn("[CrlParser] Failed to parse CRL binary ({} bytes)", crlBytes.size());
        return info;
    }

    info.parsed = true;

    X509_NAME* issuer = X509_CRL_get_issuer(crl);
    if (issuer) {
        char* issuerStr = X509_NAME_oneline(issuer, nullptr, 0);
        if (issuerStr) {
            info.issuerDn = issuerStr;
            OPENSSL_free(issuerStr);
        }
    }

    info.thisUpdate = asn1TimeToString(X509_CRL_get0_lastUpdate(crl));
    info.nextUpdate = asn1TimeToString(X509_CRL_get0_nextUpdate(crl));

    const X509_ALGOR* sigAlg = X509_CRL_get0_tbs_sigalg(crl);
    if (sigAlg) {
        int sigNid = OBJ_obj2nid(sigAlg->algorithm);
        info.signatureAlgorithm = (sigNid != NID_undef) ? OBJ_nid2sn(sigNid) : "Unknown";
    }

    STACK_OF(X509_REVOKED)* revokedList = X509_CRL_get_REVOKED(crl);
    int numRevoked = (revokedList) ? sk_X509_REVOKED_num(revokedList) : 0;
    info.revokedCount = numRevoked;

    info.revokedCertificates.reserve(numRevoked);
    for (int i = 0; i < numRevoked; i++) {
        X509_REVOKED* revoked = sk_X509_REVOKED_value(revokedList, i);
        RevokedCertificateInfo entry;

        const ASN1_INTEGER* serial = X509_REVOKED_get0_serialNumber(revoked);
        if (serial) {
            BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
            if (bn) {
                char* hex = BN_bn2hex(bn);
                if (hex) { entry.serialNumber = hex; OPENSSL_free(hex); }
                BN_free(bn);
            }
        }

        entry.revocationDate = asn1TimeToString(X509_REVOKED_get0_revocationDate(revoked));

        int criticalFlag;
        ASN1_ENUMERATED* reasonExt = static_cast<ASN1_ENUMERATED*>(
            X509_REVOKED_get_ext_d2i(revoked, NID_crl_reason, &criticalFlag, nullptr));
        if (reasonExt) {
            entry.revocationReason = reasonCodeToString(ASN1_ENUMERATED_get(reasonExt));
            ASN1_ENUMERATED_free(reasonExt);
        } else {
            entry.revocationReason = "unspecified";
        }

        info.revokedCertificates.push_back(std::move(entry));
    }

    X509_CRL_free(crl);
    return info;
}

int getRevokedCount(const std::string& crlHex) {
    if (crlHex.empty()) return -1;
    std::vector<uint8_t> crlBytes = hexToBytes(crlHex);
    if (crlBytes.empty()) return -1;

    const unsigned char* p = crlBytes.data();
    X509_CRL* crl = d2i_X509_CRL(nullptr, &p, static_cast<long>(crlBytes.size()));
    if (!crl) return -1;

    STACK_OF(X509_REVOKED)* revokedList = X509_CRL_get_REVOKED(crl);
    int count = (revokedList) ? sk_X509_REVOKED_num(revokedList) : 0;
    X509_CRL_free(crl);
    return count;
}

} // namespace crl
