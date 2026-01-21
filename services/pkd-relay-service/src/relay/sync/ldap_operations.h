#pragma once

#include <ldap.h>
#include <string>
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
    std::string buildDn(const std::string& certType,
                       const std::string& countryCode,
                       const std::string& certId) const;

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

private:
    const Config& config_;
};

} // namespace relay
} // namespace icao
