/**
 * @file certificate_validation_service.cpp
 * @brief Implementation of CertificateValidationService
 */

#include "certificate_validation_service.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <openssl/err.h>

namespace services {

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
            char* serialStr = i2s_ASN1_INTEGER(nullptr, serial);
            if (serialStr) {
                result.dscSerialNumber = serialStr;
                OPENSSL_free(serialStr);
            }
        }

        // Check DSC expiration
        result.dscExpired = isCertificateExpired(dscCert);

        // Find CSCA certificate
        X509* cscaCert = certRepo_->findCscaByIssuerDn(result.dscIssuer, countryCode);
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
            char* serialStr = i2s_ASN1_INTEGER(nullptr, serial);
            if (serialStr) {
                result.cscaSerialNumber = serialStr;
                OPENSSL_free(serialStr);
            }
        }

        result.cscaExpired = isCertificateExpired(cscaCert);

        // Verify signature
        result.signatureVerified = verifyCertificateSignature(dscCert, cscaCert);

        // Check CRL
        result.crlStatus = checkCrlStatus(dscCert, countryCode);
        result.crlChecked = (result.crlStatus != domain::models::CrlStatus::NOT_CHECKED);
        result.revoked = (result.crlStatus == domain::models::CrlStatus::REVOKED);

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
