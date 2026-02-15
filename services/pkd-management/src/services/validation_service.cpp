/** @file validation_service.cpp
 *  @brief ValidationService implementation
 */

#include "validation_service.h"
#include <spdlog/spdlog.h>
#include <cstring>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <set>
#include <algorithm>
#include <openssl/evp.h>
#include <openssl/x509v3.h>
#include <openssl/err.h>

namespace services {

// --- Constructor & Destructor ---

ValidationService::ValidationService(
    repositories::ValidationRepository* validationRepo,
    repositories::CertificateRepository* certRepo,
    repositories::CrlRepository* crlRepo
)
    : validationRepo_(validationRepo)
    , certRepo_(certRepo)
    , crlRepo_(crlRepo)
{
    if (!validationRepo_) {
        throw std::invalid_argument("ValidationService: validationRepo cannot be nullptr");
    }
    if (!certRepo_) {
        throw std::invalid_argument("ValidationService: certRepo cannot be nullptr");
    }
    spdlog::info("ValidationService initialized with Repository dependencies");
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
        // Process all DSCs with csca_found=false
        int limit = 50000;

        // Step 1: Get DSC certificates that need re-validation
        Json::Value dscs = certRepo_->findDscForRevalidation(limit);

        if (!dscs.isArray()) {
            result.success = false;
            result.message = "Failed to retrieve DSC certificates for re-validation";
            return result;
        }

        spdlog::info("Found {} DSC(s) for re-validation", dscs.size());

        // Step 2: Validate each DSC
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

                // Parse certificate data (handles double-encoded BYTEA)
                X509* cert = certRepo_->parseCertificateDataFromHex(certDataHex);
                if (!cert) {
                    spdlog::error("Failed to parse X509 certificate for ID: {}", certId);
                    result.errorCount++;
                    continue;
                }

                // Validate certificate (ICAO Doc 9303 hybrid chain model)
                ValidationResult valResult = validateCertificate(cert, "DSC");

                // Count results
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

                // Save validation result to database
                validationRepo_->updateRevalidation(
                    certId,
                    valResult.validationStatus,
                    valResult.trustChainValid,
                    valResult.cscaFound,
                    valResult.signatureValid,
                    valResult.trustChainPath.empty() ? valResult.errorMessage : valResult.trustChainPath,
                    valResult.cscaSubjectDn
                );

                // Free X509 certificate
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

        // Step 1: Check certificate expiration (ICAO hybrid model: informational, not hard failure)
        // Per ICAO Doc 9303 Part 12: DSC validity ~3 months, passport validity ~10 years
        // Expired DSC is normal and expected; cryptographic validity is the hard requirement
        time_t now = time(nullptr);
        if (X509_cmp_time(X509_get0_notAfter(cert), &now) < 0) {
            result.dscExpired = true;
            spdlog::info("Certificate validation: DSC is expired (informational per ICAO 9303)");
        }
        if (X509_cmp_time(X509_get0_notBefore(cert), &now) > 0) {
            // NOT_YET_VALID is a hard failure (certificate not yet active)
            result.validationStatus = "INVALID";
            result.errorMessage = "Certificate is not yet valid";
            spdlog::warn("Certificate validation: Certificate is NOT YET VALID");
            return result;
        }

        // Step 2: Get issuer DN to find CSCA
        std::string issuerDn = getIssuerDn(cert);
        if (issuerDn.empty()) {
            result.validationStatus = "ERROR";
            result.errorMessage = "Failed to extract issuer DN";
            return result;
        }

        // Step 3: Build trust chain
        TrustChain chain = buildTrustChain(cert, 5);

        if (!chain.isValid) {
            result.validationStatus = "INVALID";
            result.errorMessage = "Failed to build trust chain: " + chain.message;
            result.trustChainPath = chain.path;
            spdlog::warn("Certificate validation: {}", result.errorMessage);
            return result;
        }

        result.cscaFound = true;
        result.trustChainPath = chain.path;
        spdlog::info("Certificate validation: Trust chain built ({} steps)", chain.certificates.size());

        // Step 4: Validate trust chain signatures (ICAO hybrid model)
        // Signature verification is a HARD requirement; expiration is informational
        bool cscaExpired = false;
        bool signaturesValid = validateTrustChainInternal(chain, cscaExpired);
        result.cscaExpired = cscaExpired;

        if (signaturesValid) {
            result.signatureValid = true;
            result.trustChainValid = true;

            // Determine validation status per ICAO Doc 9303 hybrid chain model
            if (result.dscExpired || result.cscaExpired) {
                result.validationStatus = "EXPIRED_VALID";
                spdlog::info("Certificate validation: Trust Chain VERIFIED (expired) - Path: {}", result.trustChainPath);
            } else {
                result.validationStatus = "VALID";
                spdlog::info("Certificate validation: Trust Chain VERIFIED - Path: {}", result.trustChainPath);
            }
        } else {
            result.validationStatus = "INVALID";
            result.errorMessage = "Trust chain signature verification failed";
            spdlog::error("Certificate validation: Trust Chain FAILED - {}", result.errorMessage);
        }

        // Step 5: CRL revocation check (ICAO Doc 9303 Part 11)
        if (result.trustChainValid && crlRepo_) {
            bool revoked = checkCrlRevocation(cert);
            result.crlChecked = true;
            result.revoked = revoked;
            if (revoked) {
                result.crlMessage = "Certificate is revoked per CRL";
                spdlog::warn("Certificate validation: Certificate is REVOKED");
            } else {
                result.crlMessage = "Certificate not revoked";
            }
        }

        // Cleanup chain certificates (except first one which is the input cert)
        for (size_t i = 1; i < chain.certificates.size(); i++) {
            X509_free(chain.certificates[i]);
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
        // Use Repository to get validation result
        Json::Value validation = validationRepo_->findByFingerprint(fingerprint);

        // Wrap in proper response format
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
        // Use Repository to get validation results
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
        // Get statistics from repository
        Json::Value stats = validationRepo_->getStatisticsByUploadId(uploadId);

        // Check if there was an error
        if (stats.isMember("error")) {
            response["success"] = false;
            response["error"] = stats["error"];
            return response;
        }

        // Build response with statistics
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
        // Step 1: Verify this is actually a Link Certificate
        if (!isLinkCertificate(cert)) {
            result.message = "Certificate does not meet Link Certificate criteria "
                             "(requires: not self-signed, CA:TRUE, keyCertSign)";
            return result;
        }

        // Step 2: Build trust chain from Link Certificate to root CSCA
        TrustChain chain = buildTrustChain(cert, 5);

        if (!chain.isValid) {
            result.message = "Failed to build trust chain: " + chain.message;
            result.trustChainPath = chain.path;
            spdlog::warn("Link cert validation: {}", result.message);
            return result;
        }

        result.trustChainPath = chain.path;
        result.chainLength = static_cast<int>(chain.certificates.size());

        // Collect subject DNs for result
        for (X509* chainCert : chain.certificates) {
            result.certificateDns.push_back(getSubjectDn(chainCert));
        }

        // Step 3: Validate all signatures in chain (HARD requirement)
        bool cscaExpired = false;
        bool signaturesValid = validateTrustChainInternal(chain, cscaExpired);

        if (!signaturesValid) {
            result.message = "Link Certificate trust chain signature verification failed";
            spdlog::error("Link cert validation: {}", result.message);
            // Cleanup chain certificates (except first)
            for (size_t i = 1; i < chain.certificates.size(); i++) {
                X509_free(chain.certificates[i]);
            }
            return result;
        }

        // Step 4: Validation successful
        result.isValid = true;
        if (cscaExpired) {
            result.message = "Link Certificate trust chain verified (CSCA expired, informational per ICAO 9303)";
        } else {
            result.message = "Link Certificate trust chain verified successfully";
        }

        spdlog::info("Link cert validation: {} (chain length: {})", result.message, result.chainLength);

        // Cleanup chain certificates (except first which is input cert)
        for (size_t i = 1; i < chain.certificates.size(); i++) {
            X509_free(chain.certificates[i]);
        }

    } catch (const std::exception& e) {
        spdlog::error("Link Certificate validation failed: {}", e.what());
        result.isValid = false;
        result.message = e.what();
    }

    return result;
}

// --- Private Methods - Trust Chain Building ---

ValidationService::TrustChain ValidationService::buildTrustChain(X509* leafCert, int maxDepth)
{
    TrustChain chain;
    chain.isValid = false;

    if (!leafCert) {
        chain.message = "Leaf certificate is null";
        return chain;
    }

    spdlog::debug("Building trust chain (maxDepth: {})", maxDepth);

    try {
        // Step 1: Get issuer DN from leaf certificate to find all potential CSCAs
        std::string leafIssuerDn = getIssuerDn(leafCert);
        if (leafIssuerDn.empty()) {
            chain.message = "Failed to extract issuer DN from leaf certificate";
            return chain;
        }

        // Step 2: Find ALL CSCAs matching the issuer DN (including link certificates)
        std::vector<X509*> allCscas = certRepo_->findAllCscasBySubjectDn(leafIssuerDn);
        if (allCscas.empty()) {
            chain.message = "No CSCA found for issuer: " + leafIssuerDn.substr(0, 80);
            spdlog::warn("Trust chain building: {}", chain.message);
            return chain;
        }

        spdlog::info("Found {} CSCA(s) for issuer (may include link certs)", allCscas.size());

        // Step 3: Add leaf certificate as first in chain
        chain.certificates.push_back(leafCert);

        // Step 4: Build chain iteratively
        X509* current = leafCert;
        std::set<std::string> visitedDns;  // Prevent circular references
        int depth = 0;

        while (depth < maxDepth) {
            depth++;

            // Check if current certificate is self-signed (root)
            if (isSelfSigned(current)) {
                // Verify self-signature (RFC 5280 Section 6.1)
                // A tampered root CSCA with correct DN but invalid self-signature must be rejected
                if (!verifyCertificateSignature(current, current)) {
                    chain.isValid = false;
                    chain.message = "Root CSCA self-signature verification failed at depth " + std::to_string(depth);
                    spdlog::error("Chain building: {}", chain.message);
                    for (X509* csca : allCscas) X509_free(csca);
                    return chain;
                }
                chain.isValid = true;
                spdlog::info("Chain building: Reached root CSCA at depth {} (self-signature verified)", depth);
                break;
            }

            // Get issuer DN of current certificate
            std::string currentIssuerDn = getIssuerDn(current);
            if (currentIssuerDn.empty()) {
                chain.message = "Failed to extract issuer DN at depth " + std::to_string(depth);
                // Cleanup allocated CSCAs
                for (X509* csca : allCscas) X509_free(csca);
                return chain;
            }

            // Prevent circular references
            if (visitedDns.count(currentIssuerDn) > 0) {
                chain.message = "Circular reference detected at depth " + std::to_string(depth);
                spdlog::error("Chain building: {}", chain.message);
                // Cleanup
                for (X509* csca : allCscas) X509_free(csca);
                return chain;
            }
            visitedDns.insert(currentIssuerDn);

            // Find issuer certificate in CSCA list
            // ICAO 9303 Part 12: When multiple CSCAs share the same DN (key rollover),
            // select the one whose public key successfully verifies the current certificate's signature.
            X509* issuer = nullptr;
            X509* dnMatchFallback = nullptr;  // Fallback: first DN match (if no signature matches)
            for (X509* csca : allCscas) {
                std::string cscaSubjectDn = getSubjectDn(csca);

                // Case-insensitive DN comparison
                if (strcasecmp(currentIssuerDn.c_str(), cscaSubjectDn.c_str()) == 0) {
                    // DN matches - verify signature to confirm correct key pair
                    EVP_PKEY* cscaPubKey = X509_get_pubkey(csca);
                    if (cscaPubKey) {
                        int verifyResult = X509_verify(current, cscaPubKey);
                        EVP_PKEY_free(cscaPubKey);
                        if (verifyResult == 1) {
                            issuer = csca;
                            spdlog::debug("Chain building: Found issuer at depth {} (signature verified): {}",
                                          depth, cscaSubjectDn.substr(0, 50));
                            break;
                        } else {
                            spdlog::debug("Chain building: DN match but signature failed at depth {}: {}",
                                          depth, cscaSubjectDn.substr(0, 50));
                            if (!dnMatchFallback) dnMatchFallback = csca;
                        }
                    } else {
                        spdlog::warn("Chain building: Failed to extract public key from CSCA: {}",
                                     cscaSubjectDn.substr(0, 50));
                        if (!dnMatchFallback) dnMatchFallback = csca;
                    }
                }
            }
            // If no signature-verified match found, use DN-only match for error reporting
            if (!issuer && dnMatchFallback) {
                spdlog::warn("Chain building: No signature-verified CSCA found at depth {}, "
                             "using DN match fallback for chain path reporting", depth);
                issuer = dnMatchFallback;
            }

            if (!issuer) {
                chain.message = "Chain broken: Issuer not found at depth " +
                                std::to_string(depth) + " (issuer: " +
                                currentIssuerDn.substr(0, 80) + ")";
                spdlog::warn("Chain building: {}", chain.message);
                // Cleanup
                for (X509* csca : allCscas) X509_free(csca);
                return chain;
            }

            // Add issuer to chain
            chain.certificates.push_back(issuer);
            current = issuer;
        }

        if (depth >= maxDepth) {
            chain.message = "Maximum chain depth exceeded (" + std::to_string(maxDepth) + ")";
            chain.isValid = false;
            // Cleanup
            for (X509* csca : allCscas) X509_free(csca);
            return chain;
        }

        // Step 5: Build human-readable path
        chain.path = "DSC";
        for (size_t i = 1; i < chain.certificates.size(); i++) {
            std::string subjectDn = getSubjectDn(chain.certificates[i]);
            // Extract CN from DN for readability
            size_t cnPos = subjectDn.find("CN=");
            std::string cnPart = (cnPos != std::string::npos)
                                 ? subjectDn.substr(cnPos, 30)
                                 : subjectDn.substr(0, 30);
            chain.path += " → " + cnPart;
        }

        spdlog::info("Trust chain built successfully: {}", chain.path);

        // Note: We don't free allCscas here because chain.certificates contains pointers to them
        // The caller must manage X509* lifetime

    } catch (const std::exception& e) {
        spdlog::error("Trust chain building failed: {}", e.what());
        chain.isValid = false;
        chain.message = e.what();
    }

    return chain;
}

X509* ValidationService::findCscaByIssuerDn(const std::string& issuerDn)
{
    spdlog::debug("Finding CSCA by issuer DN: {}...", issuerDn.substr(0, 80));

    try {
        // Use CertificateRepository to find CSCA
        return certRepo_->findCscaByIssuerDn(issuerDn);

    } catch (const std::exception& e) {
        spdlog::error("CSCA lookup failed: {}", e.what());
        return nullptr;
    }
}

bool ValidationService::verifyCertificateSignature(X509* cert, X509* issuerCert)
{
    if (!cert || !issuerCert) {
        return false;
    }

    spdlog::debug("Verifying certificate signature");

    try {
        // Extract issuer's public key
        EVP_PKEY* issuerPubKey = X509_get_pubkey(issuerCert);
        if (!issuerPubKey) {
            spdlog::error("Failed to extract public key from issuer certificate");
            return false;
        }

        // Verify signature
        int verifyResult = X509_verify(cert, issuerPubKey);
        EVP_PKEY_free(issuerPubKey);

        if (verifyResult != 1) {
            unsigned long err = ERR_get_error();
            char errBuf[256];
            ERR_error_string_n(err, errBuf, sizeof(errBuf));
            spdlog::error("Signature verification FAILED: {}", errBuf);
            return false;
        }

        spdlog::debug("Certificate signature VALID");
        return true;

    } catch (const std::exception& e) {
        spdlog::error("Signature verification failed: {}", e.what());
        return false;
    }
}

bool ValidationService::validateTrustChainInternal(const TrustChain& chain, bool& cscaExpired)
{
    cscaExpired = false;

    if (!chain.isValid) {
        spdlog::warn("Chain validation: Chain is already marked as invalid");
        return false;
    }

    if (chain.certificates.empty()) {
        spdlog::error("Chain validation: No certificates in chain");
        return false;
    }

    time_t now = time(nullptr);

    // ICAO Doc 9303 Part 12 hybrid chain model:
    // - Signature verification: HARD requirement (must pass)
    // - Certificate expiration: INFORMATIONAL (reported but does not fail validation)
    // Rationale: CSCA validity 13-15 years, DSC validity ~3 months, passport validity ~10 years
    // An expired CSCA's public key can still cryptographically verify DSC signatures

    // Validate each certificate in chain (starting from index 1, skipping the leaf DSC)
    for (size_t i = 1; i < chain.certificates.size(); i++) {
        X509* cert = chain.certificates[i];
        X509* issuer = (i + 1 < chain.certificates.size())
                       ? chain.certificates[i + 1]
                       : cert;  // Last cert is self-signed

        // Check expiration (informational per ICAO hybrid model)
        if (X509_cmp_time(X509_get0_notAfter(cert), &now) < 0) {
            cscaExpired = true;
            spdlog::info("Chain validation: CSCA at depth {} is expired (informational per ICAO 9303)", i);
        }

        // Verify signature (cert signed by issuer) - HARD requirement
        if (!verifyCertificateSignature(cert, issuer)) {
            spdlog::error("Chain validation: Signature verification FAILED at depth {}", i);
            return false;
        }

        spdlog::debug("Chain validation: Certificate {} signature VALID", i);
    }

    if (cscaExpired) {
        spdlog::info("Chain validation: Trust chain signatures VALID, CSCA expired ({} certificates)",
                     chain.certificates.size());
    } else {
        spdlog::info("Chain validation: Trust chain VALID ({} certificates)",
                     chain.certificates.size());
    }
    return true;
}

// --- Private Methods - CRL Check ---

bool ValidationService::checkCrlRevocation(X509* cert)
{
    if (!cert) {
        return false;
    }

    if (!crlRepo_) {
        spdlog::debug("CRL check skipped: CrlRepository not available");
        return false;
    }

    spdlog::debug("Checking CRL revocation");

    try {
        // Extract country code from certificate issuer DN
        std::string issuerDn = getIssuerDn(cert);
        std::string countryCode = extractDnAttribute(issuerDn, "C");
        if (countryCode.empty()) {
            spdlog::warn("CRL check: Cannot extract country code from issuer DN: {}", issuerDn);
            return false;
        }

        // Lookup CRL from database by country code
        Json::Value crlData = crlRepo_->findByCountryCode(countryCode);
        if (crlData.isNull()) {
            spdlog::info("CRL check: No CRL found for country {}", countryCode);
            return false;
        }

        std::string crlBinaryHex = crlData.get("crl_binary", "").asString();
        if (crlBinaryHex.empty()) {
            spdlog::warn("CRL check: Empty CRL binary for country {}", countryCode);
            return false;
        }

        // Decode hex to DER bytes (handle \x prefix and double-encoding)
        std::vector<uint8_t> derBytes;
        size_t hexStart = 0;
        if (crlBinaryHex.size() > 2 && crlBinaryHex[0] == '\\' && crlBinaryHex[1] == 'x') {
            hexStart = 2;
        }
        derBytes.reserve((crlBinaryHex.size() - hexStart) / 2);
        for (size_t i = hexStart; i + 1 < crlBinaryHex.size(); i += 2) {
            char h[3] = {crlBinaryHex[i], crlBinaryHex[i + 1], 0};
            derBytes.push_back(static_cast<uint8_t>(strtol(h, nullptr, 16)));
        }

        // Handle double-encoded BYTEA (decoded bytes start with \x = 0x5C 0x78)
        if (derBytes.size() > 2 && derBytes[0] == 0x5C && derBytes[1] == 0x78) {
            std::vector<uint8_t> innerBytes;
            innerBytes.reserve((derBytes.size() - 2) / 2);
            for (size_t i = 2; i + 1 < derBytes.size(); i += 2) {
                char h[3] = {static_cast<char>(derBytes[i]), static_cast<char>(derBytes[i + 1]), 0};
                innerBytes.push_back(static_cast<uint8_t>(strtol(h, nullptr, 16)));
            }
            derBytes = std::move(innerBytes);
        }

        if (derBytes.empty()) {
            spdlog::warn("CRL check: Failed to decode CRL binary for country {}", countryCode);
            return false;
        }

        // Parse DER bytes to X509_CRL
        const unsigned char* p = derBytes.data();
        X509_CRL* crl = d2i_X509_CRL(nullptr, &p, static_cast<long>(derBytes.size()));
        if (!crl) {
            spdlog::warn("CRL check: Failed to parse CRL DER for country {}", countryCode);
            return false;
        }

        // Check if CRL is expired (nextUpdate < now)
        const ASN1_TIME* nextUpdate = X509_CRL_get0_nextUpdate(crl);
        if (nextUpdate) {
            time_t now = time(nullptr);
            if (X509_cmp_time(nextUpdate, &now) < 0) {
                spdlog::info("CRL check: CRL expired for country {} (informational)", countryCode);
                // Continue checking - expired CRL still provides information
            }
        }

        // Check certificate serial number against CRL
        ASN1_INTEGER* certSerial = X509_get_serialNumber(cert);
        bool isRevoked = false;
        if (certSerial) {
            X509_REVOKED* revokedEntry = nullptr;
            int ret = X509_CRL_get0_by_serial(crl, &revokedEntry, certSerial);
            if (ret == 1 && revokedEntry) {
                isRevoked = true;
                spdlog::warn("CRL check: Certificate REVOKED (country: {})", countryCode);
            }
        }

        X509_CRL_free(crl);

        if (!isRevoked) {
            spdlog::debug("CRL check: Certificate not revoked (country: {})", countryCode);
        }

        return isRevoked;

    } catch (const std::exception& e) {
        spdlog::error("CRL check failed: {}", e.what());
        return false;
    }
}

// --- Private Methods - Utility ---

std::string ValidationService::buildTrustChainPath(const std::vector<TrustChainNode>& chain)
{
    if (chain.empty()) {
        return "";
    }

    std::string path;
    for (size_t i = 0; i < chain.size(); ++i) {
        if (i > 0) {
            path += " → ";
        }
        if (chain[i].isLinkCert) {
            path += "Link";
        } else if (chain[i].isSelfSigned) {
            path += "Root";
        } else {
            path += "DSC";
        }
    }

    return path;
}

std::string ValidationService::getCertificateFingerprint(X509* cert)
{
    if (!cert) {
        return "";
    }

    // Calculate SHA-256 fingerprint
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int mdLen = 0;

    if (X509_digest(cert, EVP_sha256(), md, &mdLen) != 1) {
        spdlog::error("Failed to calculate certificate fingerprint");
        return "";
    }

    // Convert to hex string
    std::ostringstream oss;
    for (unsigned int i = 0; i < mdLen; ++i) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(md[i]);
    }

    return oss.str();
}

std::string ValidationService::getSubjectDn(X509* cert)
{
    if (!cert) {
        return "";
    }

    char* dn = X509_NAME_oneline(X509_get_subject_name(cert), nullptr, 0);
    std::string result(dn);
    OPENSSL_free(dn);
    return result;
}

std::string ValidationService::getIssuerDn(X509* cert)
{
    if (!cert) {
        return "";
    }

    char* dn = X509_NAME_oneline(X509_get_issuer_name(cert), nullptr, 0);
    std::string result(dn);
    OPENSSL_free(dn);
    return result;
}

bool ValidationService::isSelfSigned(X509* cert)
{
    if (!cert) {
        return false;
    }

    std::string subject = getSubjectDn(cert);
    std::string issuer = getIssuerDn(cert);
    // Case-insensitive DN comparison (RFC 4517)
    return (strcasecmp(subject.c_str(), issuer.c_str()) == 0);
}

bool ValidationService::isLinkCertificate(X509* cert)
{
    if (!cert) {
        return false;
    }

    // Link certificates must NOT be self-signed
    if (isSelfSigned(cert)) {
        return false;
    }

    // Check BasicConstraints: CA:TRUE
    BASIC_CONSTRAINTS* bc = static_cast<BASIC_CONSTRAINTS*>(
        X509_get_ext_d2i(cert, NID_basic_constraints, nullptr, nullptr));
    if (!bc || !bc->ca) {
        if (bc) BASIC_CONSTRAINTS_free(bc);
        return false;
    }
    BASIC_CONSTRAINTS_free(bc);

    // Check KeyUsage: keyCertSign (bit 5)
    ASN1_BIT_STRING* usage = static_cast<ASN1_BIT_STRING*>(
        X509_get_ext_d2i(cert, NID_key_usage, nullptr, nullptr));
    if (!usage) {
        return false;
    }

    bool hasKeyCertSign = (ASN1_BIT_STRING_get_bit(usage, 5) == 1);
    ASN1_BIT_STRING_free(usage);

    return hasKeyCertSign;
}

std::string ValidationService::normalizeDnForComparison(const std::string& dn)
{
    if (dn.empty()) {
        return dn;
    }

    std::vector<std::string> parts;

    if (dn[0] == '/') {
        // OpenSSL slash-separated format: /C=Z/O=Y/CN=X
        std::istringstream stream(dn);
        std::string segment;
        while (std::getline(stream, segment, '/')) {
            if (!segment.empty()) {
                std::string lower;
                for (char c : segment) {
                    lower += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                size_t s = lower.find_first_not_of(" \t");
                if (s != std::string::npos) {
                    parts.push_back(lower.substr(s));
                }
            }
        }
    } else {
        // RFC 2253 comma-separated format: CN=X,O=Y,C=Z
        std::string current;
        bool inQuotes = false;
        for (size_t i = 0; i < dn.size(); i++) {
            char c = dn[i];
            if (c == '"') {
                inQuotes = !inQuotes;
                current += c;
            } else if (c == ',' && !inQuotes) {
                std::string lower;
                for (char ch : current) {
                    lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
                }
                size_t s = lower.find_first_not_of(" \t");
                if (s != std::string::npos) {
                    parts.push_back(lower.substr(s));
                }
                current.clear();
            } else if (c == '\\' && i + 1 < dn.size()) {
                current += c;
                current += dn[++i];
            } else {
                current += c;
            }
        }
        if (!current.empty()) {
            std::string lower;
            for (char ch : current) {
                lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            }
            size_t s = lower.find_first_not_of(" \t");
            if (s != std::string::npos) {
                parts.push_back(lower.substr(s));
            }
        }
    }

    // Sort components for order-independent comparison
    std::sort(parts.begin(), parts.end());

    // Join with pipe separator
    std::string result;
    for (size_t i = 0; i < parts.size(); i++) {
        if (i > 0) {
            result += "|";
        }
        result += parts[i];
    }
    return result;
}

std::string ValidationService::extractDnAttribute(const std::string& dn, const std::string& attr)
{
    std::string searchKey = attr + "=";
    // Lowercase DN for case-insensitive search
    std::string dnLower = dn;
    for (char& c : dnLower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    std::string keyLower = searchKey;
    for (char& c : keyLower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }

    size_t pos = 0;
    while ((pos = dnLower.find(keyLower, pos)) != std::string::npos) {
        // Verify it's at a boundary (start of string, after / or ,)
        if (pos == 0 || dnLower[pos-1] == '/' || dnLower[pos-1] == ',') {
            size_t valStart = pos + keyLower.size();
            size_t valEnd = dn.find_first_of("/,", valStart);
            if (valEnd == std::string::npos) {
                valEnd = dn.size();
            }
            std::string val = dn.substr(valStart, valEnd - valStart);
            // Trim and lowercase
            size_t s = val.find_first_not_of(" \t");
            size_t e = val.find_last_not_of(" \t");
            if (s != std::string::npos) {
                val = val.substr(s, e - s + 1);
                for (char& c : val) {
                    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                }
                return val;
            }
        }
        pos++;
    }
    return "";
}

} // namespace services
