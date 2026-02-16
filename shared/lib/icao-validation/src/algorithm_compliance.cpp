/**
 * @file algorithm_compliance.cpp
 * @brief ICAO algorithm compliance check implementation
 *
 * Consolidated from: services/pa-service/src/services/certificate_validation_service.cpp:530-595
 */

#include "icao/validation/algorithm_compliance.h"
#include <string>
#include <openssl/evp.h>
#include <openssl/objects.h>

namespace icao::validation {

AlgorithmComplianceResult validateAlgorithmCompliance(X509* cert) {
    AlgorithmComplianceResult result;

    if (!cert) {
        result.compliant = false;
        result.warning = "Certificate is null";
        return result;
    }

    // Extract signature algorithm NID
    int sigNid = X509_get_signature_nid(cert);
    result.algorithm = OBJ_nid2sn(sigNid);

    // ICAO Doc 9303 Part 12 Appendix A — Approved algorithms
    switch (sigNid) {
        // Approved: SHA-256 family
        case NID_sha256WithRSAEncryption:
        case NID_ecdsa_with_SHA256:
        // Approved: SHA-384 family
        case NID_sha384WithRSAEncryption:
        case NID_ecdsa_with_SHA384:
        // Approved: SHA-512 family
        case NID_sha512WithRSAEncryption:
        case NID_ecdsa_with_SHA512:
            result.compliant = true;
            break;

        // Deprecated: SHA-1 family (ICAO NTWG recommended phasing out)
        case NID_sha1WithRSAEncryption:
        case NID_ecdsa_with_SHA1:
            result.compliant = true;
            result.warning = "SHA-1 algorithm is deprecated per ICAO NTWG recommendations";
            break;

        // RSA-PSS variants
        case NID_rsassaPss:
            result.compliant = true;
            break;

        default:
            result.compliant = false;
            result.warning = "Unknown or non-ICAO-approved signature algorithm: " + result.algorithm;
            break;
    }

    // Check RSA key size (ICAO requires minimum 2048 bits)
    EVP_PKEY* pkey = X509_get_pubkey(cert);
    if (pkey) {
        int keyType = EVP_PKEY_base_id(pkey);
        result.keyBits = EVP_PKEY_bits(pkey);

        if (keyType == EVP_PKEY_RSA && result.keyBits < 2048) {
            result.warning = "RSA key size " + std::to_string(result.keyBits) +
                             " bits is below ICAO minimum of 2048 bits";
        }

        EVP_PKEY_free(pkey);
    }

    return result;
}

} // namespace icao::validation
