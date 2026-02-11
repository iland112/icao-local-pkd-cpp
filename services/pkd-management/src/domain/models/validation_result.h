#pragma once

#include <string>

/**
 * @file validation_result.h
 * @brief Domain Model for Certificate Validation Result
 *
 * @note Moved from main.cpp as part of Phase 4.4 refactoring
 * @date 2026-01-30
 */

namespace domain {
namespace models {

/**
 * @brief Certificate validation result record
 *
 * This struct represents the complete validation result for a certificate,
 * including trust chain validation, signature verification, validity period
 * checks, and CRL verification status.
 */
struct ValidationResult {
    // Certificate identification
    std::string certificateId;       // UUID of certificate in database
    std::string uploadId;            // UUID of upload batch
    std::string certificateType;     // DSC, DSC_NC, CSCA, MLSC
    std::string countryCode;         // 2-letter country code
    std::string subjectDn;           // Certificate subject DN
    std::string issuerDn;            // Certificate issuer DN
    std::string serialNumber;        // Certificate serial number

    // Overall validation result
    std::string validationStatus;    // VALID, INVALID, PENDING, ERROR

    // Trust chain validation
    bool trustChainValid = false;    // Whether trust chain is valid
    std::string trustChainMessage;   // Detailed message about trust chain
    std::string trustChainPath;      // Human-readable chain path (e.g., "DSC → CN=CSCA")
    bool cscaFound = false;          // Whether CSCA was found
    std::string cscaSubjectDn;       // CSCA subject DN if found
    std::string cscaFingerprint;     // CSCA fingerprint if found

    // Signature verification
    bool signatureVerified = false;  // Whether signature is valid
    std::string signatureAlgorithm;  // Signature algorithm used

    // Validity period checks
    bool validityCheckPassed = false; // Whether validity period is current
    bool isExpired = false;           // Whether certificate is expired
    bool isNotYetValid = false;       // Whether certificate is not yet valid
    std::string notBefore;            // Validity start date
    std::string notAfter;             // Validity end date

    // CSCA-specific fields (for CA certificates)
    bool isCa = false;                // Whether this is a CA certificate
    bool isSelfSigned = false;        // Whether this is self-signed
    int pathLengthConstraint = -1;    // Path length constraint (-1 if not present)

    // Key usage validation
    bool keyUsageValid = false;       // Whether key usage is appropriate
    std::string keyUsageFlags;        // Key usage flags as string

    // CRL (Certificate Revocation List) check
    std::string crlCheckStatus = "NOT_CHECKED"; // NOT_CHECKED, REVOKED, NOT_REVOKED, ERROR
    std::string crlCheckMessage;                 // Detailed CRL check message

    // Error information
    std::string errorCode;            // Error code if validation failed
    std::string errorMessage;         // Error message if validation failed

    // Fingerprint (needed for validation_result table)
    std::string fingerprint;          // SHA-256 fingerprint of the certificate

    // Performance metrics
    int validationDurationMs = 0;     // Time taken for validation in milliseconds
};

} // namespace models
} // namespace domain
