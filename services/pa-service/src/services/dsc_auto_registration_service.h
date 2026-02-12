/**
 * @file dsc_auto_registration_service.h
 * @brief DSC Auto-Registration from PA Verification
 *
 * When PA verification extracts a DSC from SOD that is not yet
 * registered in the local PKD, this service automatically registers it.
 * PKD Relay reconciliation handles LDAP sync (stored_in_ldap=FALSE).
 *
 * @date 2026-02-12
 */

#pragma once

#include <string>
#include <vector>
#include <openssl/x509.h>
#include "i_query_executor.h"

namespace services {

/**
 * @brief Result of DSC auto-registration attempt
 */
struct DscRegistrationResult {
    bool success = false;         // Operation completed without error
    bool newlyRegistered = false; // true = new DSC inserted, false = already existed
    std::string certificateId;    // certificate.id (UUID)
    std::string fingerprint;      // SHA-256 hex (64 chars)
    std::string countryCode;
};

/**
 * @brief Service for auto-registering DSC certificates from SOD
 *
 * Inserts DSC into certificate table with source_type='PA_EXTRACTED'.
 * Does NOT write to LDAP — PKD Relay reconciliation handles that.
 */
class DscAutoRegistrationService {
public:
    explicit DscAutoRegistrationService(common::IQueryExecutor* queryExecutor);
    ~DscAutoRegistrationService() = default;

    /**
     * @brief Register DSC certificate from SOD if not already in local PKD
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

    std::string computeFingerprint(X509* cert);
    std::vector<uint8_t> getDerBytes(X509* cert);
    std::string x509NameToString(X509_NAME* name);
    std::string asn1TimeToString(const ASN1_TIME* t);
    std::string generateUuid();
};

} // namespace services
