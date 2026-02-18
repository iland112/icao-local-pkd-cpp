/**
 * @file ldif_processor.cpp
 * @brief LDIF file processor implementation
 *
 * Contains parseCertificateEntry() and parseCrlEntry() which are called
 * from LdifProcessor::processEntries(). These were moved from main.cpp
 * to reduce its size.
 */

#include "ldif_processor.h"
#include "common.h"
#include "common/masterlist_processor.h"
#include "common/main_utils.h"
#include "common/certificate_utils.h"
#include "common/x509_metadata_extractor.h"
#include "services/ldap_storage_service.h"
#include "domain/models/validation_result.h"
#include "repositories/validation_repository.h"
#include "repositories/certificate_repository.h"
#include "repositories/crl_repository.h"
#include "adapters/db_csca_provider.h"
#include <icao/validation/cert_ops.h>
#include <icao/validation/trust_chain_builder.h>
#include <spdlog/spdlog.h>
#include <libpq-fe.h>
#include <ldap.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/evp.h>
#include <openssl/cms.h>
#include <openssl/bn.h>
#include <openssl/bio.h>
#include <openssl/asn1.h>
#include <sstream>
#include <algorithm>
#include <optional>

// Progress manager (includes sendProgressWithMetadata, sendDbSavingProgress)
#include "common/progress_manager.h"

// For periodic DB progress updates during processing
#include "infrastructure/service_container.h"
#include "repositories/upload_repository.h"
extern infrastructure::ServiceContainer* g_services;

// --- Helper functions and types used by parseCertificateEntry / parseCrlEntry ---

namespace {

/**
 * @brief Case-insensitive string search
 * @param haystack String to search in
 * @param needle String to search for
 * @return true if needle is found in haystack (case-insensitive)
 */
bool containsIgnoreCase(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;

    std::string haystackLower = haystack;
    std::string needleLower = needle;

    std::transform(haystackLower.begin(), haystackLower.end(), haystackLower.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    std::transform(needleLower.begin(), needleLower.end(), needleLower.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    return haystackLower.find(needleLower) != std::string::npos;
}

struct CscaValidationResult {
    bool isValid;
    bool isSelfSigned;
    bool signatureValid;
    bool isCa;
    bool hasKeyCertSign;
    std::string errorMessage;
};

struct DscValidationResult {
    bool isValid;
    bool cscaFound;
    bool signatureValid;
    bool notExpired;
    bool notRevoked;
    bool dscExpired;
    bool cscaExpired;
    std::string cscaSubjectDn;
    std::string errorMessage;
    std::string trustChainPath;
};

CscaValidationResult validateCscaCertificate(X509* cert) {
    CscaValidationResult result = {false, false, false, false, false, ""};
    if (!cert) { result.errorMessage = "Certificate is null"; return result; }

    result.isSelfSigned = icao::validation::isSelfSigned(cert);
    if (!result.isSelfSigned) {
        result.errorMessage = "Certificate is not self-signed (Subject DN != Issuer DN)";
        return result;
    }

    result.signatureValid = icao::validation::verifyCertificateSignature(cert, cert);
    if (!result.signatureValid) {
        result.errorMessage = "Self-signature verification failed";
        return result;
    }

    BASIC_CONSTRAINTS* bc = static_cast<BASIC_CONSTRAINTS*>(
        X509_get_ext_d2i(cert, NID_basic_constraints, nullptr, nullptr));
    if (bc) { result.isCa = (bc->ca != 0); BASIC_CONSTRAINTS_free(bc); }

    ASN1_BIT_STRING* ku = static_cast<ASN1_BIT_STRING*>(
        X509_get_ext_d2i(cert, NID_key_usage, nullptr, nullptr));
    if (ku) { result.hasKeyCertSign = ASN1_BIT_STRING_get_bit(ku, 5); ASN1_BIT_STRING_free(ku); }

    if (result.isSelfSigned && result.signatureValid && result.isCa && result.hasKeyCertSign) {
        result.isValid = true;
    } else if (!result.isCa) {
        result.errorMessage = "Certificate does not have CA flag in Basic Constraints";
    } else if (!result.hasKeyCertSign) {
        result.errorMessage = "Certificate does not have keyCertSign in Key Usage";
    }
    return result;
}

DscValidationResult validateDscCertificate(X509* dscCert, const std::string& issuerDn) {
    DscValidationResult result = {false, false, false, false, false, false, false, "", "", ""};
    if (!dscCert) { result.errorMessage = "DSC certificate is null"; return result; }

    // Check DSC expiration (informational per ICAO hybrid model)
    result.dscExpired = icao::validation::isCertificateExpired(dscCert);
    result.notExpired = !result.dscExpired;
    if (icao::validation::isCertificateNotYetValid(dscCert)) {
        result.errorMessage = "DSC certificate is not yet valid";
        return result;
    }

    // Build and validate trust chain via icao::validation library
    adapters::DbCscaProvider provider(g_services->certificateRepository());
    icao::validation::TrustChainBuilder builder(&provider);
    auto chainResult = builder.build(dscCert);

    result.cscaFound = !chainResult.cscaSubjectDn.empty();
    result.cscaSubjectDn = chainResult.cscaSubjectDn.empty() ? issuerDn : chainResult.cscaSubjectDn;
    result.trustChainPath = chainResult.path;
    result.cscaExpired = chainResult.cscaExpired;
    result.signatureValid = chainResult.valid;
    result.isValid = chainResult.valid;

    if (!result.cscaFound && !chainResult.valid) {
        result.errorMessage = "No CSCA found for issuer: " + issuerDn.substr(0, 80);
    } else if (!chainResult.valid) {
        result.errorMessage = chainResult.message;
    }
    return result;
}

} // anonymous namespace

// --- Certificate and CRL parsing (moved from main.cpp) ---

/**
 * @brief Parse and save certificate from LDIF entry (DB + LDAP)
 */
bool parseCertificateEntry(LDAP* ld, const std::string& uploadId,
                           const LdifEntry& entry, const std::string& attrName,
                           int& cscaCount, int& dscCount, int& dscNcCount, int& ldapStoredCount,
                           ValidationStats& validationStats,
                           common::ValidationStatistics& enhancedStats) {
    std::string base64Value = entry.getFirstAttribute(attrName);
    if (base64Value.empty()) return false;

    spdlog::debug("parseCertificateEntry: base64Value len={}, first20chars={}",
                 base64Value.size(), base64Value.substr(0, 20));

    std::vector<uint8_t> derBytes = base64Decode(base64Value);
    if (derBytes.empty()) {
        common::addProcessingError(enhancedStats, "BASE64_DECODE_FAILED",
            entry.dn, "", "", "", "Base64 decode returned empty for attribute: " + attrName);
        return false;
    }

    spdlog::debug("parseCertificateEntry: derBytes size={}, first4bytes=0x{:02x}{:02x}{:02x}{:02x}",
                 derBytes.size(),
                 derBytes.size() > 0 ? derBytes[0] : 0,
                 derBytes.size() > 1 ? derBytes[1] : 0,
                 derBytes.size() > 2 ? derBytes[2] : 0,
                 derBytes.size() > 3 ? derBytes[3] : 0);

    const uint8_t* data = derBytes.data();
    X509* cert = d2i_X509(nullptr, &data, static_cast<long>(derBytes.size()));
    if (!cert) {
        spdlog::warn("Failed to parse certificate from entry: {}", entry.dn);
        common::addProcessingError(enhancedStats, "CERT_PARSE_FAILED",
            entry.dn, "", "", "", "Failed to parse X.509 certificate (d2i_X509 returned NULL)");
        return false;
    }

    std::string subjectDn = x509NameToString(X509_get_subject_name(cert));
    std::string issuerDn = x509NameToString(X509_get_issuer_name(cert));
    std::string serialNumber = asn1IntegerToHex(X509_get_serialNumber(cert));
    std::string notBefore = asn1TimeToIso8601(X509_get0_notBefore(cert));
    std::string notAfter = asn1TimeToIso8601(X509_get0_notAfter(cert));
    std::string fingerprint = computeFileHash(derBytes);
    std::string countryCode = extractCountryCode(subjectDn);
    if (countryCode == "XX") {
        countryCode = extractCountryCode(issuerDn);
    }

    // Extract comprehensive certificate metadata for progress tracking
    // Note: This extraction is done early (before validation) so metadata is available
    // for enhanced progress updates. ICAO compliance will be checked after cert type is determined.
    common::CertificateMetadata certMetadata = common::extractCertificateMetadataForProgress(cert, false);
    spdlog::debug("Extracted metadata for cert: type={}, sigAlg={}, keySize={}",
                  certMetadata.certificateType, certMetadata.signatureAlgorithm, certMetadata.keySize);

    // Determine certificate type and perform validation
    std::string certType;
    std::string validationStatus = "PENDING";
    std::string validationMessage = "";

    // Prepare validation result record
    domain::models::ValidationResult valRecord;
    valRecord.uploadId = uploadId;
    valRecord.fingerprint = fingerprint;
    valRecord.countryCode = countryCode;
    valRecord.subjectDn = subjectDn;
    valRecord.issuerDn = issuerDn;
    valRecord.serialNumber = serialNumber;
    valRecord.notBefore = notBefore;
    valRecord.notAfter = notAfter;

    auto startTime = std::chrono::high_resolution_clock::now();

    if (subjectDn == issuerDn) {
        // CSCA - self-signed certificate
        certType = "CSCA";
        cscaCount++;
        valRecord.certificateType = "CSCA";
        valRecord.isSelfSigned = true;

        // Validate CSCA self-signature
        auto cscaValidation = validateCscaCertificate(cert);
        valRecord.isCa = cscaValidation.isCa;
        valRecord.signatureVerified = cscaValidation.signatureValid;
        valRecord.validityCheckPassed = cscaValidation.isValid;  // isValid includes validity period check
        valRecord.keyUsageValid = cscaValidation.hasKeyCertSign;
        valRecord.trustChainValid = cscaValidation.signatureValid;  // Self-signed trust chain = signature valid

        if (cscaValidation.isValid) {
            validationStatus = "VALID";
            valRecord.validationStatus = "VALID";
            valRecord.trustChainMessage = "Self-signature verified";
            validationStats.validCount++;
            validationStats.trustChainValidCount++;
            spdlog::info("CSCA validation: VERIFIED - self-signature valid for {}", countryCode);
        } else if (cscaValidation.signatureValid) {
            validationStatus = "VALID";  // Signature valid but other issues
            validationMessage = cscaValidation.errorMessage;
            valRecord.validationStatus = "VALID";
            valRecord.trustChainMessage = cscaValidation.errorMessage;
            validationStats.validCount++;
            validationStats.trustChainValidCount++;
            spdlog::warn("CSCA validation: WARNING - {} for {}", cscaValidation.errorMessage, countryCode);
        } else {
            validationStatus = "INVALID";
            validationMessage = cscaValidation.errorMessage;
            valRecord.validationStatus = "INVALID";
            valRecord.trustChainValid = false;
            valRecord.trustChainMessage = cscaValidation.errorMessage;
            valRecord.errorMessage = cscaValidation.errorMessage;
            validationStats.invalidCount++;
            validationStats.trustChainInvalidCount++;
            spdlog::error("CSCA validation: FAILED - {} for {}", cscaValidation.errorMessage, countryCode);
        }
    } else if (containsIgnoreCase(entry.dn, "dc=nc-data")) {
        // Non-Conformant DSC - detected by dc=nc-data in LDIF DN path (case-insensitive)
        certType = "DSC_NC";
        dscNcCount++;
        valRecord.certificateType = "DSC_NC";
        spdlog::info("Detected DSC_NC certificate from nc-data path: dn={}", entry.dn);

        // DSC_NC - perform trust chain validation (ICAO hybrid model)
        auto dscValidation = validateDscCertificate(cert, issuerDn);
        valRecord.cscaFound = dscValidation.cscaFound;
        valRecord.cscaSubjectDn = dscValidation.cscaSubjectDn;
        valRecord.signatureVerified = dscValidation.signatureValid;
        valRecord.validityCheckPassed = dscValidation.notExpired;
        valRecord.isExpired = dscValidation.dscExpired;
        valRecord.trustChainPath = dscValidation.trustChainPath;

        if (dscValidation.isValid) {
            if (dscValidation.dscExpired || dscValidation.cscaExpired) {
                validationStatus = "EXPIRED_VALID";
                valRecord.validationStatus = "EXPIRED_VALID";
                valRecord.trustChainValid = true;
                valRecord.trustChainMessage = "Trust chain verified (certificates expired)";
                validationStats.validCount++;
                validationStats.trustChainValidCount++;
                if (dscValidation.dscExpired) validationStats.expiredCount++;
                spdlog::info("DSC_NC validation: Trust Chain VERIFIED (expired) for {} (issuer: {})",
                            countryCode, issuerDn.substr(0, 50));
            } else {
                validationStatus = "VALID";
                valRecord.validationStatus = "VALID";
                valRecord.trustChainValid = true;
                valRecord.trustChainMessage = "Trust chain verified: DSC signed by CSCA";
                validationStats.validCount++;
                validationStats.trustChainValidCount++;
                spdlog::info("DSC_NC validation: Trust Chain VERIFIED for {} (issuer: {})",
                            countryCode, issuerDn.substr(0, 50));
            }
        } else if (dscValidation.cscaFound) {
            validationStatus = "INVALID";
            validationMessage = dscValidation.errorMessage;
            valRecord.validationStatus = "INVALID";
            valRecord.trustChainValid = false;
            valRecord.trustChainMessage = dscValidation.errorMessage;
            valRecord.errorMessage = dscValidation.errorMessage;
            validationStats.invalidCount++;
            validationStats.trustChainInvalidCount++;
            spdlog::error("DSC_NC validation: Trust Chain FAILED - {} for {}",
                         dscValidation.errorMessage, countryCode);
        } else {
            validationStatus = "PENDING";
            validationMessage = dscValidation.errorMessage;
            valRecord.validationStatus = "PENDING";
            valRecord.trustChainMessage = "CSCA not found in database";
            valRecord.errorCode = "CSCA_NOT_FOUND";
            valRecord.errorMessage = dscValidation.errorMessage;
            validationStats.pendingCount++;
            validationStats.cscaNotFoundCount++;
            spdlog::warn("DSC_NC validation: CSCA not found - {} for {}",
                        dscValidation.errorMessage, countryCode);
        }
    } else {
        // Detect Link Certificates (subject != issuer, CA capability)
        // Check if this is a Link Certificate by validating CA status
        auto cscaValidation = validateCscaCertificate(cert);
        bool isLinkCertificate = (cscaValidation.isCa && cscaValidation.hasKeyCertSign);

        if (isLinkCertificate) {
            // Link Certificate - Cross-signed CSCA (subject != issuer)
            certType = "CSCA";  // Store as CSCA in DB for querying
            cscaCount++;
            valRecord.certificateType = "CSCA";
            valRecord.isSelfSigned = false;  // Link cert is not self-signed
            valRecord.isCa = cscaValidation.isCa;
            valRecord.signatureVerified = false;  // Cannot self-verify
            valRecord.validityCheckPassed = cscaValidation.isValid;
            valRecord.keyUsageValid = cscaValidation.hasKeyCertSign;

            // Link certificates need parent CSCA validation (ICAO hybrid model)
            auto lcValidation = validateDscCertificate(cert, issuerDn);
            valRecord.cscaFound = lcValidation.cscaFound;
            valRecord.cscaSubjectDn = lcValidation.cscaSubjectDn;
            valRecord.trustChainPath = lcValidation.trustChainPath;
            valRecord.isExpired = lcValidation.dscExpired;

            if (lcValidation.isValid) {
                if (lcValidation.dscExpired || lcValidation.cscaExpired) {
                    validationStatus = "EXPIRED_VALID";
                    valRecord.validationStatus = "EXPIRED_VALID";
                    valRecord.trustChainValid = true;
                    valRecord.trustChainMessage = "Trust chain verified (certificates expired)";
                    validationStats.validCount++;
                    validationStats.trustChainValidCount++;
                    spdlog::info("LC validation: Trust Chain VERIFIED (expired) for {} (issuer: {})",
                                countryCode, issuerDn.substr(0, 50));
                } else {
                    validationStatus = "VALID";
                    valRecord.validationStatus = "VALID";
                    valRecord.trustChainValid = true;
                    valRecord.trustChainMessage = "Trust chain verified: Link Certificate signed by CSCA";
                    validationStats.validCount++;
                    validationStats.trustChainValidCount++;
                    spdlog::info("LC validation: Trust Chain VERIFIED for {} (issuer: {})",
                                countryCode, issuerDn.substr(0, 50));
                }
            } else if (lcValidation.cscaFound) {
                validationStatus = "INVALID";
                validationMessage = lcValidation.errorMessage;
                valRecord.validationStatus = "INVALID";
                valRecord.trustChainValid = false;
                valRecord.trustChainMessage = lcValidation.errorMessage;
                valRecord.errorMessage = lcValidation.errorMessage;
                validationStats.invalidCount++;
                validationStats.trustChainInvalidCount++;
                spdlog::error("LC validation: Trust Chain FAILED - {} for {}",
                             lcValidation.errorMessage, countryCode);
            } else {
                validationStatus = "PENDING";
                validationMessage = lcValidation.errorMessage;
                valRecord.validationStatus = "PENDING";
                valRecord.trustChainMessage = "CSCA not found in database";
                valRecord.errorCode = "CSCA_NOT_FOUND";
                valRecord.errorMessage = lcValidation.errorMessage;
                validationStats.pendingCount++;
                validationStats.cscaNotFoundCount++;
                spdlog::warn("LC validation: CSCA not found - {} for {}",
                            lcValidation.errorMessage, countryCode);
            }
        } else {
            // Regular DSC
            certType = "DSC";
            dscCount++;
            valRecord.certificateType = "DSC";

        // DSC - perform trust chain validation
        // ICAO Doc 9303 Part 12 hybrid chain model: expiration is informational
        auto dscValidation = validateDscCertificate(cert, issuerDn);
        valRecord.cscaFound = dscValidation.cscaFound;
        valRecord.cscaSubjectDn = dscValidation.cscaSubjectDn;
        valRecord.signatureVerified = dscValidation.signatureValid;
        valRecord.validityCheckPassed = dscValidation.notExpired;
        valRecord.isExpired = dscValidation.dscExpired;
        valRecord.trustChainPath = dscValidation.trustChainPath;

        if (dscValidation.isValid) {
            // Determine status per ICAO Doc 9303 hybrid chain model
            if (dscValidation.dscExpired || dscValidation.cscaExpired) {
                validationStatus = "EXPIRED_VALID";
                valRecord.validationStatus = "EXPIRED_VALID";
                valRecord.trustChainValid = true;
                valRecord.trustChainMessage = "Trust chain verified (certificates expired)";
                validationStats.validCount++;
                validationStats.trustChainValidCount++;
                if (dscValidation.dscExpired) validationStats.expiredCount++;
                spdlog::info("DSC validation: Trust Chain VERIFIED (expired) for {} (issuer: {})",
                            countryCode, issuerDn.substr(0, 50));
            } else {
                validationStatus = "VALID";
                valRecord.validationStatus = "VALID";
                valRecord.trustChainValid = true;
                valRecord.trustChainMessage = "Trust chain verified: DSC signed by CSCA";
                validationStats.validCount++;
                validationStats.trustChainValidCount++;
                spdlog::info("DSC validation: Trust Chain VERIFIED for {} (issuer: {})",
                            countryCode, issuerDn.substr(0, 50));
            }
        } else if (dscValidation.cscaFound) {
            validationStatus = "INVALID";
            validationMessage = dscValidation.errorMessage;
            valRecord.validationStatus = "INVALID";
            valRecord.trustChainValid = false;
            valRecord.trustChainMessage = dscValidation.errorMessage;
            valRecord.errorMessage = dscValidation.errorMessage;
            validationStats.invalidCount++;
            validationStats.trustChainInvalidCount++;
            spdlog::error("DSC validation: Trust Chain FAILED - {} for {}",
                         dscValidation.errorMessage, countryCode);
        } else {
            validationStatus = "PENDING";
            validationMessage = dscValidation.errorMessage;
            valRecord.validationStatus = "PENDING";
            valRecord.trustChainMessage = "CSCA not found in database";
            valRecord.errorCode = "CSCA_NOT_FOUND";
            valRecord.errorMessage = dscValidation.errorMessage;
            validationStats.pendingCount++;
            validationStats.cscaNotFoundCount++;
            spdlog::warn("DSC validation: CSCA not found - {} for {}",
                        dscValidation.errorMessage, countryCode);
        }
        }  // End of else block for regular DSC
    }

    // Check ICAO 9303 compliance after certificate type is determined
    common::IcaoComplianceStatus icaoCompliance = common::checkIcaoCompliance(cert, certType);
    spdlog::debug("ICAO compliance for {} cert: isCompliant={}, level={}",
                  certType, icaoCompliance.isCompliant, icaoCompliance.complianceLevel);

    // Persist ICAO compliance details to validation record (saved to DB)
    valRecord.icaoCompliant = icaoCompliance.isCompliant;
    valRecord.icaoComplianceLevel = icaoCompliance.complianceLevel;
    valRecord.icaoKeyUsageCompliant = icaoCompliance.keyUsageCompliant;
    valRecord.icaoAlgorithmCompliant = icaoCompliance.algorithmCompliant;
    valRecord.icaoKeySizeCompliant = icaoCompliance.keySizeCompliant;
    valRecord.icaoValidityPeriodCompliant = icaoCompliance.validityPeriodCompliant;
    valRecord.icaoExtensionsCompliant = icaoCompliance.extensionsCompliant;
    {
        std::string violations;
        for (const auto& v : icaoCompliance.violations) {
            if (!violations.empty()) violations += "|";
            violations += v;
        }
        valRecord.icaoViolations = violations;
    }

    // Update enhanced statistics (ValidationStatistics)
    enhancedStats.totalCertificates++;
    enhancedStats.certificateTypes[certType]++;
    enhancedStats.signatureAlgorithms[certMetadata.signatureAlgorithm]++;
    enhancedStats.keySizes[certMetadata.keySize]++;

    // Update ICAO compliance counts
    if (icaoCompliance.isCompliant) {
        enhancedStats.icaoCompliantCount++;
    } else {
        enhancedStats.icaoNonCompliantCount++;
    }
    // Track per-category violation counts
    if (!icaoCompliance.keyUsageCompliant) enhancedStats.complianceViolations["keyUsage"]++;
    if (!icaoCompliance.algorithmCompliant) enhancedStats.complianceViolations["algorithm"]++;
    if (!icaoCompliance.keySizeCompliant) enhancedStats.complianceViolations["keySize"]++;
    if (!icaoCompliance.validityPeriodCompliant) enhancedStats.complianceViolations["validityPeriod"]++;
    if (!icaoCompliance.dnFormatCompliant) enhancedStats.complianceViolations["dnFormat"]++;
    if (!icaoCompliance.extensionsCompliant) enhancedStats.complianceViolations["extensions"]++;

    // Update validation status counts and reason tracking
    if (validationStatus == "VALID") {
        enhancedStats.validCount++;
        enhancedStats.validationReasons["VALID"]++;
    } else if (validationStatus == "EXPIRED_VALID") {
        enhancedStats.expiredValidCount++;
        enhancedStats.validationReasons["EXPIRED_VALID: " + valRecord.trustChainMessage]++;
    } else if (validationStatus == "INVALID") {
        enhancedStats.invalidCount++;
        enhancedStats.validationReasons["INVALID: " + valRecord.trustChainMessage]++;
    } else if (validationStatus == "PENDING") {
        enhancedStats.pendingCount++;
        enhancedStats.validationReasons["PENDING: " + valRecord.trustChainMessage]++;
    }

    // Update trust chain counters on enhancedStats (SSE-streamed)
    if (valRecord.trustChainValid) {
        enhancedStats.trustChainValidCount++;
    } else if (validationStatus == "INVALID") {
        enhancedStats.trustChainInvalidCount++;
    }
    if (validationStatus == "PENDING" && valRecord.errorCode == "CSCA_NOT_FOUND") {
        enhancedStats.cscaNotFoundCount++;
    }

    // Update expiration status counters on enhancedStats
    if (valRecord.isExpired) {
        enhancedStats.expiredCount++;
    } else if (validationStatus == "VALID" || validationStatus == "EXPIRED_VALID") {
        enhancedStats.validPeriodCount++;
    }

    // Per-certificate validation log for real-time EventLog display
    common::addValidationLog(
        enhancedStats,
        certType, countryCode, subjectDn, issuerDn,
        validationStatus,
        valRecord.trustChainMessage,
        valRecord.trustChainPath,
        valRecord.errorCode,
        fingerprint
    );

    spdlog::debug("Updated statistics - total={}, type={}, sigAlg={}, keySize={}, icaoCompliant={}",
                  enhancedStats.totalCertificates, certType, certMetadata.signatureAlgorithm,
                  certMetadata.keySize, icaoCompliance.isCompliant);
    // Note: This requires passing ValidationStatistics as a parameter to this function
    // For now, we log the metadata and compliance for verification
    // Statistics will be updated once the parameter is added to function signature

    auto endTime = std::chrono::high_resolution_clock::now();
    valRecord.validationDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

    X509_free(cert);

    // 1. Save to DB with validation status
    auto [certId, isDuplicate] = certificate_utils::saveCertificateWithDuplicateCheck(
        uploadId, certType, countryCode,
        subjectDn, issuerDn, serialNumber, fingerprint,
        notBefore, notAfter, derBytes,
        validationStatus, validationMessage
    );

    if (isDuplicate) {
        enhancedStats.duplicateCount++;
    }

    if (!certId.empty()) {
        spdlog::debug("Saved certificate to DB: type={}, country={}, fingerprint={}",
                     certType, countryCode, fingerprint.substr(0, 16));

        // 3. Save validation result via ValidationRepository
        valRecord.certificateId = certId;
        g_services->validationRepository()->save(valRecord);

        // 4. Save to LDAP
        if (ld) {
            // Extract DSC_NC specific attributes from LDIF entry
            std::string pkdConformanceCode = entry.getFirstAttribute("pkdConformanceCode");
            std::string pkdConformanceText = entry.getFirstAttribute("pkdConformanceText");
            std::string pkdVersion = entry.getFirstAttribute("pkdVersion");

            // Use "LC" for LDAP storage of Link Certificates
            // DB stores as "CSCA" for querying, but LDAP uses "LC" for proper organizational unit
            std::string ldapCertType = certType;
            if (certType == "CSCA" && !valRecord.isSelfSigned) {
                ldapCertType = "LC";  // Link Certificate (subject != issuer)
                spdlog::debug("Using LDAP cert type 'LC' for link certificate: {}", fingerprint.substr(0, 16));
            }

            std::string ldapDn = g_services->ldapStorageService()->saveCertificateToLdap(ld, ldapCertType, countryCode,
                                                        subjectDn, issuerDn, serialNumber,
                                                        fingerprint, derBytes,
                                                        pkdConformanceCode, pkdConformanceText, pkdVersion);
            if (!ldapDn.empty()) {
                // Use Repository method instead of standalone function
                g_services->certificateRepository()->updateCertificateLdapStatus(certId, ldapDn);
                ldapStoredCount++;
                spdlog::debug("Saved certificate to LDAP: {}", ldapDn);
            } else {
                common::addProcessingError(enhancedStats, "LDAP_SAVE_FAILED",
                    entry.dn, subjectDn, countryCode, certType,
                    "LDAP save returned empty DN for fingerprint: " + fingerprint.substr(0, 16));
            }
        }
    } else if (!isDuplicate) {
        common::addProcessingError(enhancedStats, "DB_SAVE_FAILED",
            entry.dn, subjectDn, countryCode, certType,
            "Database save returned empty ID");
    }

    return !certId.empty();
}

/**
 * @brief Parse and save CRL from LDIF entry (DB + LDAP)
 */
bool parseCrlEntry(LDAP* ld, const std::string& uploadId,
                   const LdifEntry& entry, int& crlCount, int& ldapCrlStoredCount,
                   common::ValidationStatistics& enhancedStats) {
    std::string base64Value = entry.getFirstAttribute("certificateRevocationList;binary");
    if (base64Value.empty()) return false;

    std::vector<uint8_t> derBytes = base64Decode(base64Value);
    if (derBytes.empty()) {
        common::addProcessingError(enhancedStats, "BASE64_DECODE_FAILED",
            entry.dn, "", "", "CRL", "Base64 decode failed for CRL");
        return false;
    }

    const uint8_t* data = derBytes.data();
    X509_CRL* crl = d2i_X509_CRL(nullptr, &data, static_cast<long>(derBytes.size()));
    if (!crl) {
        spdlog::warn("Failed to parse CRL from entry: {}", entry.dn);
        common::addProcessingError(enhancedStats, "CRL_PARSE_FAILED",
            entry.dn, "", "", "CRL", "Failed to parse CRL (d2i_X509_CRL returned NULL)");
        return false;
    }

    std::string issuerDn = x509NameToString(X509_CRL_get_issuer(crl));
    std::string thisUpdate = asn1TimeToIso8601(X509_CRL_get0_lastUpdate(crl));
    std::string nextUpdate;
    if (X509_CRL_get0_nextUpdate(crl)) {
        nextUpdate = asn1TimeToIso8601(X509_CRL_get0_nextUpdate(crl));
    }

    std::string crlNumber;
    ASN1_INTEGER* crlNumAsn1 = static_cast<ASN1_INTEGER*>(
        X509_CRL_get_ext_d2i(crl, NID_crl_number, nullptr, nullptr));
    if (crlNumAsn1) {
        crlNumber = asn1IntegerToHex(crlNumAsn1);
        ASN1_INTEGER_free(crlNumAsn1);
    }

    std::string fingerprint = computeFileHash(derBytes);
    std::string countryCode = extractCountryCode(issuerDn);

    // 1. Save to DB via CrlRepository
    std::string crlId = g_services->crlRepository()->save(uploadId, countryCode, issuerDn,
                                               thisUpdate, nextUpdate, crlNumber, fingerprint, derBytes);

    if (!crlId.empty()) {
        crlCount++;

        // Save revoked certificates to DB
        STACK_OF(X509_REVOKED)* revokedStack = X509_CRL_get_REVOKED(crl);
        if (revokedStack) {
            int revokedCount = sk_X509_REVOKED_num(revokedStack);
            for (int i = 0; i < revokedCount; i++) {
                X509_REVOKED* revoked = sk_X509_REVOKED_value(revokedStack, i);
                if (revoked) {
                    std::string serialNum = asn1IntegerToHex(X509_REVOKED_get0_serialNumber(revoked));
                    std::string revDate = asn1TimeToIso8601(X509_REVOKED_get0_revocationDate(revoked));
                    std::string reason = "unspecified";

                    ASN1_ENUMERATED* reasonEnum = static_cast<ASN1_ENUMERATED*>(
                        X509_REVOKED_get_ext_d2i(revoked, NID_crl_reason, nullptr, nullptr));
                    if (reasonEnum) {
                        long reasonCode = ASN1_ENUMERATED_get(reasonEnum);
                        switch (reasonCode) {
                            case 1: reason = "keyCompromise"; break;
                            case 2: reason = "cACompromise"; break;
                            case 3: reason = "affiliationChanged"; break;
                            case 4: reason = "superseded"; break;
                            case 5: reason = "cessationOfOperation"; break;
                            case 6: reason = "certificateHold"; break;
                        }
                        ASN1_ENUMERATED_free(reasonEnum);
                    }

                    g_services->crlRepository()->saveRevokedCertificate(crlId, serialNum, revDate, reason);
                }
            }
            spdlog::debug("Saved CRL to DB with {} revoked certificates, issuer={}",
                         revokedCount, issuerDn.substr(0, 50));
        }

        // 2. Save to LDAP
        if (ld) {
            std::string ldapDn = g_services->ldapStorageService()->saveCrlToLdap(ld, countryCode, issuerDn, fingerprint, derBytes);
            if (!ldapDn.empty()) {
                g_services->crlRepository()->updateLdapStatus(crlId, ldapDn);
                ldapCrlStoredCount++;
                spdlog::debug("Saved CRL to LDAP: {}", ldapDn);
            } else {
                common::addProcessingError(enhancedStats, "LDAP_SAVE_FAILED",
                    entry.dn, issuerDn, countryCode, "CRL",
                    "CRL LDAP save returned empty DN for fingerprint: " + fingerprint.substr(0, 16));
            }
        }
    } else {
        common::addProcessingError(enhancedStats, "DB_SAVE_FAILED",
            entry.dn, issuerDn, countryCode, "CRL",
            "CRL database save returned empty ID");
    }

    X509_CRL_free(crl);
    return !crlId.empty();
}

// --- LdifProcessor methods ---

std::vector<LdifEntry> LdifProcessor::parseLdifContent(const std::string& content) {
    std::vector<LdifEntry> entries;
    LdifEntry currentEntry;
    std::string currentAttrName;
    std::string currentAttrValue;
    bool inContinuation = false;

    std::istringstream stream(content);
    std::string line;

    auto finalizeAttribute = [&]() {
        if (!currentAttrName.empty()) {
            currentEntry.attributes[currentAttrName].push_back(currentAttrValue);
            currentAttrName.clear();
            currentAttrValue.clear();
        }
    };

    auto finalizeEntry = [&]() {
        finalizeAttribute();
        if (!currentEntry.dn.empty()) {
            entries.push_back(std::move(currentEntry));
            currentEntry = LdifEntry();
        }
    };

    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            finalizeEntry();
            inContinuation = false;
            continue;
        }

        if (line[0] == '#') continue;

        if (line[0] == ' ') {
            // LDIF continuation line - append to current value
            if (inContinuation) {
                if (currentAttrName == "dn") {
                    // DN continuation - append to entry.dn
                    currentEntry.dn += line.substr(1);
                } else {
                    currentAttrValue += line.substr(1);
                }
            }
            continue;
        }

        finalizeAttribute();
        inContinuation = false;

        size_t colonPos = line.find(':');
        if (colonPos == std::string::npos) continue;

        currentAttrName = line.substr(0, colonPos);

        if (colonPos + 1 < line.size() && line[colonPos + 1] == ':') {
            // Base64 encoded value (double colon ::)
            // Only add ;binary suffix if not already present
            if (currentAttrName.find(";binary") == std::string::npos) {
                currentAttrName += ";binary";
            }
            size_t valueStart = colonPos + 2;
            while (valueStart < line.size() && line[valueStart] == ' ') valueStart++;
            currentAttrValue = line.substr(valueStart);
        } else {
            size_t valueStart = colonPos + 1;
            while (valueStart < line.size() && line[valueStart] == ' ') valueStart++;
            currentAttrValue = line.substr(valueStart);
        }

        if (currentAttrName == "dn") {
            currentEntry.dn = currentAttrValue;
            // Keep currentAttrName as "dn" for continuation line handling
            inContinuation = true;
        } else {
            inContinuation = true;
        }
    }

    finalizeEntry();
    return entries;
}

LdifProcessor::ProcessingCounts LdifProcessor::processEntries(
    const std::string& uploadId,
    const std::vector<LdifEntry>& entries,
    LDAP* ld,
    ValidationStats& stats,
    common::ValidationStatistics& enhancedStats,
    const TotalCounts* totalCounts
) {
    ProcessingCounts counts;
    int processedEntries = 0;
    int totalEntries = static_cast<int>(entries.size());

    spdlog::info("Processing {} LDIF entries for upload {}", totalEntries, uploadId);

    // Process each entry
    for (const auto& entry : entries) {
        try {
            // Check for userCertificate;binary
            if (entry.hasAttribute("userCertificate;binary")) {
                parseCertificateEntry(ld, uploadId, entry, "userCertificate;binary",
                                    counts.cscaCount, counts.dscCount, counts.dscNcCount,
                                    counts.ldapCertStoredCount, stats, enhancedStats);
            }
            // Check for cACertificate;binary
            else if (entry.hasAttribute("cACertificate;binary")) {
                parseCertificateEntry(ld, uploadId, entry, "cACertificate;binary",
                                    counts.cscaCount, counts.dscCount, counts.dscNcCount,
                                    counts.ldapCertStoredCount, stats, enhancedStats);
            }

            // Check for CRL
            if (entry.hasAttribute("certificateRevocationList;binary")) {
                parseCrlEntry(ld, uploadId, entry, counts.crlCount, counts.ldapCrlStoredCount, enhancedStats);
            }

            // Check for Master List
            if (entry.hasAttribute("pkdMasterListContent;binary") ||
                entry.hasAttribute("pkdMasterListContent")) {
                MasterListStats mlStats;
                parseMasterListEntryV2(ld, uploadId, entry, mlStats, &enhancedStats);
                // Track Master List file count
                counts.mlCount++;
                // Track MLSC count
                counts.mlscCount += mlStats.mlscCount;
                counts.ldapMlStoredCount += mlStats.ldapMlStoredCount;
                // Add extracted CSCAs to counts
                counts.cscaCount += mlStats.cscaNewCount;
                counts.ldapCertStoredCount += mlStats.ldapCscaStoredCount;
            }

        } catch (const std::exception& e) {
            spdlog::warn("Error processing entry {}: {}", entry.dn, e.what());
            common::addProcessingError(enhancedStats, "ENTRY_PROCESSING_EXCEPTION",
                entry.dn, "", "", "", std::string("Exception: ") + e.what());
        }

        processedEntries++;

        // Update DB progress every 500 entries (for upload history/detail page)
        if (g_services->uploadRepository() && (processedEntries % 500 == 0 || processedEntries == totalEntries)) {
            g_services->uploadRepository()->updateProgress(uploadId, totalEntries, processedEntries);
            g_services->uploadRepository()->updateStatistics(uploadId,
                counts.cscaCount, counts.dscCount, counts.dscNcCount, counts.crlCount,
                counts.mlscCount, counts.mlCount);
        }

        // Send progress update to frontend every 50 entries
        if (processedEntries % 50 == 0 || processedEntries == totalEntries) {
            // Build detailed progress message with X/Total format if totalCounts provided
            std::string progressMsg = "처리 중: ";
            std::vector<std::string> parts;

            int totalCerts = totalCounts ? totalCounts->totalCerts : 0;
            int totalCrl = totalCounts ? totalCounts->totalCrl : 0;
            int totalMl = totalCounts ? totalCounts->totalMl : 0;

            // Show individual cert types (CSCA/DSC/DSC_NC) separately
            // Only display items with count > 0
            if (counts.cscaCount > 0) {
                if (totalCerts > 0) {
                    parts.push_back("CSCA " + std::to_string(counts.cscaCount) + "/" + std::to_string(totalCerts));
                } else {
                    parts.push_back("CSCA " + std::to_string(counts.cscaCount));
                }
            }

            if (counts.dscCount > 0) {
                if (totalCerts > 0) {
                    parts.push_back("DSC " + std::to_string(counts.dscCount) + "/" + std::to_string(totalCerts));
                } else {
                    parts.push_back("DSC " + std::to_string(counts.dscCount));
                }
            }

            if (counts.dscNcCount > 0) {
                if (totalCerts > 0) {
                    parts.push_back("DSC_NC " + std::to_string(counts.dscNcCount) + "/" + std::to_string(totalCerts));
                } else {
                    parts.push_back("DSC_NC " + std::to_string(counts.dscNcCount));
                }
            }

            if (counts.crlCount > 0) {
                if (totalCrl > 0) {
                    parts.push_back("CRL " + std::to_string(counts.crlCount) + "/" + std::to_string(totalCrl));
                } else {
                    parts.push_back("CRL " + std::to_string(counts.crlCount));
                }
            }

            if (counts.mlCount > 0) {
                if (totalMl > 0) {
                    parts.push_back("ML " + std::to_string(counts.mlCount) + "/" + std::to_string(totalMl));
                } else {
                    parts.push_back("ML " + std::to_string(counts.mlCount));
                }
            }

            for (size_t i = 0; i < parts.size(); ++i) {
                if (i > 0) progressMsg += ", ";
                progressMsg += parts[i];
            }

            // Update processed count in statistics
            enhancedStats.processedCount = counts.cscaCount + counts.dscCount + counts.dscNcCount;

            // Send enhanced progress with validation statistics via SSE
            common::sendProgressWithMetadata(
                uploadId,
                common::ProcessingStage::VALIDATION_IN_PROGRESS,
                processedEntries,
                totalEntries,
                progressMsg,
                std::nullopt,  // No current certificate metadata (batch update)
                std::nullopt,  // No current compliance status (batch update)
                enhancedStats  // Include accumulated validation statistics
            );

            spdlog::info("Processing progress: {}/{} entries, {} certs ({} LDAP), {} CRLs ({} LDAP), {} MLs ({} LDAP)",
                        processedEntries, totalEntries,
                        counts.cscaCount + counts.dscCount, counts.ldapCertStoredCount,
                        counts.crlCount, counts.ldapCrlStoredCount,
                        counts.mlCount, counts.ldapMlStoredCount);
        }
    }

    spdlog::info("LDIF processing completed: {} CSCA, {} DSC, {} DSC_NC, {} CRLs, {} MLs",
                counts.cscaCount, counts.dscCount, counts.dscNcCount, counts.crlCount, counts.mlCount);

    // Send final progress with complete validation statistics
    enhancedStats.processedCount = counts.cscaCount + counts.dscCount + counts.dscNcCount;
    common::sendProgressWithMetadata(
        uploadId,
        common::ProcessingStage::VALIDATION_COMPLETED,
        totalEntries,
        totalEntries,
        "검증 완료: " + std::to_string(enhancedStats.processedCount) + "개 인증서 처리됨",
        std::nullopt,  // No current certificate
        std::nullopt,  // No current compliance
        enhancedStats  // Final validation statistics
    );

    return counts;
}

int LdifProcessor::uploadToLdap(
    const std::string& uploadId,
    LDAP* ld
) {
    if (!ld) {
        spdlog::warn("LDAP connection not available for upload {}", uploadId);
        return 0;
    }

    spdlog::info("Uploading certificates from DB to LDAP for upload {}", uploadId);

    // TODO: Replace with CertificateRepository::findNotStoredInLdapByUploadId()
    // For now, this is a stub implementation
    // The actual LDAP upload logic needs:
    // 1. certificateRepository->findNotStoredInLdapByUploadId(uploadId)
    // 2. For each certificate: saveCertificateToLdap() from main.cpp
    // 3. certificateRepository->updateCertificateLdapStatus(certificateId, ldapDn)

    int uploadedCount = 0;

    spdlog::warn("uploadToLdap stub: Needs CertificateRepository::findNotStoredInLdapByUploadId() method");

    return uploadedCount;
}
