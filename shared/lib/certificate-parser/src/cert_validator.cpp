#include "cert_validator.h"
#include <openssl/evp.h>
#include <openssl/err.h>
#include <openssl/asn1.h>
#include <ctime>
#include <sstream>

namespace icao {
namespace certificate_parser {

ValidationResult CertValidator::validate(X509* cert) {
    ValidationResult result;

    if (!cert) {
        result.errorMessage = "Null certificate";
        result.status = ValidationStatus::UNKNOWN_ERROR;
        return result;
    }

    // Check expiration
    checkExpiration(cert, result);
    if (result.isExpired) {
        result.status = ValidationStatus::EXPIRED;
        result.errorMessage = "Certificate has expired";
        return result;
    }
    if (result.isNotYetValid) {
        result.status = ValidationStatus::NOT_YET_VALID;
        result.errorMessage = "Certificate is not yet valid";
        return result;
    }

    // Check signature (self-signed only)
    checkSignature(cert, nullptr, result);
    if (!result.signatureVerified) {
        result.status = ValidationStatus::INVALID_SIGNATURE;
        result.errorMessage = "Self-signed signature verification failed";
        return result;
    }

    // Check purpose
    checkPurpose(cert, result);

    result.isValid = true;
    result.status = ValidationStatus::VALID;
    return result;
}

ValidationResult CertValidator::validate(X509* cert, X509* issuer) {
    ValidationResult result;

    if (!cert) {
        result.errorMessage = "Null certificate";
        result.status = ValidationStatus::UNKNOWN_ERROR;
        return result;
    }

    // Check expiration
    checkExpiration(cert, result);
    if (result.isExpired) {
        result.status = ValidationStatus::EXPIRED;
        result.errorMessage = "Certificate has expired";
        return result;
    }
    if (result.isNotYetValid) {
        result.status = ValidationStatus::NOT_YET_VALID;
        result.errorMessage = "Certificate is not yet valid";
        return result;
    }

    // Check signature with issuer
    checkSignature(cert, issuer, result);
    if (!result.signatureVerified) {
        result.status = ValidationStatus::INVALID_SIGNATURE;
        result.errorMessage = "Signature verification failed";
        return result;
    }

    // Build 1-level trust chain
    if (issuer) {
        result.trustChainValid = true;
        result.trustChainDepth = 1;

        // Get subject DNs
        char* certDn = X509_NAME_oneline(X509_get_subject_name(cert), nullptr, 0);
        char* issuerDn = X509_NAME_oneline(X509_get_subject_name(issuer), nullptr, 0);

        if (certDn) {
            result.trustChainPath.push_back(certDn);
            OPENSSL_free(certDn);
        }
        if (issuerDn) {
            result.trustChainPath.push_back(issuerDn);
            OPENSSL_free(issuerDn);
        }
    }

    // Check purpose
    checkPurpose(cert, result);

    result.isValid = true;
    result.status = ValidationStatus::VALID;
    return result;
}

ValidationResult CertValidator::validate(X509* cert, const std::vector<X509*>& trustChain) {
    ValidationResult result;

    if (!cert) {
        result.errorMessage = "Null certificate";
        result.status = ValidationStatus::UNKNOWN_ERROR;
        return result;
    }

    if (trustChain.empty()) {
        return validate(cert);
    }

    // Check expiration
    checkExpiration(cert, result);
    if (result.isExpired) {
        result.status = ValidationStatus::EXPIRED;
        result.errorMessage = "Certificate has expired";
        return result;
    }
    if (result.isNotYetValid) {
        result.status = ValidationStatus::NOT_YET_VALID;
        result.errorMessage = "Certificate is not yet valid";
        return result;
    }

    // Verify with first issuer in chain
    X509* issuer = trustChain[0];
    checkSignature(cert, issuer, result);
    if (!result.signatureVerified) {
        result.status = ValidationStatus::INVALID_SIGNATURE;
        result.errorMessage = "Signature verification failed";
        return result;
    }

    // Build trust chain path
    result.trustChainValid = true;
    result.trustChainDepth = static_cast<int>(trustChain.size());

    char* certDn = X509_NAME_oneline(X509_get_subject_name(cert), nullptr, 0);
    if (certDn) {
        result.trustChainPath.push_back(certDn);
        OPENSSL_free(certDn);
    }

    for (X509* chainCert : trustChain) {
        char* dn = X509_NAME_oneline(X509_get_subject_name(chainCert), nullptr, 0);
        if (dn) {
            result.trustChainPath.push_back(dn);
            OPENSSL_free(dn);
        }
    }

    // Check purpose
    checkPurpose(cert, result);

    result.isValid = true;
    result.status = ValidationStatus::VALID;
    return result;
}

bool CertValidator::isExpired(X509* cert) {
    if (!cert) {
        return true;
    }

    const ASN1_TIME* notAfter = X509_get0_notAfter(cert);
    if (!notAfter) {
        return true;
    }

    return X509_cmp_current_time(notAfter) < 0;
}

bool CertValidator::isNotYetValid(X509* cert) {
    if (!cert) {
        return false;
    }

    const ASN1_TIME* notBefore = X509_get0_notBefore(cert);
    if (!notBefore) {
        return false;
    }

    return X509_cmp_current_time(notBefore) > 0;
}

bool CertValidator::verifySignature(X509* cert, X509* issuer) {
    if (!cert) {
        return false;
    }

    EVP_PKEY* pkey = nullptr;

    if (issuer) {
        // Verify with issuer public key
        pkey = X509_get_pubkey(issuer);
    } else {
        // Self-signed: verify with own public key
        pkey = X509_get_pubkey(cert);
    }

    if (!pkey) {
        return false;
    }

    int result = X509_verify(cert, pkey);
    EVP_PKEY_free(pkey);

    return result == 1;
}

std::vector<std::string> CertValidator::getKeyUsages(X509* cert) {
    std::vector<std::string> usages;

    if (!cert) {
        return usages;
    }

    // Get key usage extension
    ASN1_BIT_STRING* usage = (ASN1_BIT_STRING*)X509_get_ext_d2i(
        cert, NID_key_usage, nullptr, nullptr
    );

    if (!usage) {
        return usages;
    }

    // Check each key usage bit
    if (ASN1_BIT_STRING_get_bit(usage, 0)) usages.push_back("digitalSignature");
    if (ASN1_BIT_STRING_get_bit(usage, 1)) usages.push_back("nonRepudiation");
    if (ASN1_BIT_STRING_get_bit(usage, 2)) usages.push_back("keyEncipherment");
    if (ASN1_BIT_STRING_get_bit(usage, 3)) usages.push_back("dataEncipherment");
    if (ASN1_BIT_STRING_get_bit(usage, 4)) usages.push_back("keyAgreement");
    if (ASN1_BIT_STRING_get_bit(usage, 5)) usages.push_back("keyCertSign");
    if (ASN1_BIT_STRING_get_bit(usage, 6)) usages.push_back("cRLSign");

    ASN1_BIT_STRING_free(usage);
    return usages;
}

std::vector<std::string> CertValidator::getExtendedKeyUsages(X509* cert) {
    std::vector<std::string> usages;

    if (!cert) {
        return usages;
    }

    // Get extended key usage extension
    EXTENDED_KEY_USAGE* extUsage = (EXTENDED_KEY_USAGE*)X509_get_ext_d2i(
        cert, NID_ext_key_usage, nullptr, nullptr
    );

    if (!extUsage) {
        return usages;
    }

    int count = sk_ASN1_OBJECT_num(extUsage);
    for (int i = 0; i < count; i++) {
        ASN1_OBJECT* obj = sk_ASN1_OBJECT_value(extUsage, i);
        if (obj) {
            char buf[128];
            OBJ_obj2txt(buf, sizeof(buf), obj, 0);
            usages.push_back(buf);
        }
    }

    sk_ASN1_OBJECT_pop_free(extUsage, ASN1_OBJECT_free);
    return usages;
}

std::string CertValidator::getSignatureAlgorithm(X509* cert) {
    if (!cert) {
        return "";
    }

    const X509_ALGOR* sigAlg = X509_get0_tbs_sigalg(cert);
    if (!sigAlg) {
        return "";
    }

    const ASN1_OBJECT* obj = nullptr;
    X509_ALGOR_get0(&obj, nullptr, nullptr, sigAlg);

    if (!obj) {
        return "";
    }

    char buf[128];
    OBJ_obj2txt(buf, sizeof(buf), obj, 0);
    return buf;
}

void CertValidator::checkExpiration(X509* cert, ValidationResult& result) {
    const ASN1_TIME* notBefore = X509_get0_notBefore(cert);
    const ASN1_TIME* notAfter = X509_get0_notAfter(cert);

    if (notBefore) {
        result.notBefore = asn1TimeToTimePoint(notBefore);
    }

    if (notAfter) {
        result.notAfter = asn1TimeToTimePoint(notAfter);
    }

    result.isExpired = isExpired(cert);
    result.isNotYetValid = isNotYetValid(cert);
}

void CertValidator::checkSignature(X509* cert, X509* issuer, ValidationResult& result) {
    result.signatureVerified = verifySignature(cert, issuer);
    result.signatureAlgorithm = getSignatureAlgorithm(cert);
}

void CertValidator::checkPurpose(X509* cert, ValidationResult& result) {
    result.keyUsages = getKeyUsages(cert);
    result.extendedKeyUsages = getExtendedKeyUsages(cert);

    // Purpose is valid if we can extract usage information
    result.purposeValid = true;
}

std::optional<std::chrono::system_clock::time_point> CertValidator::asn1TimeToTimePoint(
    const ASN1_TIME* asn1Time
) {
    if (!asn1Time) {
        return std::nullopt;
    }

    // Convert ASN1_TIME to tm structure
    struct tm timeInfo = {};
    if (ASN1_TIME_to_tm(asn1Time, &timeInfo) != 1) {
        return std::nullopt;
    }

    // Convert tm to time_t
    time_t tt = timegm(&timeInfo);
    if (tt == -1) {
        return std::nullopt;
    }

    // Convert to system_clock time_point
    return std::chrono::system_clock::from_time_t(tt);
}

} // namespace certificate_parser
} // namespace icao
