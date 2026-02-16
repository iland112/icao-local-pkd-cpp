/**
 * @file certificate_validation_service.cpp
 * @brief Implementation of CertificateValidationService
 *
 * Delegates pure validation to icao::validation library:
 *   - cert_ops: verifyCertificateSignature, isSelfSigned, getSubjectDn, etc.
 *   - extension_validator: validateExtensions
 *   - algorithm_compliance: validateAlgorithmCompliance
 *   - crl_checker: CRL revocation check
 *
 * PA-specific logic retained:
 *   - Point-in-time validation (signingTime)
 *   - DSC conformance check (nc-data)
 *   - CRL status messaging (ICAO Doc 9303 descriptions)
 *   - Domain model conversion (CertificateChainValidation)
 */

#include "certificate_validation_service.h"
#include <icao/validation/cert_ops.h>
#include <icao/validation/extension_validator.h>
#include <icao/validation/algorithm_compliance.h>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cstring>
#include <openssl/bn.h>

namespace services {

namespace {
std::string serialNumberToString(ASN1_INTEGER* serial) {
    if (!serial) return "";

    BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
    if (!bn) return "";

    char* hex = BN_bn2hex(bn);
    std::string result;
    if (hex) {
        std::string hexStr(hex);
        for (size_t i = 0; i < hexStr.length(); i += 2) {
            if (i > 0) result += ":";
            result += hexStr.substr(i, 2);
        }
        OPENSSL_free(hex);
    }
    BN_free(bn);

    return result;
}
} // anonymous namespace

CertificateValidationService::CertificateValidationService(
    repositories::LdapCertificateRepository* certRepo,
    repositories::LdapCrlRepository* crlRepo)
    : certRepo_(certRepo), crlRepo_(crlRepo)
{
    if (!certRepo_ || !crlRepo_) {
        throw std::invalid_argument("Repository dependencies cannot be null");
    }

    // Initialize CRL checker via library
    crlProvider_ = std::make_unique<adapters::LdapCrlProvider>(crlRepo_);
    crlChecker_ = std::make_unique<icao::validation::CrlChecker>(crlProvider_.get());

    spdlog::debug("CertificateValidationService initialized with icao::validation library");
}

domain::models::CertificateChainValidation CertificateValidationService::validateCertificateChain(
    X509* dscCert,
    const std::string& countryCode,
    const std::string& signingTime)
{
    domain::models::CertificateChainValidation result;

    if (!dscCert) {
        return domain::models::CertificateChainValidation::createInvalid("DSC certificate is null");
    }

    spdlog::info("Validating certificate chain for country: {}", countryCode);

    try {
        // Extract DSC information (using library)
        result.dscSubject = icao::validation::getSubjectDn(dscCert);
        result.dscIssuer = icao::validation::getIssuerDn(dscCert);

        ASN1_INTEGER* serial = X509_get_serialNumber(dscCert);
        if (serial) {
            result.dscSerialNumber = serialNumberToString(serial);
        }

        // Check DSC expiration (using library)
        result.dscExpired = icao::validation::isCertificateExpired(dscCert);

        // Point-in-time validation (ICAO Doc 9303 Part 12 Section 5.4)
        if (!signingTime.empty()) {
            result.signingTime = signingTime;

            struct tm tmSigning = {};
            if (sscanf(signingTime.c_str(), "%d-%d-%dT%d:%d:%d",
                       &tmSigning.tm_year, &tmSigning.tm_mon, &tmSigning.tm_mday,
                       &tmSigning.tm_hour, &tmSigning.tm_min, &tmSigning.tm_sec) >= 3) {
                tmSigning.tm_year -= 1900;
                tmSigning.tm_mon -= 1;
                time_t sigTime = timegm(&tmSigning);

                bool dscValidAtSigning = true;
                if (X509_cmp_time(X509_get0_notBefore(dscCert), &sigTime) > 0) {
                    dscValidAtSigning = false;
                    spdlog::warn("Point-in-time: DSC was NOT YET VALID at signing time {}", signingTime);
                }
                if (X509_cmp_time(X509_get0_notAfter(dscCert), &sigTime) < 0) {
                    dscValidAtSigning = false;
                    spdlog::warn("Point-in-time: DSC was EXPIRED at signing time {}", signingTime);
                }

                result.validAtSigningTime = dscValidAtSigning;
                if (!dscValidAtSigning) {
                    result.expirationMessage = "DSC certificate was not valid at document signing time (" + signingTime + ")";
                }
            }
        }

        // Extract country code from DSC issuer DN if not provided
        std::string effectiveCountry = countryCode;
        if (effectiveCountry.empty()) {
            effectiveCountry = icao::validation::extractDnAttribute(result.dscIssuer, "C");
            // Convert to uppercase for LDAP
            for (char& c : effectiveCountry) {
                c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            }
            if (!effectiveCountry.empty()) {
                spdlog::info("Extracted country code from DSC issuer: {}", effectiveCountry);
            }
        }

        result.countryCode = effectiveCountry;

        // Find CSCA certificate (multi-CSCA key rollover support)
        std::vector<X509*> allCscas = certRepo_->findAllCscasByCountry(effectiveCountry);
        if (allCscas.empty()) {
            result.valid = false;
            result.validationErrors = "CSCA not found for issuer: " + result.dscIssuer;
            result.expirationStatus = "INVALID";
            return result;
        }

        std::string dscIssuerDn = result.dscIssuer;

        // Try each CSCA: match by DN, then verify signature (using library)
        X509* cscaCert = nullptr;
        for (X509* candidate : allCscas) {
            std::string candidateSubject = icao::validation::getSubjectDn(candidate);
            if (strcasecmp(dscIssuerDn.c_str(), candidateSubject.c_str()) == 0) {
                if (icao::validation::verifyCertificateSignature(dscCert, candidate)) {
                    cscaCert = candidate;
                    spdlog::debug("PA chain validation: Found signature-verified CSCA: {}",
                                  candidateSubject.substr(0, 50));
                    break;
                } else {
                    spdlog::debug("PA chain validation: DN match but signature failed: {}",
                                  candidateSubject.substr(0, 50));
                }
            }
        }

        // Fallback: DN-only match
        if (!cscaCert) {
            for (X509* candidate : allCscas) {
                std::string candidateSubject = icao::validation::getSubjectDn(candidate);
                if (strcasecmp(dscIssuerDn.c_str(), candidateSubject.c_str()) == 0) {
                    cscaCert = candidate;
                    spdlog::warn("PA chain validation: Using DN-only match (no signature verified): {}",
                                 candidateSubject.substr(0, 50));
                    break;
                }
            }
        }

        if (!cscaCert) {
            for (X509* c : allCscas) X509_free(c);
            result.valid = false;
            result.validationErrors = "CSCA not found for issuer: " + result.dscIssuer;
            result.expirationStatus = "INVALID";
            return result;
        }

        // Extract CSCA information (using library)
        result.cscaSubject = icao::validation::getSubjectDn(cscaCert);
        serial = X509_get_serialNumber(cscaCert);
        if (serial) {
            result.cscaSerialNumber = serialNumberToString(serial);
        }

        result.cscaExpired = icao::validation::isCertificateExpired(cscaCert);

        // Verify DSC → CSCA signature (using library)
        result.signatureVerified = icao::validation::verifyCertificateSignature(dscCert, cscaCert);

        // Verify self-signed CSCA self-signature (RFC 5280 Section 6.1)
        if (result.signatureVerified && icao::validation::isSelfSigned(cscaCert)) {
            if (!icao::validation::verifyCertificateSignature(cscaCert, cscaCert)) {
                spdlog::error("CSCA self-signature verification FAILED - root CSCA may be tampered");
                result.signatureVerified = false;
                result.valid = false;
                result.validationErrors = "CSCA self-signature verification failed";
                result.expirationStatus = "INVALID";
                for (X509* c : allCscas) X509_free(c);
                return result;
            }
            spdlog::debug("CSCA self-signature verified (root certificate integrity confirmed)");
        }

        // Validate DSC extensions (using library)
        auto dscExtResult = icao::validation::validateExtensions(dscCert, "DSC");
        auto cscaExtResult = icao::validation::validateExtensions(cscaCert, "CSCA");
        std::string dscExtWarnings = dscExtResult.warningsAsString();
        std::string cscaExtWarnings = cscaExtResult.warningsAsString();
        if (!dscExtWarnings.empty() || !cscaExtWarnings.empty()) {
            std::string combined;
            if (!dscExtWarnings.empty()) combined += "DSC: " + dscExtWarnings;
            if (!cscaExtWarnings.empty()) {
                if (!combined.empty()) combined += "; ";
                combined += "CSCA: " + cscaExtWarnings;
            }
            if (result.expirationMessage) {
                result.expirationMessage = *result.expirationMessage + "; " + combined;
            } else {
                result.expirationMessage = combined;
            }
        }

        // Validate DSC signature algorithm (using library)
        auto algResult = icao::validation::validateAlgorithmCompliance(dscCert);
        result.signatureAlgorithm = algResult.algorithm;
        if (!algResult.warning.empty()) {
            if (result.expirationMessage) {
                result.expirationMessage = *result.expirationMessage + "; " + algResult.warning;
            } else {
                result.expirationMessage = algResult.warning;
            }
        }

        // Check CRL (using library CrlChecker)
        icao::validation::CrlCheckResult crlResult = crlChecker_->check(dscCert, effectiveCountry);
        if (!crlResult.thisUpdate.empty()) result.crlThisUpdate = crlResult.thisUpdate;
        if (!crlResult.nextUpdate.empty()) result.crlNextUpdate = crlResult.nextUpdate;
        if (!crlResult.revocationReason.empty()) result.crlRevocationReason = crlResult.revocationReason;

        // Map library CrlCheckStatus to domain CrlStatus
        switch (crlResult.status) {
            case icao::validation::CrlCheckStatus::VALID:
                result.crlStatus = domain::models::CrlStatus::VALID;
                break;
            case icao::validation::CrlCheckStatus::REVOKED:
                result.crlStatus = domain::models::CrlStatus::REVOKED;
                break;
            case icao::validation::CrlCheckStatus::CRL_UNAVAILABLE:
                result.crlStatus = domain::models::CrlStatus::CRL_UNAVAILABLE;
                break;
            case icao::validation::CrlCheckStatus::CRL_EXPIRED:
                result.crlStatus = domain::models::CrlStatus::CRL_EXPIRED;
                break;
            case icao::validation::CrlCheckStatus::CRL_INVALID:
                result.crlStatus = domain::models::CrlStatus::CRL_INVALID;
                break;
            case icao::validation::CrlCheckStatus::NOT_CHECKED:
                result.crlStatus = domain::models::CrlStatus::NOT_CHECKED;
                break;
        }

        result.crlChecked = (result.crlStatus != domain::models::CrlStatus::NOT_CHECKED);
        result.revoked = (result.crlStatus == domain::models::CrlStatus::REVOKED);

        // Set CRL status messages (ICAO Doc 9303 compliant)
        switch (result.crlStatus) {
            case domain::models::CrlStatus::VALID:
                result.crlStatusDescription = "Certificate Revocation List (CRL) check passed";
                result.crlStatusDetailedDescription = "The Document Signer Certificate (DSC) was verified against the Certificate Revocation List (CRL) as specified in ICAO Doc 9303 Part 11. The certificate is not revoked and remains valid for Passive Authentication.";
                result.crlStatusSeverity = "INFO";
                result.crlMessage = "DSC verified - not revoked";
                break;
            case domain::models::CrlStatus::REVOKED:
                result.crlStatusDescription = "Certificate has been revoked by issuing authority";
                result.crlStatusDetailedDescription = "The Document Signer Certificate (DSC) appears on the Certificate Revocation List (CRL) published by the issuing Country Signing CA (CSCA). According to RFC 5280 and ICAO Doc 9303 Part 11, this certificate must not be used for Passive Authentication verification.";
                result.crlStatusSeverity = "CRITICAL";
                result.crlMessage = "DSC is revoked - PA verification FAILED";
                break;
            case domain::models::CrlStatus::CRL_UNAVAILABLE:
                result.crlStatusDescription = "Certificate Revocation List (CRL) not available";
                result.crlStatusDetailedDescription = "No CRL was found in the LDAP PKD for this issuing country. ICAO Doc 9303 Part 11 specifies CRL checking as RECOMMENDED but not mandatory. According to the principle of fail-open for unavailable infrastructure, this verification continues with a warning.";
                result.crlStatusSeverity = "WARNING";
                result.crlMessage = "CRL not found - proceeding with caution";
                break;
            case domain::models::CrlStatus::CRL_EXPIRED:
                result.crlStatusDescription = "Certificate Revocation List (CRL) has expired";
                result.crlStatusDetailedDescription = "The CRL retrieved from the PKD has passed its nextUpdate time as defined in RFC 5280. An expired CRL cannot be relied upon for revocation status. ICAO Doc 9303 Part 11 recommends treating expired CRLs with caution, as they may not reflect recent revocations.";
                result.crlStatusSeverity = "WARNING";
                result.crlMessage = "CRL expired - revocation status uncertain";
                break;
            case domain::models::CrlStatus::CRL_INVALID:
                result.crlStatusDescription = "Certificate Revocation List (CRL) signature verification failed";
                result.crlStatusDetailedDescription = "The digital signature on the CRL could not be verified against the issuing CSCA's public key. This indicates either CRL corruption or a security compromise. Per RFC 5280 Section 6.3, an invalid CRL must not be used for certificate validation.";
                result.crlStatusSeverity = "CRITICAL";
                result.crlMessage = "CRL signature invalid - cannot verify revocation";
                break;
            case domain::models::CrlStatus::NOT_CHECKED:
                result.crlStatusDescription = "Certificate revocation check was not performed";
                result.crlStatusDetailedDescription = "CRL checking was skipped or could not be completed. ICAO Doc 9303 Part 11 considers CRL verification as a SHOULD requirement rather than MUST. This is acceptable in environments where CRL infrastructure is not fully deployed.";
                result.crlStatusSeverity = "INFO";
                result.crlMessage = "CRL check skipped";
                break;
        }

        // Overall validation
        result.valid = result.signatureVerified && !result.revoked && result.validAtSigningTime;

        // Expiration status
        if (!result.validAtSigningTime) {
            result.expirationStatus = "INVALID";
        } else if (result.dscExpired || result.cscaExpired) {
            result.expirationStatus = "EXPIRED";
        } else {
            result.expirationStatus = "VALID";
        }

        result.trustChainPath = "DSC -> " + result.cscaSubject.substr(0, 50);
        result.trustChainDepth = 2;

        // Check DSC conformance status (nc-data LDAP lookup) — PA-specific
        auto conformanceInfo = certRepo_->checkDscConformance(dscCert, effectiveCountry);
        result.dscNonConformant = conformanceInfo.isNonConformant;
        result.pkdConformanceCode = conformanceInfo.conformanceCode;
        result.pkdConformanceText = conformanceInfo.conformanceText;

        // Free all CSCA candidates
        for (X509* c : allCscas) X509_free(c);

    } catch (const std::exception& e) {
        spdlog::error("Certificate chain validation failed: {}", e.what());
        result.valid = false;
        result.validationErrors = e.what();
    }

    return result;
}

std::vector<X509*> CertificateValidationService::buildTrustChain(
    X509* dscCert,
    const std::string& countryCode)
{
    std::vector<X509*> chain;
    chain.push_back(dscCert);

    std::string issuerDn = icao::validation::getIssuerDn(dscCert);
    X509* cscaCert = certRepo_->findCscaByIssuerDn(issuerDn, countryCode);

    if (cscaCert) {
        chain.push_back(cscaCert);
    }

    return chain;
}

} // namespace services
