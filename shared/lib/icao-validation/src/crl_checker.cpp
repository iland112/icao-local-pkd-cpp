/**
 * @file crl_checker.cpp
 * @brief CRL revocation checker implementation
 *
 * Consolidated from:
 *   - services/pa-service/src/services/certificate_validation_service.cpp:360-438 (checkCrlStatus)
 *   - services/pkd-management/src/services/validation_service.cpp:738-843 (checkCrlRevocation)
 */

#include "icao/validation/crl_checker.h"
#include "icao/validation/cert_ops.h"

#include <ctime>
#include <stdexcept>
#include <openssl/x509v3.h>

namespace icao::validation {

CrlChecker::CrlChecker(ICrlProvider* crlProvider)
    : crlProvider_(crlProvider)
{
    if (!crlProvider_) {
        throw std::invalid_argument("CrlChecker: crlProvider cannot be nullptr");
    }
}

CrlCheckResult CrlChecker::check(X509* cert, const std::string& countryCode) {
    CrlCheckResult result;

    if (!cert) {
        result.status = CrlCheckStatus::NOT_CHECKED;
        result.message = "Certificate is null";
        return result;
    }

    if (countryCode.empty()) {
        result.status = CrlCheckStatus::NOT_CHECKED;
        result.message = "Country code is empty";
        return result;
    }

    // Step 1: Fetch CRL for the country
    X509_CRL* crl = crlProvider_->findCrlByCountry(countryCode);
    if (!crl) {
        result.status = CrlCheckStatus::CRL_UNAVAILABLE;
        result.message = "No CRL found for country " + countryCode;
        return result;
    }

    // Step 2: Extract CRL dates
    result.thisUpdate = asn1TimeToIso8601(X509_CRL_get0_lastUpdate(crl));
    result.nextUpdate = asn1TimeToIso8601(X509_CRL_get0_nextUpdate(crl));

    // Step 3: Check CRL expiration
    const ASN1_TIME* nextUpdate = X509_CRL_get0_nextUpdate(crl);
    if (nextUpdate) {
        time_t now = time(nullptr);
        if (X509_cmp_time(nextUpdate, &now) < 0) {
            X509_CRL_free(crl);
            result.status = CrlCheckStatus::CRL_EXPIRED;
            result.message = "CRL expired for country " + countryCode;
            return result;
        }
    }

    // Step 4: Check certificate serial number against CRL
    ASN1_INTEGER* certSerial = X509_get_serialNumber(cert);
    if (!certSerial) {
        X509_CRL_free(crl);
        result.status = CrlCheckStatus::VALID;
        result.message = "Could not extract certificate serial number";
        return result;
    }

    X509_REVOKED* revokedEntry = nullptr;
    int ret = X509_CRL_get0_by_serial(crl, &revokedEntry, certSerial);

    if (ret == 1 && revokedEntry) {
        result.status = CrlCheckStatus::REVOKED;
        result.message = "Certificate is revoked (country: " + countryCode + ")";

        // Step 5: Extract revocation reason (RFC 5280 Section 5.3.1)
        int reasonIdx = X509_REVOKED_get_ext_by_NID(revokedEntry, NID_crl_reason, -1);
        if (reasonIdx >= 0) {
            X509_EXTENSION* ext = X509_REVOKED_get_ext(revokedEntry, reasonIdx);
            if (ext) {
                ASN1_ENUMERATED* reasonEnum = static_cast<ASN1_ENUMERATED*>(
                    X509V3_EXT_d2i(ext));
                if (reasonEnum) {
                    long reasonCode = ASN1_ENUMERATED_get(reasonEnum);
                    switch (reasonCode) {
                        case 0:  result.revocationReason = "unspecified"; break;
                        case 1:  result.revocationReason = "keyCompromise"; break;
                        case 2:  result.revocationReason = "cACompromise"; break;
                        case 3:  result.revocationReason = "affiliationChanged"; break;
                        case 4:  result.revocationReason = "superseded"; break;
                        case 5:  result.revocationReason = "cessationOfOperation"; break;
                        case 6:  result.revocationReason = "certificateHold"; break;
                        case 8:  result.revocationReason = "removeFromCRL"; break;
                        case 9:  result.revocationReason = "privilegeWithdrawn"; break;
                        case 10: result.revocationReason = "aACompromise"; break;
                        default: result.revocationReason = "unknown(" + std::to_string(reasonCode) + ")"; break;
                    }
                    ASN1_ENUMERATED_free(reasonEnum);
                }
            }
        }
    } else {
        result.status = CrlCheckStatus::VALID;
        result.message = "Certificate not revoked (country: " + countryCode + ")";
    }

    X509_CRL_free(crl);
    return result;
}

} // namespace icao::validation
