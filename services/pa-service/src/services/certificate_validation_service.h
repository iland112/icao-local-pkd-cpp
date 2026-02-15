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

/**
 * @brief Certificate chain validation service (DSC to CSCA trust chain)
 *
 * Validates DSC certificates against CSCA certificates retrieved from LDAP,
 * performs CRL revocation checking, and builds trust chains per ICAO 9303.
 */
class CertificateValidationService {
private:
    repositories::LdapCertificateRepository* certRepo_;
    repositories::LdapCrlRepository* crlRepo_;

public:
    /**
     * @brief Constructor with repository dependencies
     * @param certRepo LDAP certificate repository for CSCA/DSC lookup
     * @param crlRepo LDAP CRL repository for revocation checking
     * @throws std::invalid_argument if any dependency is nullptr
     */
    CertificateValidationService(
        repositories::LdapCertificateRepository* certRepo,
        repositories::LdapCrlRepository* crlRepo
    );

    /** @brief Destructor */
    ~CertificateValidationService() = default;

    /**
     * @brief Validate DSC certificate chain against CSCA (ICAO 9303)
     * @param dscCert DSC X509 certificate extracted from SOD
     * @param countryCode ISO 3166-1 alpha-2 country code
     * @param signingTime Optional SOD signing time (ISO 8601) for point-in-time validation
     * @return CertificateChainValidation result with trust chain details
     */
    domain::models::CertificateChainValidation validateCertificateChain(
        X509* dscCert,
        const std::string& countryCode,
        const std::string& signingTime = ""
    );

    /**
     * @brief Verify certificate signature against issuer public key
     * @param cert Certificate to verify
     * @param issuerCert Issuer certificate containing the public key
     * @return true if signature is valid
     */
    bool verifyCertificateSignature(X509* cert, X509* issuerCert);

    /**
     * @brief Check if certificate has expired
     * @param cert X509 certificate to check
     * @return true if certificate notAfter is in the past
     */
    bool isCertificateExpired(X509* cert);

    /**
     * @brief Check CRL revocation status for a certificate
     * @param cert Certificate to check
     * @param countryCode Country code for CRL lookup
     * @param[out] crlThisUpdate CRL issued date (ISO 8601)
     * @param[out] crlNextUpdate CRL next update date (ISO 8601)
     * @return CrlStatus indicating revocation check result
     */
    domain::models::CrlStatus checkCrlStatus(X509* cert, const std::string& countryCode,
        std::string& crlThisUpdate, std::string& crlNextUpdate,
        std::string& revocationReason);

    /**
     * @brief Build trust chain from DSC to root CSCA
     * @param dscCert DSC certificate (start of chain)
     * @param countryCode Country code for CSCA lookup
     * @return Vector of X509* certificates in chain order (caller must free)
     */
    std::vector<X509*> buildTrustChain(X509* dscCert, const std::string& countryCode);

    /**
     * @brief Validate certificate extensions per RFC 5280 and ICAO 9303 Part 12
     *
     * Checks:
     * - No unknown critical extensions (RFC 5280 Section 4.2)
     * - DSC: must have digitalSignature key usage
     * - CSCA: must have keyCertSign and cRLSign key usage
     *
     * @param cert X509 certificate to validate
     * @param role Certificate role ("DSC" or "CSCA")
     * @return Warning message (empty if all checks pass)
     */
    std::string validateExtensions(X509* cert, const std::string& role);

    /**
     * @brief ICAO 9303 algorithm compliance check result
     */
    struct AlgorithmComplianceResult {
        bool compliant = true;
        std::string algorithm;
        std::string warning;  // Non-empty if deprecated algorithm
    };

    /**
     * @brief Validate DSC signature algorithm against ICAO 9303 requirements
     * @param cert X509 certificate to check
     * @return AlgorithmComplianceResult with compliance details
     */
    AlgorithmComplianceResult validateAlgorithmCompliance(X509* cert);

private:
    std::string getSubjectDn(X509* cert);
    std::string getIssuerDn(X509* cert);
    bool isSelfSigned(X509* cert);
};

} // namespace services
