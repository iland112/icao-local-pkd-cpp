/**
 * @file algorithm_compliance.cpp
 * @brief ICAO algorithm compliance check implementation
 *
 * Consolidated from: services/pa-service/src/services/certificate_validation_service.cpp:530-595
 */

#include "icao/validation/algorithm_compliance.h"
#include <string>
#include <openssl/evp.h>
#include <openssl/ec.h>
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
    // BSI TR-03110 — Additional algorithms supported for EAC/PACE
    switch (sigNid) {
        // Doc 9303 Part 12: SHA-256 family
        case NID_sha256WithRSAEncryption:
        case NID_ecdsa_with_SHA256:
        // Doc 9303 Part 12: SHA-384 family
        case NID_sha384WithRSAEncryption:
        case NID_ecdsa_with_SHA384:
        // Doc 9303 Part 12: SHA-512 family
        case NID_sha512WithRSAEncryption:
        case NID_ecdsa_with_SHA512:
            result.compliant = true;
            break;

        // SHA-224: supported via BSI TR-03110 but outside Doc 9303 Part 12 primary list
        case NID_sha224WithRSAEncryption:
        case NID_ecdsa_with_SHA224:
            result.compliant = true;
            result.warning = "SHA-224 supported via BSI TR-03110 but outside Doc 9303 Part 12 primary list";
            break;

        // SHA-1: deprecated per ICAO NTWG (still functional, phasing out)
        case NID_sha1WithRSAEncryption:
        case NID_ecdsa_with_SHA1:
            result.compliant = true;
            result.warning = "SHA-1 is deprecated per ICAO NTWG recommendation (migration to SHA-256+ advised)";
            break;

        // RSA-PSS variants
        case NID_rsassaPss:
            result.compliant = true;
            break;

        default:
            result.compliant = false;
            result.warning = "Signature algorithm not in Doc 9303 Part 12 or BSI TR-03110: " + result.algorithm;
            break;
    }

    // Check key size and ECDSA curve
    EVP_PKEY* pkey = X509_get_pubkey(cert);
    if (pkey) {
        int keyType = EVP_PKEY_base_id(pkey);
        result.keyBits = EVP_PKEY_bits(pkey);

        if (keyType == EVP_PKEY_RSA && result.keyBits < 2048) {
            result.warning = "RSA key size " + std::to_string(result.keyBits) +
                             " bits is below Doc 9303 minimum of 2048 bits";
        } else if (keyType == EVP_PKEY_EC) {
            // Check ECDSA curve: Doc 9303 Part 12 (NIST) + BSI TR-03110 (Brainpool)
            EC_KEY* ecKey = EVP_PKEY_get0_EC_KEY(pkey);
            if (ecKey) {
                int curveNid = EC_GROUP_get_curve_name(EC_KEY_get0_group(ecKey));
                bool doc9303Curve = (curveNid == NID_X9_62_prime256v1 ||  // P-256
                                     curveNid == NID_secp384r1 ||         // P-384
                                     curveNid == NID_secp521r1);          // P-521
                bool bsiCurve = (curveNid == NID_brainpoolP256r1 ||
                                 curveNid == NID_brainpoolP384r1 ||
                                 curveNid == NID_brainpoolP512r1);
                if (bsiCurve && result.warning.empty()) {
                    result.warning = "Brainpool curve supported via BSI TR-03110 but outside Doc 9303 Part 12 primary list";
                } else if (!doc9303Curve && !bsiCurve) {
                    result.warning = "ECDSA curve not in Doc 9303/BSI TR-03110: " + std::string(OBJ_nid2sn(curveNid));
                }
            }
        }

        EVP_PKEY_free(pkey);
    }

    return result;
}

} // namespace icao::validation
