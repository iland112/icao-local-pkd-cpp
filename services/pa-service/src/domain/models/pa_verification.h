/**
 * @file pa_verification.h
 * @brief Domain model for PA verification records
 *
 * Represents a Passive Authentication verification record stored in database.
 * This is a plain Data Transfer Object (DTO) used for passing data between
 * Repository and Service layers.
 *
 * @author SmartCore Inc.
 * @date 2026-02-01
 */

#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <chrono>
#include <optional>
#include <json/json.h>

namespace domain {
namespace models {

/**
 * @brief PA verification record domain model
 *
 * Represents a single PA verification attempt with all validation results.
 * Stored in pa_verification table in PostgreSQL.
 */
struct PaVerification {
    // Primary identification
    std::string id;  // UUID

    // Document information
    std::string documentNumber;
    std::string countryCode;  // ISO 3166-1 alpha-2 (e.g., "KR", "US")

    // Verification status
    std::string verificationStatus;  // "VALID", "INVALID", "ERROR"

    // SOD information
    std::string sodHash;  // SHA-256 hash of SOD
    std::vector<uint8_t> sodBinary;  // Raw SOD binary data

    // DSC certificate information
    std::string dscSubject;
    std::string dscSerialNumber;
    std::string dscIssuer;
    std::optional<std::string> dscNotBefore;
    std::optional<std::string> dscNotAfter;
    bool dscExpired = false;

    // CSCA certificate information
    std::string cscaSubject;
    std::string cscaSerialNumber;
    std::optional<std::string> cscaNotBefore;
    std::optional<std::string> cscaNotAfter;
    bool cscaExpired = false;

    // Validation results
    bool certificateChainValid = false;
    bool sodSignatureValid = false;
    bool dataGroupsValid = false;

    // CRL checking
    bool crlChecked = false;
    bool revoked = false;
    std::string crlStatus;  // "VALID", "REVOKED", "CRL_UNAVAILABLE", etc.
    std::optional<std::string> crlMessage;

    // Additional validation details
    std::optional<std::string> validationErrors;
    std::string expirationStatus;  // "VALID", "WARNING", "EXPIRED"
    std::optional<std::string> expirationMessage;

    // Metadata (JSON)
    std::optional<Json::Value> metadata;

    // Timestamps
    std::string createdAt;  // ISO 8601 format
    std::optional<std::string> updatedAt;

    // Client information (audit)
    std::optional<std::string> ipAddress;
    std::optional<std::string> userAgent;

    /**
     * @brief Convert to JSON for API response
     * @return Json::Value representation
     */
    Json::Value toJson() const;

    /**
     * @brief Create from JSON (e.g., from API request)
     * @param json Input JSON object
     * @return PaVerification instance
     */
    static PaVerification fromJson(const Json::Value& json);

    /**
     * @brief Validate required fields
     * @return true if all required fields are present
     */
    bool isValid() const;
};

} // namespace models
} // namespace domain
