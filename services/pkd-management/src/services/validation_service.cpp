/** @file validation_service.cpp
 *  @brief ValidationService implementation
 *
 *  Delegates pure validation logic to icao::validation shared library.
 *  This service handles orchestration: DB reads → validation → DB writes.
 */

#include "validation_service.h"
#include <icao/validation/cert_ops.h>
#include <icao/validation/types.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <ctime>
#include <openssl/x509.h>

namespace services {

// --- Constructor ---

ValidationService::ValidationService(
    repositories::ValidationRepository* validationRepo,
    repositories::CertificateRepository* certRepo,
    repositories::CrlRepository* crlRepo,
    icao::validation::ICscaProvider* ldapCscaProvider,
    icao::validation::ICrlProvider* ldapCrlProvider
)
    : validationRepo_(validationRepo)
    , certRepo_(certRepo)
    , crlRepo_(crlRepo)
    , ldapCscaProvider_(ldapCscaProvider)
    , ldapCrlProvider_(ldapCrlProvider)
{
    if (!validationRepo_) {
        throw std::invalid_argument("ValidationService: validationRepo cannot be nullptr");
    }
    if (!certRepo_) {
        throw std::invalid_argument("ValidationService: certRepo cannot be nullptr");
    }

    // Initialize DB-based provider adapters (for upload-time validation)
    cscaProvider_ = std::make_unique<adapters::DbCscaProvider>(certRepo_);
    trustChainBuilder_ = std::make_unique<icao::validation::TrustChainBuilder>(cscaProvider_.get());

    if (crlRepo_) {
        crlProvider_ = std::make_unique<adapters::DbCrlProvider>(crlRepo_);
        crlChecker_ = std::make_unique<icao::validation::CrlChecker>(crlProvider_.get());
    }

    // Initialize LDAP-based validation components (for real-time PA Lookup)
    if (ldapCscaProvider_) {
        ldapTrustChainBuilder_ = std::make_unique<icao::validation::TrustChainBuilder>(ldapCscaProvider_);
        spdlog::info("ValidationService: LDAP CSCA provider configured for real-time PA Lookup");
    }
    if (ldapCrlProvider_) {
        ldapCrlChecker_ = std::make_unique<icao::validation::CrlChecker>(ldapCrlProvider_);
        spdlog::info("ValidationService: LDAP CRL provider configured for real-time PA Lookup");
    }

    spdlog::info("ValidationService initialized (DB providers: yes, LDAP providers: {})",
                 ldapCscaProvider_ ? "yes" : "no");
}

// --- Public Methods - DSC Re-validation ---

ValidationService::RevalidateResult ValidationService::revalidateDscCertificates()
{
    spdlog::info("ValidationService::revalidateDscCertificates - Starting re-validation");

    RevalidateResult result;
    result.success = false;
    result.totalProcessed = 0;
    result.validCount = 0;
    result.expiredValidCount = 0;
    result.invalidCount = 0;
    result.pendingCount = 0;
    result.errorCount = 0;

    auto startTime = std::chrono::steady_clock::now();

    try {
        int limit = 50000;
        Json::Value dscs = certRepo_->findDscForRevalidation(limit);

        if (!dscs.isArray()) {
            result.success = false;
            result.message = "Failed to retrieve DSC certificates for re-validation";
            return result;
        }

        spdlog::info("Found {} DSC(s) for re-validation", dscs.size());

        for (const auto& dscInfo : dscs) {
            result.totalProcessed++;

            try {
                std::string certId = dscInfo.get("id", "").asString();
                std::string certDataHex = dscInfo.get("certificateData", "").asString();

                if (certDataHex.empty()) {
                    spdlog::warn("Empty certificate data for ID: {}", certId);
                    result.errorCount++;
                    continue;
                }

                X509* cert = certRepo_->parseCertificateDataFromHex(certDataHex);
                if (!cert) {
                    spdlog::error("Failed to parse X509 certificate for ID: {}", certId);
                    result.errorCount++;
                    continue;
                }

                ValidationResult valResult = validateCertificate(cert, "DSC");

                if (valResult.validationStatus == "VALID") {
                    result.validCount++;
                } else if (valResult.validationStatus == "EXPIRED_VALID") {
                    result.validCount++;
                    result.expiredValidCount++;
                } else if (valResult.validationStatus == "INVALID") {
                    result.invalidCount++;
                } else if (valResult.validationStatus == "PENDING") {
                    result.pendingCount++;
                } else {
                    result.errorCount++;
                }

                validationRepo_->updateRevalidation(
                    certId,
                    valResult.validationStatus,
                    valResult.trustChainValid,
                    valResult.cscaFound,
                    valResult.signatureValid,
                    valResult.trustChainPath.empty() ? valResult.errorMessage : valResult.trustChainPath,
                    valResult.cscaSubjectDn
                );

                X509_free(cert);

                spdlog::debug("Validated DSC {}: {}", certId, valResult.validationStatus);

            } catch (const std::exception& e) {
                spdlog::error("Error validating DSC: {}", e.what());
                result.errorCount++;
            }
        }

        result.success = true;
        result.message = "Re-validation completed successfully";

        spdlog::info("Re-validation complete: processed={}, valid={}, invalid={}, pending={}, error={}",
            result.totalProcessed, result.validCount, result.invalidCount, result.pendingCount, result.errorCount);

    } catch (const std::exception& e) {
        spdlog::error("ValidationService::revalidateDscCertificates failed: {}", e.what());
        result.success = false;
        result.message = e.what();
    }

    auto endTime = std::chrono::steady_clock::now();
    result.durationSeconds = std::chrono::duration<double>(endTime - startTime).count();

    return result;
}

// --- Public Methods - PENDING DSC Re-validation by Country ---

ValidationService::RevalidateResult ValidationService::revalidatePendingDscForCountries(
    const std::set<std::string>& countryCodes)
{
    spdlog::info("ValidationService::revalidatePendingDscForCountries - {} countries",
                 countryCodes.size());

    RevalidateResult result;
    result.success = false;
    result.totalProcessed = 0;
    result.validCount = 0;
    result.expiredValidCount = 0;
    result.invalidCount = 0;
    result.pendingCount = 0;
    result.errorCount = 0;

    if (countryCodes.empty()) {
        result.success = true;
        result.message = "No countries to re-validate";
        result.durationSeconds = 0.0;
        return result;
    }

    auto startTime = std::chrono::steady_clock::now();

    try {
        // Refresh CSCA cache to include newly added CSCAs
        cscaProvider_->preloadAllCscas();

        int limit = 50000;
        Json::Value dscs = certRepo_->findPendingDscByCountries(countryCodes, limit);

        if (!dscs.isArray() || dscs.empty()) {
            result.success = true;
            result.message = "No PENDING DSCs found for the given countries";
            auto endTime = std::chrono::steady_clock::now();
            result.durationSeconds = std::chrono::duration<double>(endTime - startTime).count();
            return result;
        }

        spdlog::info("Found {} PENDING DSC(s) for re-validation", dscs.size());

        for (const auto& dscInfo : dscs) {
            result.totalProcessed++;

            try {
                std::string certId = dscInfo.get("id", "").asString();
                std::string certDataHex = dscInfo.get("certificateData", "").asString();

                if (certDataHex.empty()) {
                    result.errorCount++;
                    continue;
                }

                X509* cert = certRepo_->parseCertificateDataFromHex(certDataHex);
                if (!cert) {
                    result.errorCount++;
                    continue;
                }

                ValidationResult valResult = validateCertificate(cert, "DSC");

                if (valResult.validationStatus == "VALID") {
                    result.validCount++;
                } else if (valResult.validationStatus == "EXPIRED_VALID") {
                    result.validCount++;
                    result.expiredValidCount++;
                } else if (valResult.validationStatus == "INVALID") {
                    result.invalidCount++;
                } else if (valResult.validationStatus == "PENDING") {
                    result.pendingCount++;
                } else {
                    result.errorCount++;
                }

                validationRepo_->updateRevalidation(
                    certId,
                    valResult.validationStatus,
                    valResult.trustChainValid,
                    valResult.cscaFound,
                    valResult.signatureValid,
                    valResult.trustChainPath.empty() ? valResult.errorMessage : valResult.trustChainPath,
                    valResult.cscaSubjectDn
                );

                X509_free(cert);

            } catch (const std::exception& e) {
                spdlog::error("Error re-validating PENDING DSC: {}", e.what());
                result.errorCount++;
            }
        }

        result.success = true;
        std::string countryList;
        for (const auto& cc : countryCodes) {
            if (!countryList.empty()) countryList += ",";
            countryList += cc;
        }
        result.message = "Re-validation completed for countries: " + countryList;

        spdlog::info("PENDING DSC re-validation complete: processed={}, valid={}, invalid={}, pending={}, error={}",
            result.totalProcessed, result.validCount, result.invalidCount, result.pendingCount, result.errorCount);

    } catch (const std::exception& e) {
        spdlog::error("revalidatePendingDscForCountries failed: {}", e.what());
        result.success = false;
        result.message = e.what();
    }

    auto endTime = std::chrono::steady_clock::now();
    result.durationSeconds = std::chrono::duration<double>(endTime - startTime).count();

    return result;
}

// --- Public Methods - Single Certificate Validation ---

ValidationService::ValidationResult ValidationService::validateCertificate(
    X509* cert,
    const std::string& certType
)
{
    ValidationResult result;
    result.trustChainValid = false;
    result.signatureValid = false;
    result.crlChecked = false;
    result.revoked = false;
    result.cscaFound = false;
    result.dscExpired = false;
    result.cscaExpired = false;
    result.validationStatus = "PENDING";

    if (!cert) {
        result.validationStatus = "ERROR";
        result.errorMessage = "Certificate is null";
        return result;
    }

    try {
        spdlog::debug("Validating {} certificate", certType);

        // Step 1: Check certificate expiration (ICAO hybrid model: informational)
        result.dscExpired = icao::validation::isCertificateExpired(cert);
        if (result.dscExpired) {
            spdlog::info("Certificate validation: DSC is expired (informational per ICAO 9303)");
        }

        if (icao::validation::isCertificateNotYetValid(cert)) {
            result.validationStatus = "INVALID";
            result.errorMessage = "Certificate is not yet valid";
            spdlog::warn("Certificate validation: Certificate is NOT YET VALID");
            return result;
        }

        // Step 2: Build and validate trust chain (via library)
        icao::validation::TrustChainResult chainResult = trustChainBuilder_->build(cert, 5);

        if (!chainResult.valid) {
            result.validationStatus = "INVALID";
            result.errorMessage = "Failed to build trust chain: " + chainResult.message;
            result.trustChainPath = chainResult.path;
            spdlog::warn("Certificate validation: {}", result.errorMessage);
            return result;
        }

        result.cscaFound = true;
        result.trustChainPath = chainResult.path;
        result.cscaSubjectDn = chainResult.cscaSubjectDn;
        result.cscaFingerprint = chainResult.cscaFingerprint;
        result.cscaExpired = chainResult.cscaExpired;
        result.signatureValid = true;
        result.trustChainValid = true;

        spdlog::info("Certificate validation: Trust chain built ({} steps)", chainResult.depth);

        // Determine validation status per ICAO Doc 9303 hybrid chain model
        if (result.dscExpired || result.cscaExpired) {
            result.validationStatus = "EXPIRED_VALID";
            spdlog::info("Certificate validation: Trust Chain VERIFIED (expired) - Path: {}", result.trustChainPath);
        } else {
            result.validationStatus = "VALID";
            spdlog::info("Certificate validation: Trust Chain VERIFIED - Path: {}", result.trustChainPath);
        }

        // Step 3: CRL revocation check (ICAO Doc 9303 Part 11)
        if (result.trustChainValid && crlChecker_) {
            std::string countryCode = icao::validation::extractDnAttribute(
                icao::validation::getIssuerDn(cert), "C");

            if (!countryCode.empty()) {
                icao::validation::CrlCheckResult crlResult = crlChecker_->check(cert, countryCode);
                result.crlChecked = true;
                result.revoked = (crlResult.status == icao::validation::CrlCheckStatus::REVOKED);
                result.crlMessage = crlResult.message;

                if (result.revoked) {
                    spdlog::warn("Certificate validation: Certificate is REVOKED");
                }
            }
        }

    } catch (const std::exception& e) {
        spdlog::error("Certificate validation failed: {}", e.what());
        result.validationStatus = "ERROR";
        result.errorMessage = e.what();
    }

    return result;
}

// --- Public Methods - Validation Result Retrieval ---

Json::Value ValidationService::getValidationByFingerprint(const std::string& fingerprint)
{
    spdlog::info("ValidationService::getValidationByFingerprint - fingerprint: {}",
        fingerprint.substr(0, 16) + "...");

    Json::Value response;

    try {
        Json::Value validation = validationRepo_->findByFingerprint(fingerprint);
        response["success"] = true;
        if (validation.isNull()) {
            response["validation"] = Json::nullValue;
        } else {
            response["validation"] = validation;
        }
    } catch (const std::exception& e) {
        spdlog::error("ValidationService::getValidationByFingerprint failed: {}", e.what());
        response["success"] = false;
        response["error"] = e.what();
    }

    return response;
}

Json::Value ValidationService::getValidationBySubjectDn(const std::string& subjectDn)
{
    spdlog::info("ValidationService::getValidationBySubjectDn - subjectDn: {}",
        subjectDn.substr(0, 60) + "...");

    Json::Value response;

    try {
        Json::Value validation = validationRepo_->findBySubjectDn(subjectDn);
        response["success"] = true;
        if (validation.isNull()) {
            response["validation"] = Json::nullValue;
        } else {
            response["validation"] = validation;
        }
    } catch (const std::exception& e) {
        spdlog::error("ValidationService::getValidationBySubjectDn failed: {}", e.what());
        response["success"] = false;
        response["error"] = e.what();
    }

    return response;
}

Json::Value ValidationService::getValidationsByUploadId(
    const std::string& uploadId,
    int limit,
    int offset,
    const std::string& statusFilter,
    const std::string& certTypeFilter
)
{
    spdlog::info("ValidationService::getValidationsByUploadId - uploadId: {}, limit: {}, offset: {}, status: {}, certType: {}",
        uploadId, limit, offset, statusFilter, certTypeFilter);

    Json::Value response;

    try {
        response = validationRepo_->findByUploadId(uploadId, limit, offset, statusFilter, certTypeFilter);
    } catch (const std::exception& e) {
        spdlog::error("ValidationService::getValidationsByUploadId failed: {}", e.what());
        response["success"] = false;
        response["error"] = e.what();
        response["count"] = 0;
        response["total"] = 0;
        response["validations"] = Json::arrayValue;
    }

    return response;
}

Json::Value ValidationService::getValidationStatistics(const std::string& uploadId)
{
    spdlog::info("ValidationService::getValidationStatistics - uploadId: {}", uploadId);

    Json::Value response;

    try {
        Json::Value stats = validationRepo_->getStatisticsByUploadId(uploadId);
        if (stats.isMember("error")) {
            response["success"] = false;
            response["error"] = stats["error"];
            return response;
        }

        response["success"] = true;
        response["data"] = stats;

        spdlog::info("ValidationService::getValidationStatistics - Returned statistics: total={}, valid={}, invalid={}",
            stats.get("totalCount", 0).asInt(),
            stats.get("validCount", 0).asInt(),
            stats.get("invalidCount", 0).asInt());

    } catch (const std::exception& e) {
        spdlog::error("ValidationService::getValidationStatistics failed: {}", e.what());
        response["success"] = false;
        response["error"] = e.what();
    }

    return response;
}

// --- Public Methods - Link Certificate Validation ---

ValidationService::LinkCertValidationResult ValidationService::validateLinkCertificate(X509* cert)
{
    LinkCertValidationResult result;
    result.isValid = false;
    result.chainLength = 0;

    if (!cert) {
        result.message = "Certificate is null";
        return result;
    }

    spdlog::info("ValidationService::validateLinkCertificate - Starting validation");

    try {
        // Step 1: Verify this is actually a Link Certificate (via library)
        if (!icao::validation::isLinkCertificate(cert)) {
            result.message = "Certificate does not meet Link Certificate criteria "
                             "(requires: not self-signed, CA:TRUE, keyCertSign)";
            return result;
        }

        // Step 2: Build trust chain from Link Certificate to root CSCA (via library)
        icao::validation::TrustChainResult chainResult = trustChainBuilder_->build(cert, 5);

        if (!chainResult.valid) {
            result.message = "Failed to build trust chain: " + chainResult.message;
            result.trustChainPath = chainResult.path;
            spdlog::warn("Link cert validation: {}", result.message);
            return result;
        }

        result.trustChainPath = chainResult.path;
        result.chainLength = chainResult.depth;

        // Step 3: Validation successful
        result.isValid = true;
        if (chainResult.cscaExpired) {
            result.message = "Link Certificate trust chain verified (CSCA expired, informational per ICAO 9303)";
        } else {
            result.message = "Link Certificate trust chain verified successfully";
        }

        spdlog::info("Link cert validation: {} (chain length: {})", result.message, result.chainLength);

    } catch (const std::exception& e) {
        spdlog::error("Link Certificate validation failed: {}", e.what());
        result.isValid = false;
        result.message = e.what();
    }

    return result;
}

// --- Public Methods - Real-time LDAP Validation (PA Lookup) ---

Json::Value ValidationService::validateDscRealTime(
    const std::string& subjectDn,
    const std::string& fingerprint)
{
    spdlog::info("ValidationService::validateDscRealTime - subjectDn: '{}', fingerprint: '{}'",
        subjectDn.empty() ? "(empty)" : subjectDn.substr(0, 60),
        fingerprint.empty() ? "(empty)" : fingerprint.substr(0, 16));

    Json::Value response;

    // Check LDAP providers are available
    if (!ldapTrustChainBuilder_) {
        spdlog::warn("validateDscRealTime: LDAP CSCA provider not configured, falling back to DB result");
        // Fallback to pre-computed DB results
        if (!subjectDn.empty()) {
            return getValidationBySubjectDn(subjectDn);
        } else {
            return getValidationByFingerprint(fingerprint);
        }
    }

    try {
        // Step 1: Find DSC certificate from DB (with binary data for X509 parsing)
        Json::Value dscInfo;
        if (!fingerprint.empty()) {
            dscInfo = certRepo_->findDscWithBinaryByFingerprint(fingerprint);
        } else {
            dscInfo = certRepo_->findDscWithBinaryBySubjectDn(subjectDn);
        }

        if (dscInfo.isNull() || dscInfo.empty()) {
            response["success"] = true;
            response["validation"] = Json::nullValue;
            return response;
        }

        std::string certDataHex = dscInfo.get("certificate_data", "").asString();
        if (certDataHex.empty()) {
            response["success"] = true;
            response["validation"] = Json::nullValue;
            spdlog::warn("validateDscRealTime: DSC found but no certificate binary data");
            return response;
        }

        // Step 2: Parse X509 certificate from hex data
        X509* cert = certRepo_->parseCertificateDataFromHex(certDataHex);
        if (!cert) {
            response["success"] = false;
            response["error"] = "Failed to parse DSC certificate binary";
            return response;
        }

        // Step 3: Build trust chain via LDAP CSCA lookup
        icao::validation::TrustChainResult chainResult = ldapTrustChainBuilder_->build(cert, 5);

        // Step 4: Check CRL via LDAP (if available)
        bool crlChecked = false;
        bool revoked = false;
        std::string crlMessage;
        std::string crlCheckStatus = "NOT_CHECKED";

        if (chainResult.valid && ldapCrlChecker_) {
            std::string countryCode = dscInfo.get("country_code", "").asString();
            if (countryCode.empty()) {
                countryCode = icao::validation::extractDnAttribute(
                    icao::validation::getIssuerDn(cert), "C");
            }

            if (!countryCode.empty()) {
                icao::validation::CrlCheckResult crlResult = ldapCrlChecker_->check(cert, countryCode);
                crlChecked = true;
                revoked = (crlResult.status == icao::validation::CrlCheckStatus::REVOKED);
                crlMessage = crlResult.message;

                if (crlResult.status == icao::validation::CrlCheckStatus::REVOKED) {
                    crlCheckStatus = "REVOKED";
                } else if (crlResult.status == icao::validation::CrlCheckStatus::VALID) {
                    crlCheckStatus = "GOOD";
                } else if (crlResult.status == icao::validation::CrlCheckStatus::CRL_UNAVAILABLE) {
                    crlCheckStatus = "CRL_NOT_FOUND";
                } else if (crlResult.status == icao::validation::CrlCheckStatus::CRL_EXPIRED) {
                    crlCheckStatus = "CRL_EXPIRED";
                } else {
                    crlCheckStatus = "NOT_CHECKED";
                }
            }
        }

        // Step 5: Determine validation status
        bool dscExpired = icao::validation::isCertificateExpired(cert);
        std::string validationStatus;

        if (!chainResult.valid) {
            validationStatus = "INVALID";
        } else if (dscExpired || chainResult.cscaExpired) {
            validationStatus = "EXPIRED_VALID";
        } else {
            validationStatus = "VALID";
        }

        // Step 6: Assemble response (matching existing format)
        Json::Value validation;
        validation["validationStatus"] = validationStatus;
        validation["trustChainValid"] = chainResult.valid;
        validation["trustChainMessage"] = chainResult.valid
            ? ("Trust chain verified via LDAP (" + std::to_string(chainResult.depth) + " steps)")
            : ("Trust chain failed: " + chainResult.message);
        validation["trustChainPath"] = chainResult.path;
        validation["signatureValid"] = chainResult.valid;
        validation["cscaFound"] = chainResult.valid;
        validation["cscaSubjectDn"] = chainResult.cscaSubjectDn;
        validation["cscaFingerprint"] = chainResult.cscaFingerprint;
        validation["dscExpired"] = dscExpired;
        validation["cscaExpired"] = chainResult.cscaExpired;
        validation["crlChecked"] = crlChecked;
        validation["revoked"] = revoked;
        validation["crlMessage"] = crlMessage;
        validation["crlCheckStatus"] = crlCheckStatus;
        validation["validationSource"] = "LDAP_REALTIME";

        // Copy DSC info from DB (column names are snake_case from query executor)
        validation["subjectDn"] = dscInfo.get("subject_dn", "").asString();
        validation["issuerDn"] = dscInfo.get("issuer_dn", "").asString();
        validation["countryCode"] = dscInfo.get("country_code", "").asString();
        validation["certificateType"] = dscInfo.get("certificate_type", "").asString();
        validation["fingerprint"] = dscInfo.get("fingerprint_sha256", "").asString();
        validation["serialNumber"] = dscInfo.get("serial_number", "").asString();
        validation["notBefore"] = dscInfo.get("not_before", "").asString();
        validation["notAfter"] = dscInfo.get("not_after", "").asString();

        if (!chainResult.valid) {
            validation["errorMessage"] = chainResult.message;
        }

        X509_free(cert);

        response["success"] = true;
        response["validation"] = validation;

        spdlog::info("validateDscRealTime: {} (trust chain: {}, CRL: {})",
            validationStatus,
            chainResult.valid ? "verified" : "failed",
            crlCheckStatus);

    } catch (const std::exception& e) {
        spdlog::error("validateDscRealTime failed: {}", e.what());
        response["success"] = false;
        response["error"] = e.what();
    }

    return response;
}

} // namespace services
