/**
 * @file ICertificateValidationPort.hpp
 * @brief Port interface for certificate validation operations
 */

#pragma once

#include "certificatevalidation/domain/model/Certificate.hpp"
#include "certificatevalidation/domain/model/CertificateRevocationList.hpp"
#include "certificatevalidation/domain/model/ValidationError.hpp"
#include <vector>
#include <optional>

namespace certificatevalidation::domain::port {

using namespace certificatevalidation::domain::model;

/**
 * @brief Port interface for certificate validation operations
 *
 * Defines the contract for X.509 certificate validation.
 * Implementations (adapters) should use cryptographic libraries
 * like OpenSSL to perform actual validation.
 */
class ICertificateValidationPort {
public:
    virtual ~ICertificateValidationPort() = default;

    /**
     * @brief Validate certificate signature
     * @param certificate Certificate to validate
     * @param issuerCertificate Issuer certificate (null for self-signed)
     * @return true if signature is valid
     */
    virtual bool validateSignature(
        const Certificate& certificate,
        const std::optional<Certificate>& issuerCertificate
    ) = 0;

    /**
     * @brief Validate certificate validity period
     * @param certificate Certificate to validate
     * @return true if certificate is currently valid (notBefore <= now <= notAfter)
     */
    virtual bool validateValidity(const Certificate& certificate) = 0;

    /**
     * @brief Validate Basic Constraints extension
     * @param certificate Certificate to validate
     * @return true if Basic Constraints match certificate type
     */
    virtual bool validateBasicConstraints(const Certificate& certificate) = 0;

    /**
     * @brief Validate Key Usage extension
     * @param certificate Certificate to validate
     * @return true if Key Usage matches certificate type requirements
     */
    virtual bool validateKeyUsage(const Certificate& certificate) = 0;

    /**
     * @brief Check certificate revocation status
     * @param certificate Certificate to check
     * @return true if certificate is NOT revoked (valid)
     */
    virtual bool checkRevocation(const Certificate& certificate) = 0;

    /**
     * @brief Check if certificate is revoked using specific CRL
     * @param certificate Certificate to check
     * @param crl CRL to check against
     * @return true if certificate is revoked
     */
    virtual bool isRevoked(
        const Certificate& certificate,
        const CertificateRevocationList& crl
    ) = 0;

    /**
     * @brief Build trust chain from certificate to trust anchor
     * @param certificate End entity certificate
     * @param trustAnchor Trust anchor (null for auto-discovery)
     * @param maxDepth Maximum chain depth
     * @return Chain of certificates from end entity to trust anchor
     */
    virtual std::vector<Certificate> buildTrustChain(
        const Certificate& certificate,
        const std::optional<Certificate>& trustAnchor,
        int maxDepth = 5
    ) = 0;

    /**
     * @brief Perform full certificate validation
     * @param certificate Certificate to validate
     * @param trustAnchor Trust anchor (null for auto-discovery)
     * @param checkRevocation Whether to check revocation status
     * @return List of validation errors (empty if valid)
     */
    virtual std::vector<ValidationError> performFullValidation(
        const Certificate& certificate,
        const std::optional<Certificate>& trustAnchor,
        bool checkRevocation = true
    ) = 0;

    /**
     * @brief Validate trust chain (DSC -> CSCA)
     * @param dsc Document Signer Certificate
     * @param csca Country Signing CA
     * @throws std::runtime_error if trust chain validation fails
     */
    virtual void validateTrustChain(
        const Certificate& dsc,
        const Certificate& csca
    ) = 0;
};

} // namespace certificatevalidation::domain::port
