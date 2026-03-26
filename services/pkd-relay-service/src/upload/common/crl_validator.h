/**
 * @file crl_validator.h
 * @brief CRL-based certificate revocation checking
 *
 * Implements RFC 5280 CRL validation for CSCA/DSC/LC certificates
 */

#pragma once

#include <string>
#include <optional>
#include <chrono>
#include "i_query_executor.h"
#include <openssl/x509.h>
#include <openssl/x509v3.h>

namespace crl {

/**
 * @brief CRL revocation status enumeration
 */
enum class RevocationStatus {
    GOOD,       ///< Certificate is not revoked
    REVOKED,    ///< Certificate is revoked
    UNKNOWN     ///< CRL not available or check failed
};

/**
 * @brief CRL revocation reason codes (RFC 5280 Section 5.3.1)
 */
enum class RevocationReason {
    UNSPECIFIED = 0,
    KEY_COMPROMISE = 1,
    CA_COMPROMISE = 2,
    AFFILIATION_CHANGED = 3,
    SUPERSEDED = 4,
    CESSATION_OF_OPERATION = 5,
    CERTIFICATE_HOLD = 6,
    REMOVE_FROM_CRL = 8,
    PRIVILEGE_WITHDRAWN = 9,
    AA_COMPROMISE = 10,
    UNKNOWN = -1
};

/**
 * @brief Result of CRL revocation check
 */
struct RevocationCheckResult {
    RevocationStatus status;
    std::optional<RevocationReason> reason;
    std::optional<std::string> revocationDate;  ///< ISO 8601 format
    std::string crlIssuerDn;
    std::string crlThisUpdate;  ///< CRL effective date
    std::string crlNextUpdate;  ///< CRL expiry date
    int checkDurationMs;
    std::string message;
};

/**
 * @brief CRL Validator class
 *
 * Provides CRL-based certificate revocation checking following RFC 5280.
 * Queries database for latest CRL, parses binary, and checks serial number.
 * Supports PostgreSQL and Oracle via IQueryExecutor abstraction.
 */
class CrlValidator {
public:
    /**
     * @brief Construct CRL validator
     * @param executor Query executor for database operations (must be valid)
     */
    explicit CrlValidator(common::IQueryExecutor* executor);

    /**
     * @brief Destructor
     */
    ~CrlValidator() = default;

    /**
     * @brief Check if certificate is revoked via CRL
     *
     * Process:
     * 1. Find latest CRL for certificate's issuer DN
     * 2. Parse CRL binary (d2i_X509_CRL)
     * 3. Search for certificate serial in revoked list
     * 4. Extract revocation reason and date if found
     * 5. Log result to crl_revocation_log table
     *
     * @param certificateId UUID of certificate in database
     * @param certificateType Type: CSCA, DSC, DSC_NC, LC
     * @param serialNumber Certificate serial number (hex string)
     * @param fingerprint SHA-256 fingerprint (hex string)
     * @param issuerDn Issuer DN of certificate
     * @return RevocationCheckResult with status and metadata
     *
     * @note Returns UNKNOWN if CRL not found or parsing fails
     * @note Returns GOOD if CRL exists but certificate not in revoked list
     * @note Returns REVOKED if certificate found in revoked list
     */
    RevocationCheckResult checkRevocation(
        const std::string& certificateId,
        const std::string& certificateType,
        const std::string& serialNumber,
        const std::string& fingerprint,
        const std::string& issuerDn
    );

    /**
     * @brief Check if CRL is expired
     *
     * CRL is expired if nextUpdate < NOW()
     *
     * @param issuerDn Issuer DN to find CRL
     * @return true if CRL expired or not found, false if valid
     */
    bool isCrlExpired(const std::string& issuerDn);

    /**
     * @brief Get latest CRL metadata for issuer
     *
     * @param issuerDn Issuer DN
     * @return Optional tuple: {thisUpdate, nextUpdate, crlId}
     */
    std::optional<std::tuple<std::string, std::string, std::string>>
    getLatestCrlMetadata(const std::string& issuerDn);

private:
    common::IQueryExecutor* executor_;  ///< Query executor (non-owning)

    /**
     * @brief Convert RevocationReason enum to string
     * @param reason Revocation reason code
     * @return Human-readable reason string
     */
    static std::string revocationReasonToString(RevocationReason reason);

    /**
     * @brief Convert OpenSSL CRL reason code to enum
     * @param opensslReason OpenSSL reason code (0-10)
     * @return RevocationReason enum
     */
    static RevocationReason opensslReasonToEnum(int opensslReason);

    /**
     * @brief Convert ASN1_TIME to ISO 8601 string
     * @param asn1Time OpenSSL ASN1_TIME object
     * @return ISO 8601 formatted string (YYYY-MM-DDTHH:MM:SSZ)
     */
    static std::string asn1TimeToString(const ASN1_TIME* asn1Time);

    /**
     * @brief Convert hex serial number string to ASN1_INTEGER
     * @param serialHex Hex string (e.g., "1A2B3C")
     * @return ASN1_INTEGER object (caller must free with ASN1_INTEGER_free)
     */
    static ASN1_INTEGER* hexSerialToAsn1(const std::string& serialHex);

    /**
     * @brief Log revocation check result to database
     *
     * Inserts record into crl_revocation_log table.
     *
     * @param result Revocation check result
     * @param certificateId UUID of certificate
     * @param certificateType Type: CSCA, DSC, DSC_NC, LC
     * @param serialNumber Certificate serial number
     * @param fingerprint SHA-256 fingerprint
     * @param subjectDn Certificate subject DN (optional)
     * @param crlId CRL UUID (optional)
     */
    void logRevocationCheck(
        const RevocationCheckResult& result,
        const std::string& certificateId,
        const std::string& certificateType,
        const std::string& serialNumber,
        const std::string& fingerprint,
        const std::string& subjectDn = "",
        const std::string& crlId = ""
    );
};

/**
 * @brief Convert RevocationStatus enum to string
 * @param status Revocation status
 * @return String: "GOOD", "REVOKED", or "UNKNOWN"
 */
inline std::string revocationStatusToString(RevocationStatus status) {
    switch (status) {
        case RevocationStatus::GOOD: return "GOOD";
        case RevocationStatus::REVOKED: return "REVOKED";
        case RevocationStatus::UNKNOWN: return "UNKNOWN";
        default: return "UNKNOWN";
    }
}

} // namespace crl
