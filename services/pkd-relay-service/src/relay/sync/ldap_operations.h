#pragma once

#include <ldap.h>
#include <string>
#include <vector>
#include <map>
#include "relay/sync/common/types.h"
#include "relay/sync/common/config.h"

namespace icao {
namespace relay {

// =============================================================================
// LDAP Operations - Certificate Management
// =============================================================================
class LdapOperations {
public:
    explicit LdapOperations(const Config& config);
    ~LdapOperations() = default;

    // Build LDAP Distinguished Name for certificate
    // v2.0.3: Uses fingerprint (SHA-256) instead of UUID for DN
    std::string buildDn(const std::string& certType,
                       const std::string& countryCode,
                       const std::string& fingerprint) const;

    // Add certificate to LDAP
    bool addCertificate(LDAP* ld,
                       const CertificateInfo& cert,
                       std::string& errorMsg) const;

    // Delete certificate from LDAP
    bool deleteCertificate(LDAP* ld,
                          const std::string& dn,
                          std::string& errorMsg) const;

    // Convert certificate DER to PEM format
    static std::string certToPem(const std::vector<unsigned char>& certData);

    // v2.0.4: Ensure parent DN hierarchy exists (create if missing)
    bool ensureParentDnExists(LDAP* ld,
                             const std::string& certType,
                             const std::string& countryCode,
                             std::string& errorMsg) const;

    // v2.0.5: CRL operations
    std::string buildCrlDn(const std::string& countryCode,
                          const std::string& fingerprint) const;

    bool addCrl(LDAP* ld,
               const CrlInfo& crl,
               std::string& errorMsg) const;

private:
    const Config& config_;

    // Helper to create LDAP entry if it doesn't exist
    bool createEntryIfNotExists(LDAP* ld,
                               const std::string& dn,
                               const std::vector<std::string>& objectClasses,
                               const std::map<std::string, std::string>& attributes) const;
};

} // namespace relay
} // namespace icao
