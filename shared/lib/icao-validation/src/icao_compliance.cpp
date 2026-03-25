/**
 * @file icao_compliance.cpp
 * @brief ICAO Doc 9303 compliance checking implementation
 */
#include "icao/validation/icao_compliance.h"

#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/asn1.h>
#include <algorithm>
#include <sstream>

namespace icao {
namespace validation {

std::string IcaoComplianceResult::violationsString() const {
    std::string result;
    for (size_t i = 0; i < violations.size(); ++i) {
        if (i > 0) result += "|";
        result += violations[i];
    }
    return result;
}

// --- Internal helpers ---
namespace {

struct CertMeta {
    std::string publicKeyAlgorithm;
    int publicKeySize = 0;
    std::string publicKeyCurve;
    std::string signatureHashAlgorithm;
    std::vector<std::string> keyUsage;
    bool isCA = false;
    bool isSelfSigned = false;
};

CertMeta extractMeta(X509* cert) {
    CertMeta m;
    if (!cert) return m;

    // Public key info
    EVP_PKEY* pkey = X509_get0_pubkey(cert);
    if (pkey) {
        int pkType = EVP_PKEY_base_id(pkey);
        if (pkType == EVP_PKEY_RSA) m.publicKeyAlgorithm = "RSA";
        else if (pkType == EVP_PKEY_EC) m.publicKeyAlgorithm = "ECDSA";
        else if (pkType == EVP_PKEY_DSA) m.publicKeyAlgorithm = "DSA";
        else m.publicKeyAlgorithm = OBJ_nid2sn(pkType);
        m.publicKeySize = EVP_PKEY_bits(pkey);

        // EC curve name
        if (pkType == EVP_PKEY_EC) {
            const EC_KEY* ec = EVP_PKEY_get0_EC_KEY(pkey);
            if (ec) {
                const EC_GROUP* group = EC_KEY_get0_group(ec);
                if (group) {
                    int nid = EC_GROUP_get_curve_name(group);
                    if (nid != NID_undef) {
                        m.publicKeyCurve = OBJ_nid2sn(nid);
                    }
                }
            }
        }
    }

    // Signature hash algorithm
    const X509_ALGOR* sigAlgor = nullptr;
    X509_get0_signature(nullptr, &sigAlgor, cert);
    if (sigAlgor) {
        int sigNid = OBJ_obj2nid(sigAlgor->algorithm);
        // Extract hash from combined sig algorithm
        int hashNid = NID_undef, pkNid = NID_undef;
        OBJ_find_sigid_algs(sigNid, &hashNid, &pkNid);
        if (hashNid != NID_undef) {
            m.signatureHashAlgorithm = OBJ_nid2sn(hashNid);
        }
    }

    // Key usage
    ASN1_BIT_STRING* usage = static_cast<ASN1_BIT_STRING*>(
        X509_get_ext_d2i(cert, NID_key_usage, nullptr, nullptr));
    if (usage) {
        if (ASN1_BIT_STRING_get_bit(usage, 0)) m.keyUsage.push_back("digitalSignature");
        if (ASN1_BIT_STRING_get_bit(usage, 5)) m.keyUsage.push_back("keyCertSign");
        if (ASN1_BIT_STRING_get_bit(usage, 6)) m.keyUsage.push_back("cRLSign");
        ASN1_BIT_STRING_free(usage);
    }

    // Basic constraints (CA flag)
    BASIC_CONSTRAINTS* bc = static_cast<BASIC_CONSTRAINTS*>(
        X509_get_ext_d2i(cert, NID_basic_constraints, nullptr, nullptr));
    if (bc) {
        m.isCA = (bc->ca != 0);
        BASIC_CONSTRAINTS_free(bc);
    }

    // Self-signed check
    m.isSelfSigned = (X509_name_cmp(
        X509_get_subject_name(cert), X509_get_issuer_name(cert)) == 0);

    return m;
}

} // anonymous namespace

// --- Main compliance check ---

IcaoComplianceResult checkIcaoCompliance(X509* cert, const std::string& certType) {
    IcaoComplianceResult status;

    if (!cert) {
        status.isCompliant = false;
        status.complianceLevel = "ERROR";
        status.violations.push_back("NULL certificate pointer");
        return status;
    }

    // DSC_NC: always non-conformant by ICAO classification
    if (certType == "DSC_NC") {
        status.isCompliant = false;
        status.complianceLevel = "NON_CONFORMANT";
        status.violations.push_back("ICAO PKD non-conformant DSC (classified by ICAO)");
        return status;
    }

    auto meta = extractMeta(cert);

    // --- 1. Key Usage Validation ---
    std::vector<std::string> requiredKeyUsage;

    if (certType == "CSCA") {
        requiredKeyUsage = {"keyCertSign", "cRLSign"};
        if (!meta.isCA) {
            status.keyUsageCompliant = false;
            status.violations.push_back("CSCA must have CA=TRUE");
        }
    } else if (certType == "DSC") {
        requiredKeyUsage = {"digitalSignature"};
        if (meta.isCA) {
            status.keyUsageCompliant = false;
            status.violations.push_back("DSC must have CA=FALSE");
        }
    } else if (certType == "MLSC") {
        requiredKeyUsage = {"keyCertSign"};
        if (!meta.isCA) {
            status.keyUsageCompliant = false;
            status.violations.push_back("MLSC must have CA=TRUE");
        }
    }

    for (const auto& req : requiredKeyUsage) {
        if (std::find(meta.keyUsage.begin(), meta.keyUsage.end(), req) == meta.keyUsage.end()) {
            status.keyUsageCompliant = false;
            status.violations.push_back("Missing required Key Usage: " + req);
        }
    }

    // --- 2. Signature Algorithm Validation ---
    std::string hashAlg = meta.signatureHashAlgorithm;
    std::transform(hashAlg.begin(), hashAlg.end(), hashAlg.begin(), ::tolower);

    bool approvedHash = (hashAlg.find("sha256") != std::string::npos ||
                         hashAlg.find("sha384") != std::string::npos ||
                         hashAlg.find("sha512") != std::string::npos);
    bool bsiHash = (hashAlg.find("sha224") != std::string::npos);
    bool deprecatedHash = (hashAlg.find("sha1") != std::string::npos);

    bool approvedPubKey = (meta.publicKeyAlgorithm == "RSA" || meta.publicKeyAlgorithm == "ECDSA");

    if (!approvedHash && !bsiHash && !deprecatedHash) {
        status.algorithmCompliant = false;
        status.violations.push_back("Hash algorithm not in Doc 9303 (SHA-256/384/512): " + meta.signatureHashAlgorithm);
    } else if (deprecatedHash) {
        status.complianceLevel = "WARNING";
        status.violations.push_back("SHA-1 deprecated per ICAO NTWG: " + meta.signatureHashAlgorithm);
    }

    if (!approvedPubKey) {
        status.algorithmCompliant = false;
        status.violations.push_back("Public key algorithm not in Doc 9303 (RSA/ECDSA): " + meta.publicKeyAlgorithm);
    }

    // --- 3. Key Size Validation ---
    if (meta.publicKeyAlgorithm == "RSA") {
        if (meta.publicKeySize < 2048) {
            status.keySizeCompliant = false;
            status.violations.push_back("RSA key below 2048 bits: " + std::to_string(meta.publicKeySize));
        }
    } else if (meta.publicKeyAlgorithm == "ECDSA" && !meta.publicKeyCurve.empty()) {
        bool doc9303Curve = (meta.publicKeyCurve == "prime256v1" || meta.publicKeyCurve == "secp256r1" ||
                             meta.publicKeyCurve == "secp384r1" || meta.publicKeyCurve == "secp521r1");
        bool bsiCurve = (meta.publicKeyCurve == "brainpoolP256r1" ||
                         meta.publicKeyCurve == "brainpoolP384r1" ||
                         meta.publicKeyCurve == "brainpoolP512r1");
        if (!doc9303Curve && !bsiCurve) {
            status.keySizeCompliant = false;
            status.violations.push_back("ECDSA curve not in Doc 9303/BSI: " + meta.publicKeyCurve);
        }
    }

    // --- 4. Validity Period Validation ---
    const ASN1_TIME* notBefore = X509_get0_notBefore(cert);
    const ASN1_TIME* notAfter = X509_get0_notAfter(cert);
    if (notBefore && notAfter) {
        int days = 0, secs = 0;
        if (ASN1_TIME_diff(&days, &secs, notBefore, notAfter)) {
            int years = days / 365;
            if (certType == "CSCA" && years > 15) {
                status.validityPeriodCompliant = false;
                status.violations.push_back("CSCA validity > 15 years: " + std::to_string(years));
            } else if (certType == "DSC" && years > 3) {
                if (status.complianceLevel != "NON_CONFORMANT")
                    status.complianceLevel = "WARNING";
                status.violations.push_back("DSC validity > 3 years: " + std::to_string(years));
            }
        }
    }

    // --- 5. DN Format Validation ---
    X509_NAME* subject = X509_get_subject_name(cert);
    if (subject) {
        if (X509_NAME_get_index_by_NID(subject, NID_countryName, -1) < 0) {
            status.dnFormatCompliant = false;
            status.violations.push_back("Subject DN missing Country (C) attribute");
        }
    } else {
        status.dnFormatCompliant = false;
        status.violations.push_back("No Subject DN");
    }

    // --- 6. Extensions Validation ---
    if ((certType == "CSCA" || certType == "MLSC")) {
        BASIC_CONSTRAINTS* bc2 = static_cast<BASIC_CONSTRAINTS*>(
            X509_get_ext_d2i(cert, NID_basic_constraints, nullptr, nullptr));
        if (!bc2) {
            status.extensionsCompliant = false;
            status.violations.push_back(certType + " missing Basic Constraints");
        } else {
            BASIC_CONSTRAINTS_free(bc2);
        }
    }
    if (meta.keyUsage.empty()) {
        status.extensionsCompliant = false;
        status.violations.push_back("Missing Key Usage extension");
    }

    // --- Final assessment ---
    if (!status.keyUsageCompliant || !status.algorithmCompliant ||
        !status.keySizeCompliant || !status.dnFormatCompliant ||
        !status.extensionsCompliant) {
        status.isCompliant = false;
        status.complianceLevel = "NON_CONFORMANT";
    } else if (!status.validityPeriodCompliant) {
        status.isCompliant = true;
        if (status.complianceLevel == "CONFORMANT")
            status.complianceLevel = "WARNING";
    }

    return status;
}

} // namespace validation
} // namespace icao
