#pragma once

#include <ldap.h>
#include <string>
#include "../common/types.h"
#include "../common/config.h"

namespace icao {
namespace sync {

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
                       int certId) const;

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

} // namespace sync
} // namespace icao
