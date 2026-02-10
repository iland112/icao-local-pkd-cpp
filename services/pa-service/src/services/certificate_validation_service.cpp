/**
 * @file certificate_validation_service.cpp
 * @brief Implementation of CertificateValidationService
 */

#include "certificate_validation_service.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <openssl/err.h>
#include <openssl/bn.h>

namespace services {

namespace {
/**
 * @brief Convert ASN1_INTEGER to hex string
 * @param serial ASN1_INTEGER pointer
 * @return Hex string representation (e.g., "01:23:45:67")
 */
std::string serialNumberToString(ASN1_INTEGER* serial) {
    if (!serial) return "";

    BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
    if (!bn) return "";

    char* hex = BN_bn2hex(bn);
    std::string result;
    if (hex) {
        // Convert to uppercase and add colons between bytes
        std::string hexStr(hex);
        for (size_t i = 0; i < hexStr.length(); i += 2) {
            if (i > 0) result += ":";
            result += hexStr.substr(i, 2);
        }
        OPENSSL_free(hex);
    }
    BN_free(bn);

    return result;
}
} // anonymous namespace

CertificateValidationService::CertificateValidationService(
    repositories::LdapCertificateRepository* certRepo,
    repositories::LdapCrlRepository* crlRepo)
    : certRepo_(certRepo), crlRepo_(crlRepo)
{
    if (!certRepo_ || !crlRepo_) {
        throw std::invalid_argument("Repository dependencies cannot be null");
    }
    spdlog::debug("CertificateValidationService initialized");
}

domain::models::CertificateChainValidation CertificateValidationService::validateCertificateChain(
    X509* dscCert,
    const std::string& countryCode)
{
    domain::models::CertificateChainValidation result;

    if (!dscCert) {
        return domain::models::CertificateChainValidation::createInvalid("DSC certificate is null");
    }

    spdlog::info("Validating certificate chain for country: {}", countryCode);

    try {
        // Extract DSC information
        result.dscSubject = getSubjectDn(dscCert);
        result.dscIssuer = getIssuerDn(dscCert);

        ASN1_INTEGER* serial = X509_get_serialNumber(dscCert);
        if (serial) {
            result.dscSerialNumber = serialNumberToString(serial);
        }

        // Check DSC expiration
        result.dscExpired = isCertificateExpired(dscCert);

        // Extract country code from DSC issuer DN if not provided
        std::string effectiveCountry = countryCode;
        if (effectiveCountry.empty()) {
            // Try to extract country from issuer DN (e.g., /C=KR/O=Gov/CN=CSCA)
            size_t cPos = result.dscIssuer.find("/C=");
            if (cPos != std::string::npos) {
                cPos += 3; // Skip "/C="
                size_t endPos = result.dscIssuer.find('/', cPos);
                if (endPos != std::string::npos) {
                    effectiveCountry = result.dscIssuer.substr(cPos, endPos - cPos);
                } else {
                    effectiveCountry = result.dscIssuer.substr(cPos);
                }
                spdlog::info("Extracted country code from DSC issuer: {}", effectiveCountry);
            }
        }

        // Store extracted country code in result for caller use
        result.countryCode = effectiveCountry;

        // Find CSCA certificate
        X509* cscaCert = certRepo_->findCscaByIssuerDn(result.dscIssuer, effectiveCountry);
        if (!cscaCert) {
            result.valid = false;
            result.validationErrors = "CSCA not found for issuer: " + result.dscIssuer;
            result.expirationStatus = "INVALID";
            return result;
        }

        // Extract CSCA information
        result.cscaSubject = getSubjectDn(cscaCert);
        serial = X509_get_serialNumber(cscaCert);
        if (serial) {
            result.cscaSerialNumber = serialNumberToString(serial);
        }

        result.cscaExpired = isCertificateExpired(cscaCert);

        // Verify signature
        result.signatureVerified = verifyCertificateSignature(dscCert, cscaCert);

        // Check CRL (ICAO Doc 9303 Part 11 - Certificate Revocation)
        result.crlStatus = checkCrlStatus(dscCert, effectiveCountry);
        result.crlChecked = (result.crlStatus != domain::models::CrlStatus::NOT_CHECKED);
        result.revoked = (result.crlStatus == domain::models::CrlStatus::REVOKED);

        // Set CRL status messages (ICAO Doc 9303 compliant)
        switch (result.crlStatus) {
            case domain::models::CrlStatus::VALID:
                result.crlStatusDescription = "Certificate Revocation List (CRL) check passed";
                result.crlStatusDetailedDescription = "The Document Signer Certificate (DSC) was verified against the Certificate Revocation List (CRL) as specified in ICAO Doc 9303 Part 11. The certificate is not revoked and remains valid for Passive Authentication.";
                result.crlStatusSeverity = "INFO";
                result.crlMessage = "DSC verified - not revoked";
                break;
            case domain::models::CrlStatus::REVOKED:
                result.crlStatusDescription = "Certificate has been revoked by issuing authority";
                result.crlStatusDetailedDescription = "The Document Signer Certificate (DSC) appears on the Certificate Revocation List (CRL) published by the issuing Country Signing CA (CSCA). According to RFC 5280 and ICAO Doc 9303 Part 11, this certificate must not be used for Passive Authentication verification.";
                result.crlStatusSeverity = "CRITICAL";
                result.crlMessage = "DSC is revoked - PA verification FAILED";
                break;
            case domain::models::CrlStatus::CRL_UNAVAILABLE:
                result.crlStatusDescription = "Certificate Revocation List (CRL) not available";
                result.crlStatusDetailedDescription = "No CRL was found in the LDAP PKD for this issuing country. ICAO Doc 9303 Part 11 specifies CRL checking as RECOMMENDED but not mandatory. According to the principle of fail-open for unavailable infrastructure, this verification continues with a warning.";
                result.crlStatusSeverity = "WARNING";
                result.crlMessage = "CRL not found - proceeding with caution";
                break;
            case domain::models::CrlStatus::CRL_EXPIRED:
                result.crlStatusDescription = "Certificate Revocation List (CRL) has expired";
                result.crlStatusDetailedDescription = "The CRL retrieved from the PKD has passed its nextUpdate time as defined in RFC 5280. An expired CRL cannot be relied upon for revocation status. ICAO Doc 9303 Part 11 recommends treating expired CRLs with caution, as they may not reflect recent revocations.";
                result.crlStatusSeverity = "WARNING";
                result.crlMessage = "CRL expired - revocation status uncertain";
                break;
            case domain::models::CrlStatus::CRL_INVALID:
                result.crlStatusDescription = "Certificate Revocation List (CRL) signature verification failed";
                result.crlStatusDetailedDescription = "The digital signature on the CRL could not be verified against the issuing CSCA's public key. This indicates either CRL corruption or a security compromise. Per RFC 5280 Section 6.3, an invalid CRL must not be used for certificate validation.";
                result.crlStatusSeverity = "CRITICAL";
                result.crlMessage = "CRL signature invalid - cannot verify revocation";
                break;
            case domain::models::CrlStatus::NOT_CHECKED:
                result.crlStatusDescription = "Certificate revocation check was not performed";
                result.crlStatusDetailedDescription = "CRL checking was skipped or could not be completed. ICAO Doc 9303 Part 11 considers CRL verification as a SHOULD requirement rather than MUST. This is acceptable in environments where CRL infrastructure is not fully deployed.";
                result.crlStatusSeverity = "INFO";
                result.crlMessage = "CRL check skipped";
                break;
        }

        // Overall validation
        result.valid = result.signatureVerified && !result.revoked;

        // Expiration status
        if (result.dscExpired || result.cscaExpired) {
            result.expirationStatus = "EXPIRED";
        } else {
            result.expirationStatus = "VALID";
        }

        result.trustChainPath = "DSC → " + result.cscaSubject.substr(0, 50);
        result.trustChainDepth = 2;

        X509_free(cscaCert);

    } catch (const std::exception& e) {
        spdlog::error("Certificate chain validation failed: {}", e.what());
        result.valid = false;
        result.validationErrors = e.what();
    }

    return result;
}

bool CertificateValidationService::verifyCertificateSignature(X509* cert, X509* issuerCert) {
    if (!cert || !issuerCert) return false;

    EVP_PKEY* issuerPubKey = X509_get_pubkey(issuerCert);
    if (!issuerPubKey) {
        spdlog::error("Failed to extract public key from issuer");
        return false;
    }

    int verifyResult = X509_verify(cert, issuerPubKey);
    EVP_PKEY_free(issuerPubKey);

    if (verifyResult != 1) {
        unsigned long err = ERR_get_error();
        char errBuf[256];
        ERR_error_string_n(err, errBuf, sizeof(errBuf));
        spdlog::warn("Signature verification failed: {}", errBuf);
        return false;
    }

    spdlog::debug("Certificate signature VALID");
    return true;
}

bool CertificateValidationService::isCertificateExpired(X509* cert) {
    if (!cert) return true;

    time_t now = time(nullptr);
    return (X509_cmp_time(X509_get0_notAfter(cert), &now) < 0);
}

domain::models::CrlStatus CertificateValidationService::checkCrlStatus(
    X509* cert,
    const std::string& countryCode)
{
    X509_CRL* crl = crlRepo_->findCrlByCountry(countryCode);
    if (!crl) {
        return domain::models::CrlStatus::CRL_UNAVAILABLE;
    }

    if (crlRepo_->isCrlExpired(crl)) {
        X509_CRL_free(crl);
        return domain::models::CrlStatus::CRL_EXPIRED;
    }

    bool revoked = crlRepo_->isCertificateRevoked(cert, crl);
    X509_CRL_free(crl);

    return revoked ? domain::models::CrlStatus::REVOKED : domain::models::CrlStatus::VALID;
}

std::vector<X509*> CertificateValidationService::buildTrustChain(
    X509* dscCert,
    const std::string& countryCode)
{
    std::vector<X509*> chain;
    chain.push_back(dscCert);

    std::string issuerDn = getIssuerDn(dscCert);
    X509* cscaCert = certRepo_->findCscaByIssuerDn(issuerDn, countryCode);

    if (cscaCert) {
        chain.push_back(cscaCert);
    }

    return chain;
}

std::string CertificateValidationService::getSubjectDn(X509* cert) {
    if (!cert) return "";
    X509_NAME* subject = X509_get_subject_name(cert);
    char buf[512];
    X509_NAME_oneline(subject, buf, sizeof(buf));
    return std::string(buf);
}

std::string CertificateValidationService::getIssuerDn(X509* cert) {
    if (!cert) return "";
    X509_NAME* issuer = X509_get_issuer_name(cert);
    char buf[512];
    X509_NAME_oneline(issuer, buf, sizeof(buf));
    return std::string(buf);
}

bool CertificateValidationService::isSelfSigned(X509* cert) {
    return getSubjectDn(cert) == getIssuerDn(cert);
}

} // namespace services
