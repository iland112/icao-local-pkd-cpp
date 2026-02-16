/**
 * @file extension_validator.cpp
 * @brief X.509 extension validation implementation
 *
 * Consolidated from: services/pa-service/src/services/certificate_validation_service.cpp:457-528
 */

#include "icao/validation/extension_validator.h"
#include <openssl/x509v3.h>
#include <openssl/objects.h>

namespace icao::validation {

ExtensionValidationResult validateExtensions(X509* cert, const std::string& role) {
    ExtensionValidationResult result;

    if (!cert) {
        result.valid = false;
        result.warnings.push_back("Certificate is null");
        return result;
    }

    // RFC 5280 Section 4.2: Check for unknown critical extensions
    int extCount = X509_get_ext_count(cert);
    for (int i = 0; i < extCount; i++) {
        X509_EXTENSION* ext = X509_get_ext(cert, i);
        if (!ext) continue;

        if (X509_EXTENSION_get_critical(ext)) {
            ASN1_OBJECT* obj = X509_EXTENSION_get_object(ext);
            int nid = OBJ_obj2nid(obj);

            // Known critical extensions per ICAO 9303 Part 12 / RFC 5280
            if (nid == NID_basic_constraints ||
                nid == NID_key_usage ||
                nid == NID_certificate_policies ||
                nid == NID_subject_key_identifier ||
                nid == NID_authority_key_identifier ||
                nid == NID_name_constraints ||
                nid == NID_policy_constraints ||
                nid == NID_inhibit_any_policy ||
                nid == NID_subject_alt_name ||
                nid == NID_issuer_alt_name ||
                nid == NID_crl_distribution_points ||
                nid == NID_ext_key_usage) {
                continue;  // Known extension
            }

            char oidBuf[80];
            OBJ_obj2txt(oidBuf, sizeof(oidBuf), obj, 1);
            result.warnings.push_back("Unknown critical extension: " + std::string(oidBuf));
        }
    }

    // ICAO Doc 9303 Part 12 Section 4.6: Key Usage validation
    ASN1_BIT_STRING* usage = static_cast<ASN1_BIT_STRING*>(
        X509_get_ext_d2i(cert, NID_key_usage, nullptr, nullptr));
    if (usage) {
        if (role == "DSC") {
            // DSC must have digitalSignature (bit 0)
            if (!ASN1_BIT_STRING_get_bit(usage, 0)) {
                result.warnings.push_back("DSC missing required digitalSignature key usage");
            }
        } else if (role == "CSCA") {
            // CSCA must have keyCertSign (bit 5)
            if (!ASN1_BIT_STRING_get_bit(usage, 5)) {
                result.warnings.push_back("CSCA missing required keyCertSign key usage");
            }
            // CSCA should have cRLSign (bit 6) — recommended but not required
        }
        ASN1_BIT_STRING_free(usage);
    } else if (role == "DSC") {
        // DSC with no Key Usage extension is unusual but not prohibited
    }

    result.valid = result.warnings.empty();
    return result;
}

} // namespace icao::validation
