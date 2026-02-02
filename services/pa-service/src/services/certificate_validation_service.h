/**
 * @file certificate_validation_service.h
 * @brief Service for certificate chain validation
 *
 * @author SmartCore Inc.
 * @date 2026-02-01
 */

#pragma once

#include <string>
#include <vector>
#include <openssl/x509.h>
#include "../domain/models/certificate_chain_validation.h"
#include "../repositories/ldap_certificate_repository.h"
#include "../repositories/ldap_crl_repository.h"

namespace services {

class CertificateValidationService {
private:
    repositories::LdapCertificateRepository* certRepo_;
    repositories::LdapCrlRepository* crlRepo_;

public:
    CertificateValidationService(
        repositories::LdapCertificateRepository* certRepo,
        repositories::LdapCrlRepository* crlRepo
    );
    ~CertificateValidationService() = default;

    // Main validation method
    domain::models::CertificateChainValidation validateCertificateChain(
        X509* dscCert,
        const std::string& countryCode
    );

    // Certificate operations
    bool verifyCertificateSignature(X509* cert, X509* issuerCert);
    bool isCertificateExpired(X509* cert);

    // CRL checking
    domain::models::CrlStatus checkCrlStatus(X509* cert, const std::string& countryCode);

    // Trust chain building
    std::vector<X509*> buildTrustChain(X509* dscCert, const std::string& countryCode);

private:
    std::string getSubjectDn(X509* cert);
    std::string getIssuerDn(X509* cert);
    bool isSelfSigned(X509* cert);
};

} // namespace services
