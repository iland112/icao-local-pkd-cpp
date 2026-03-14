#pragma once

/**
 * @file cvc_signature.h
 * @brief CVC certificate signature verification using OpenSSL EVP API
 *
 * Verifies CVC signatures for both RSA and ECDSA algorithms.
 * The signature covers the Certificate Body (tag 0x7F4E content).
 *
 * Reference: BSI TR-03110 Part 3, Section D
 */

#include "icao/cvc/cvc_certificate.h"

#include <cstdint>
#include <string>
#include <vector>

namespace icao::cvc {

/**
 * @brief Signature verification result
 */
struct SignatureVerifyResult {
    bool valid = false;
    std::string message;
};

/**
 * @brief CVC signature verifier
 */
class CvcSignatureVerifier {
public:
    /**
     * @brief Verify a CVC certificate's signature using the issuer's public key
     *
     * For CVCA (self-signed): verifies using the certificate's own public key
     * For DV/IS: verifies using the issuer (CAR-referenced) certificate's public key
     *
     * @param cert Certificate to verify (bodyRaw = signed data, signature = signature bytes)
     * @param issuerKey Issuer's public key (from CVCA or DV certificate)
     * @return Verification result
     */
    static SignatureVerifyResult verify(const CvcCertificate& cert,
                                        const CvcPublicKey& issuerKey);

    /**
     * @brief Verify a self-signed CVCA certificate
     * @param cert CVCA certificate (uses its own public key)
     * @return Verification result
     */
    static SignatureVerifyResult verifySelfSigned(const CvcCertificate& cert);

private:
    /**
     * @brief Verify RSA signature (PKCS#1 v1.5 or PSS)
     */
    static SignatureVerifyResult verifyRsa(const std::vector<uint8_t>& data,
                                           const std::vector<uint8_t>& signature,
                                           const CvcPublicKey& key);

    /**
     * @brief Verify ECDSA signature
     */
    static SignatureVerifyResult verifyEcdsa(const std::vector<uint8_t>& data,
                                             const std::vector<uint8_t>& signature,
                                             const CvcPublicKey& key);

    /**
     * @brief Get the OpenSSL digest for a given algorithm OID
     * @return EVP_MD pointer, or nullptr if unsupported
     */
    static const void* getDigestForOid(const std::string& oid);
};

} // namespace icao::cvc
