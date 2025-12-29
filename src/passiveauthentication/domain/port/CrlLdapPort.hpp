#pragma once

#include "passiveauthentication/domain/model/CrlCheckResult.hpp"
#include <string>
#include <optional>
#include <openssl/x509.h>

namespace pa::domain::port {

/**
 * Port interface for CRL (Certificate Revocation List) operations.
 *
 * Provides CRL retrieval and revocation checking.
 */
class CrlLdapPort {
public:
    virtual ~CrlLdapPort() = default;

    /**
     * Get CRL for a CSCA.
     *
     * @param cscaSubjectDn CSCA subject DN
     * @param countryCode ISO 3166-1 alpha-2 country code
     * @return X509_CRL* if found (caller takes ownership), nullptr otherwise
     */
    virtual X509_CRL* getCrl(
        const std::string& cscaSubjectDn,
        const std::string& countryCode
    ) = 0;

    /**
     * Check if a certificate is revoked.
     *
     * @param cert Certificate to check
     * @param crl CRL to check against
     * @param cscaCert CSCA certificate for CRL signature verification
     * @return CrlCheckResult with verification outcome
     */
    virtual model::CrlCheckResult checkRevocation(
        X509* cert,
        X509_CRL* crl,
        X509* cscaCert
    ) = 0;

    /**
     * Verify CRL signature using CSCA public key.
     *
     * @param crl CRL to verify
     * @param cscaCert CSCA certificate
     * @return true if signature is valid
     */
    virtual bool verifyCrlSignature(
        X509_CRL* crl,
        X509* cscaCert
    ) = 0;
};

} // namespace pa::domain::port
