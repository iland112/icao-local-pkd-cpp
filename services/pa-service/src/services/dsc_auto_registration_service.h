/**
 * @file dsc_auto_registration_service.h
 * @brief DSC Pending Registration from PA Verification
 *
 * When PA verification extracts a DSC from SOD that is not yet
 * registered in the local PKD, this service saves it to the
 * pending_dsc_registration table for admin review and approval.
 *
 * @date 2026-02-12
 * @modified 2026-03-10 — Changed from auto-registration to pending approval workflow
 */

#pragma once

#include <string>
#include <vector>
#include <openssl/x509.h>
#include "i_query_executor.h"

namespace services {

/**
 * @brief Result of DSC pending registration attempt
 */
struct DscRegistrationResult {
    bool success = false;             // Operation completed without error
    bool newlyRegistered = false;     // true = new pending entry, false = already existed
    bool pendingApproval = false;     // true = saved to pending table (awaiting admin approval)
    bool alreadyRegistered = false;   // true = already in certificate table
    std::string pendingId;            // pending_dsc_registration.id (UUID)
    std::string certificateId;        // certificate.id if already registered
    std::string fingerprint;          // SHA-256 hex (64 chars)
    std::string countryCode;
};

/**
 * @brief Service for saving DSC certificates from SOD to pending approval
 *
 * Inserts DSC into pending_dsc_registration table for admin review.
 * Admin approval via PKD Management triggers actual certificate + LDAP registration.
 */
class DscAutoRegistrationService {
public:
    /**
     * @brief Constructor with query executor injection
     * @param queryExecutor Database query executor (PostgreSQL or Oracle)
     * @throws std::invalid_argument if queryExecutor is nullptr
     */
    explicit DscAutoRegistrationService(common::IQueryExecutor* queryExecutor);

    /** @brief Destructor */
    ~DscAutoRegistrationService() = default;

    /**
     * @brief Save DSC certificate from SOD to pending table for admin approval
     * @param dscCert X509 certificate pointer (not owned, not freed)
     * @param countryCode ISO 3166-1 alpha-2 country code
     * @param verificationId PA verification UUID (for source tracking)
     * @param verificationStatus "VALID" or "INVALID"
     * @return DscRegistrationResult
     */
    DscRegistrationResult registerDscFromSod(
        X509* dscCert,
        const std::string& countryCode,
        const std::string& verificationId,
        const std::string& verificationStatus
    );

private:
    common::IQueryExecutor* queryExecutor_;

    /** @brief Compute SHA-256 fingerprint of an X509 certificate */
    std::string computeFingerprint(X509* cert);

    /** @brief Get DER-encoded bytes of an X509 certificate */
    std::vector<uint8_t> getDerBytes(X509* cert);

    /** @brief Convert X509_NAME to OpenSSL oneline string */
    std::string x509NameToString(X509_NAME* name);

    /** @brief Convert ASN1_TIME to human-readable string */
    std::string asn1TimeToString(const ASN1_TIME* t);

    /** @brief Generate a UUID v4 string */
    std::string generateUuid();
};

} // namespace services
