/**
 * @file certificate_validation_service.h
 * @brief Service for certificate chain validation
 *
 * Delegates pure validation operations to icao::validation shared library.
 * Handles PA-specific orchestration: point-in-time validation, CRL messaging,
 * DSC conformance checking, domain model conversion.
 *
 * @author SmartCore Inc.
 * @date 2026-02-16
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <openssl/x509.h>
#include <icao/validation/types.h>
#include <icao/validation/crl_checker.h>
#include "../domain/models/certificate_chain_validation.h"
#include "../repositories/ldap_certificate_repository.h"
#include "../repositories/ldap_crl_repository.h"
#include "../adapters/ldap_crl_provider.h"

namespace services {

/**
 * @brief Certificate chain validation service (DSC to CSCA trust chain)
 *
 * Validates DSC certificates against CSCA certificates retrieved from LDAP,
 * performs CRL revocation checking, and builds trust chains per ICAO 9303.
 *
 * Pure validation operations (signature, extensions, algorithms) are delegated
 * to the icao::validation shared library.
 */
class CertificateValidationService {
private:
    repositories::LdapCertificateRepository* certRepo_;
    repositories::LdapCrlRepository* crlRepo_;

    // CRL checker (via library)
    std::unique_ptr<adapters::LdapCrlProvider> crlProvider_;
    std::unique_ptr<icao::validation::CrlChecker> crlChecker_;

public:
    /**
     * @brief Constructor with repository dependencies
     */
    CertificateValidationService(
        repositories::LdapCertificateRepository* certRepo,
        repositories::LdapCrlRepository* crlRepo
    );

    ~CertificateValidationService() = default;

    /**
     * @brief Validate DSC certificate chain against CSCA (ICAO 9303)
     */
    domain::models::CertificateChainValidation validateCertificateChain(
        X509* dscCert,
        const std::string& countryCode,
        const std::string& signingTime = ""
    );

    /**
     * @brief Build trust chain from DSC to root CSCA
     */
    std::vector<X509*> buildTrustChain(X509* dscCert, const std::string& countryCode);
};

} // namespace services
