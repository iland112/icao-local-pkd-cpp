/**
 * @file certificate_validation_service.cpp
 * @brief Implementation of CertificateValidationService
 */

#include "certificate_validation_service.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <cstring>
#include <openssl/err.h>
#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509v3.h>

namespace services {

namespace {
/**
 * @brief Convert ASN1_INTEGER to hex string
 * @param serial ASN1_INTEGER pointer
 * @return Hex string representation (e.g., "01:23:45:67")
 */
std::string serialNumberToString(ASN1_INTEGER* serial) {
    if (!serial) return "";

    BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
    if (!bn) return "";

    char* hex = BN_bn2hex(bn);
    std::string result;
    if (hex) {
        // Convert to uppercase and add colons between bytes
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
    spdlog::debug("CertificateValidationService initialized");
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
        // Extract DSC information
        result.dscSubject = getSubjectDn(dscCert);
        result.dscIssuer = getIssuerDn(dscCert);

        ASN1_INTEGER* serial = X509_get_serialNumber(dscCert);
        if (serial) {
            result.dscSerialNumber = serialNumberToString(serial);
        }

        // Check DSC expiration (against current time)
        result.dscExpired = isCertificateExpired(dscCert);

        // Point-in-time validation (ICAO Doc 9303 Part 12 Section 5.4)
        // If signing time is available, check if DSC was valid AT THE TIME OF SIGNING
        if (!signingTime.empty()) {
            result.signingTime = signingTime;

            // Parse signing time to time_t
            struct tm tmSigning = {};
            if (sscanf(signingTime.c_str(), "%d-%d-%dT%d:%d:%d",
                       &tmSigning.tm_year, &tmSigning.tm_mon, &tmSigning.tm_mday,
                       &tmSigning.tm_hour, &tmSigning.tm_min, &tmSigning.tm_sec) >= 3) {
                tmSigning.tm_year -= 1900;
                tmSigning.tm_mon -= 1;
                time_t sigTime = timegm(&tmSigning);

                // Check DSC validity at signing time
                bool dscValidAtSigning = true;
                if (X509_cmp_time(X509_get0_notBefore(dscCert), &sigTime) > 0) {
                    dscValidAtSigning = false;  // Not-yet-valid at signing time
                    spdlog::warn("Point-in-time: DSC was NOT YET VALID at signing time {}", signingTime);
                }
                if (X509_cmp_time(X509_get0_notAfter(dscCert), &sigTime) < 0) {
                    dscValidAtSigning = false;  // Already expired at signing time
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
            // Try to extract country from issuer DN (e.g., /C=KR/O=Gov/CN=CSCA)
            size_t cPos = result.dscIssuer.find("/C=");
            if (cPos != std::string::npos) {
                cPos += 3; // Skip "/C="
                size_t endPos = result.dscIssuer.find('/', cPos);
                if (endPos != std::string::npos) {
                    effectiveCountry = result.dscIssuer.substr(cPos, endPos - cPos);
                } else {
                    effectiveCountry = result.dscIssuer.substr(cPos);
                }
                spdlog::info("Extracted country code from DSC issuer: {}", effectiveCountry);
            }
        }

        // Store extracted country code in result for caller use
        result.countryCode = effectiveCountry;

        // Find CSCA certificate
        // ICAO 9303 Part 12: When multiple CSCAs share the same DN (key rollover),
        // try all candidates and select the one whose signature verifies.
        std::vector<X509*> allCscas = certRepo_->findAllCscasByCountry(effectiveCountry);
        if (allCscas.empty()) {
            result.valid = false;
            result.validationErrors = "CSCA not found for issuer: " + result.dscIssuer;
            result.expirationStatus = "INVALID";
            return result;
        }

        std::string dscIssuerDn = result.dscIssuer;

        // Try each CSCA: match by DN, then verify signature
        X509* cscaCert = nullptr;
        for (X509* candidate : allCscas) {
            std::string candidateSubject = getSubjectDn(candidate);
            // Case-insensitive DN comparison
            if (strcasecmp(dscIssuerDn.c_str(), candidateSubject.c_str()) == 0) {
                // DN matches - verify signature to confirm correct key pair
                if (verifyCertificateSignature(dscCert, candidate)) {
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

        // Fallback: if no signature-verified match, try DN-only match
        if (!cscaCert) {
            for (X509* candidate : allCscas) {
                std::string candidateSubject = getSubjectDn(candidate);
                if (strcasecmp(dscIssuerDn.c_str(), candidateSubject.c_str()) == 0) {
                    cscaCert = candidate;
                    spdlog::warn("PA chain validation: Using DN-only match (no signature verified): {}",
                                 candidateSubject.substr(0, 50));
                    break;
                }
            }
        }

        if (!cscaCert) {
            // Free all candidates
            for (X509* c : allCscas) X509_free(c);
            result.valid = false;
            result.validationErrors = "CSCA not found for issuer: " + result.dscIssuer;
            result.expirationStatus = "INVALID";
            return result;
        }

        // Extract CSCA information
        result.cscaSubject = getSubjectDn(cscaCert);
        serial = X509_get_serialNumber(cscaCert);
        if (serial) {
            result.cscaSerialNumber = serialNumberToString(serial);
        }

        result.cscaExpired = isCertificateExpired(cscaCert);

        // Verify DSC → CSCA signature (may already be verified above, but re-check for consistency)
        result.signatureVerified = verifyCertificateSignature(dscCert, cscaCert);

        // Verify self-signed CSCA self-signature (RFC 5280 Section 6.1)
        // A tampered root CSCA with correct DN structure but invalid self-signature must be rejected
        if (result.signatureVerified && isSelfSigned(cscaCert)) {
            if (!verifyCertificateSignature(cscaCert, cscaCert)) {
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

        // Validate DSC extensions (RFC 5280 + ICAO 9303 Part 12 key usage)
        std::string dscExtWarnings = validateExtensions(dscCert, "DSC");
        std::string cscaExtWarnings = validateExtensions(cscaCert, "CSCA");
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

        // Validate DSC signature algorithm (ICAO Doc 9303 Part 12 Appendix A)
        auto algResult = validateAlgorithmCompliance(dscCert);
        result.signatureAlgorithm = algResult.algorithm;
        if (!algResult.warning.empty()) {
            if (result.expirationMessage) {
                result.expirationMessage = *result.expirationMessage + "; " + algResult.warning;
            } else {
                result.expirationMessage = algResult.warning;
            }
        }

        // Check CRL (ICAO Doc 9303 Part 11 - Certificate Revocation)
        std::string crlThisUpdate, crlNextUpdate, revocationReason;
        result.crlStatus = checkCrlStatus(dscCert, effectiveCountry, crlThisUpdate, crlNextUpdate, revocationReason);
        if (!crlThisUpdate.empty()) result.crlThisUpdate = crlThisUpdate;
        if (!crlNextUpdate.empty()) result.crlNextUpdate = crlNextUpdate;
        if (!revocationReason.empty()) result.crlRevocationReason = revocationReason;
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
        // ICAO Doc 9303 Part 12: Signature and CRL are hard requirements.
        // Point-in-time invalidity (cert not valid at signing time) is also a hard failure.
        result.valid = result.signatureVerified && !result.revoked && result.validAtSigningTime;

        // Expiration status
        if (!result.validAtSigningTime) {
            result.expirationStatus = "INVALID";
        } else if (result.dscExpired || result.cscaExpired) {
            result.expirationStatus = "EXPIRED";
        } else {
            result.expirationStatus = "VALID";
        }

        result.trustChainPath = "DSC → " + result.cscaSubject.substr(0, 50);
        result.trustChainDepth = 2;

        // Check DSC conformance status (nc-data LDAP lookup)
        auto conformanceInfo = certRepo_->checkDscConformance(dscCert, effectiveCountry);
        result.dscNonConformant = conformanceInfo.isNonConformant;
        result.pkdConformanceCode = conformanceInfo.conformanceCode;
        result.pkdConformanceText = conformanceInfo.conformanceText;

        // Free all CSCA candidates (including the selected one)
        for (X509* c : allCscas) X509_free(c);

    } catch (const std::exception& e) {
        spdlog::error("Certificate chain validation failed: {}", e.what());
        result.valid = false;
        result.validationErrors = e.what();
    }

    return result;
}

bool CertificateValidationService::verifyCertificateSignature(X509* cert, X509* issuerCert) {
    if (!cert || !issuerCert) return false;

    EVP_PKEY* issuerPubKey = X509_get_pubkey(issuerCert);
    if (!issuerPubKey) {
        spdlog::error("Failed to extract public key from issuer");
        return false;
    }

    int verifyResult = X509_verify(cert, issuerPubKey);
    EVP_PKEY_free(issuerPubKey);

    if (verifyResult != 1) {
        unsigned long err = ERR_get_error();
        char errBuf[256];
        ERR_error_string_n(err, errBuf, sizeof(errBuf));
        spdlog::warn("Signature verification failed: {}", errBuf);
        return false;
    }

    spdlog::debug("Certificate signature VALID");
    return true;
}

bool CertificateValidationService::isCertificateExpired(X509* cert) {
    if (!cert) return true;

    time_t now = time(nullptr);
    return (X509_cmp_time(X509_get0_notAfter(cert), &now) < 0);
}

domain::models::CrlStatus CertificateValidationService::checkCrlStatus(
    X509* cert,
    const std::string& countryCode,
    std::string& crlThisUpdate,
    std::string& crlNextUpdate,
    std::string& revocationReason)
{
    X509_CRL* crl = crlRepo_->findCrlByCountry(countryCode);
    if (!crl) {
        return domain::models::CrlStatus::CRL_UNAVAILABLE;
    }

    // Extract CRL thisUpdate / nextUpdate dates
    auto asn1TimeToString = [](const ASN1_TIME* t) -> std::string {
        if (!t) return "";
        struct tm tm_val;
        if (ASN1_TIME_to_tm(t, &tm_val) == 1) {
            char buf[32];
            strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_val);
            return std::string(buf);
        }
        return "";
    };

    crlThisUpdate = asn1TimeToString(X509_CRL_get0_lastUpdate(crl));
    crlNextUpdate = asn1TimeToString(X509_CRL_get0_nextUpdate(crl));
    spdlog::info("CRL dates - thisUpdate: {}, nextUpdate: {}", crlThisUpdate, crlNextUpdate);

    if (crlRepo_->isCrlExpired(crl)) {
        X509_CRL_free(crl);
        return domain::models::CrlStatus::CRL_EXPIRED;
    }

    // Check revocation with reason code extraction (RFC 5280 Section 5.3.1)
    bool isRevoked = false;
    ASN1_INTEGER* certSerial = X509_get_serialNumber(cert);
    if (certSerial) {
        X509_REVOKED* revokedEntry = nullptr;
        int ret = X509_CRL_get0_by_serial(crl, &revokedEntry, certSerial);
        if (ret == 1 && revokedEntry) {
            isRevoked = true;

            // Extract CRLReason from revoked entry extensions
            int reasonIdx = X509_REVOKED_get_ext_by_NID(revokedEntry, NID_crl_reason, -1);
            if (reasonIdx >= 0) {
                X509_EXTENSION* ext = X509_REVOKED_get_ext(revokedEntry, reasonIdx);
                if (ext) {
                    ASN1_ENUMERATED* reasonEnum = static_cast<ASN1_ENUMERATED*>(
                        X509V3_EXT_d2i(ext));
                    if (reasonEnum) {
                        long reasonCode = ASN1_ENUMERATED_get(reasonEnum);
                        // RFC 5280 CRLReason enumeration
                        switch (reasonCode) {
                            case 0: revocationReason = "unspecified"; break;
                            case 1: revocationReason = "keyCompromise"; break;
                            case 2: revocationReason = "cACompromise"; break;
                            case 3: revocationReason = "affiliationChanged"; break;
                            case 4: revocationReason = "superseded"; break;
                            case 5: revocationReason = "cessationOfOperation"; break;
                            case 6: revocationReason = "certificateHold"; break;
                            case 8: revocationReason = "removeFromCRL"; break;
                            case 9: revocationReason = "privilegeWithdrawn"; break;
                            case 10: revocationReason = "aACompromise"; break;
                            default: revocationReason = "unknown(" + std::to_string(reasonCode) + ")"; break;
                        }
                        ASN1_ENUMERATED_free(reasonEnum);
                        spdlog::info("CRL revocation reason: {}", revocationReason);
                    }
                }
            }
        }
    } else {
        // Fallback to repository method if serial extraction fails
        isRevoked = crlRepo_->isCertificateRevoked(cert, crl);
    }

    X509_CRL_free(crl);
    return isRevoked ? domain::models::CrlStatus::REVOKED : domain::models::CrlStatus::VALID;
}

std::vector<X509*> CertificateValidationService::buildTrustChain(
    X509* dscCert,
    const std::string& countryCode)
{
    std::vector<X509*> chain;
    chain.push_back(dscCert);

    std::string issuerDn = getIssuerDn(dscCert);
    X509* cscaCert = certRepo_->findCscaByIssuerDn(issuerDn, countryCode);

    if (cscaCert) {
        chain.push_back(cscaCert);
    }

    return chain;
}

std::string CertificateValidationService::validateExtensions(X509* cert, const std::string& role) {
    if (!cert) return "Certificate is null";

    std::string warnings;

    // RFC 5280 Section 4.2: Check for unknown critical extensions
    int extCount = X509_get_ext_count(cert);
    for (int i = 0; i < extCount; i++) {
        X509_EXTENSION* ext = X509_get_ext(cert, i);
        if (!ext) continue;

        if (X509_EXTENSION_get_critical(ext)) {
            ASN1_OBJECT* obj = X509_EXTENSION_get_object(ext);
            int nid = OBJ_obj2nid(obj);

            // Known critical extensions per ICAO 9303 / RFC 5280
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
            std::string warn = "Unknown critical extension: " + std::string(oidBuf);
            spdlog::warn("Extension validation ({}): {}", role, warn);
            if (!warnings.empty()) warnings += "; ";
            warnings += warn;
        }
    }

    // ICAO Doc 9303 Part 12 Section 4.6: Key Usage validation
    ASN1_BIT_STRING* usage = static_cast<ASN1_BIT_STRING*>(
        X509_get_ext_d2i(cert, NID_key_usage, nullptr, nullptr));
    if (usage) {
        if (role == "DSC") {
            // DSC must have digitalSignature (bit 0)
            if (!ASN1_BIT_STRING_get_bit(usage, 0)) {
                std::string warn = "DSC missing required digitalSignature key usage";
                spdlog::warn("Extension validation: {}", warn);
                if (!warnings.empty()) warnings += "; ";
                warnings += warn;
            }
        } else if (role == "CSCA") {
            // CSCA must have keyCertSign (bit 5)
            if (!ASN1_BIT_STRING_get_bit(usage, 5)) {
                std::string warn = "CSCA missing required keyCertSign key usage";
                spdlog::warn("Extension validation: {}", warn);
                if (!warnings.empty()) warnings += "; ";
                warnings += warn;
            }
            // CSCA should have cRLSign (bit 6)
            if (!ASN1_BIT_STRING_get_bit(usage, 6)) {
                spdlog::info("Extension validation: CSCA missing cRLSign key usage (recommended, not required)");
            }
        }
        ASN1_BIT_STRING_free(usage);
    } else if (role == "DSC") {
        spdlog::info("Extension validation: DSC has no Key Usage extension (unusual but not prohibited)");
    }

    return warnings;
}

CertificateValidationService::AlgorithmComplianceResult
CertificateValidationService::validateAlgorithmCompliance(X509* cert) {
    AlgorithmComplianceResult result;

    if (!cert) {
        result.compliant = false;
        result.warning = "Certificate is null";
        return result;
    }

    // Extract signature algorithm NID
    int sigNid = X509_get_signature_nid(cert);
    result.algorithm = OBJ_nid2sn(sigNid);

    // ICAO Doc 9303 Part 12 Appendix A - Approved algorithms
    // SHA-256/384/512 with RSA or ECDSA
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
            spdlog::warn("Algorithm compliance: {} - SHA-1 is deprecated", result.algorithm);
            break;

        // RSA-PSS variants
        case NID_rsassaPss:
            result.compliant = true;
            break;

        default:
            result.compliant = false;
            result.warning = "Unknown or non-ICAO-approved signature algorithm: " + result.algorithm;
            spdlog::warn("Algorithm compliance: {}", result.warning);
            break;
    }

    // Check RSA key size (ICAO requires minimum 2048 bits)
    EVP_PKEY* pkey = X509_get_pubkey(cert);
    if (pkey) {
        int keyType = EVP_PKEY_base_id(pkey);
        int keyBits = EVP_PKEY_bits(pkey);

        if (keyType == EVP_PKEY_RSA && keyBits < 2048) {
            result.warning = "RSA key size " + std::to_string(keyBits) +
                             " bits is below ICAO minimum of 2048 bits";
            spdlog::warn("Algorithm compliance: {}", result.warning);
        }

        EVP_PKEY_free(pkey);
    }

    return result;
}

std::string CertificateValidationService::getSubjectDn(X509* cert) {
    if (!cert) return "";
    X509_NAME* subject = X509_get_subject_name(cert);
    char buf[512];
    X509_NAME_oneline(subject, buf, sizeof(buf));
    return std::string(buf);
}

std::string CertificateValidationService::getIssuerDn(X509* cert) {
    if (!cert) return "";
    X509_NAME* issuer = X509_get_issuer_name(cert);
    char buf[512];
    X509_NAME_oneline(issuer, buf, sizeof(buf));
    return std::string(buf);
}

bool CertificateValidationService::isSelfSigned(X509* cert) {
    return getSubjectDn(cert) == getIssuerDn(cert);
}

} // namespace services
