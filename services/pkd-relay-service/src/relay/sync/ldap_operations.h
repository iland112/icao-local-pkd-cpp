/**
 * @file ldap_operations.h
 * @brief LDAP operations for certificate and CRL management
 */
#pragma once

#include <ldap.h>
#include <string>
#include <vector>
#include <map>
#include "relay/sync/common/types.h"
#include "relay/sync/common/config.h"

namespace icao {
namespace relay {

/**
 * @brief LDAP operations for certificate and CRL management
 *
 * Provides methods for building DNs, adding/deleting certificates
 * and CRLs, and ensuring parent DN hierarchy in LDAP.
 */
class LdapOperations {
public:
    explicit LdapOperations(const Config& config);
    ~LdapOperations() = default;

    /**
     * @brief Build LDAP Distinguished Name for certificate
     * @param certType Certificate type (CSCA, DSC, DSC_NC, LC, CRL)
     * @param countryCode Country code (e.g., KR)
     * @param fingerprint SHA-256 fingerprint (hex)
     * @return Fully qualified LDAP DN
     */
    std::string buildDn(const std::string& certType,
                       const std::string& countryCode,
                       const std::string& fingerprint) const;

    /**
     * @brief Add certificate to LDAP
     * @param ld LDAP connection handle
     * @param cert Certificate information
     * @param errorMsg Error message output
     * @return true if successful
     */
    bool addCertificate(LDAP* ld,
                       const CertificateInfo& cert,
                       std::string& errorMsg) const;

    /**
     * @brief Delete certificate from LDAP
     * @param ld LDAP connection handle
     * @param dn Distinguished Name to delete
     * @param errorMsg Error message output
     * @return true if successful
     */
    bool deleteCertificate(LDAP* ld,
                          const std::string& dn,
                          std::string& errorMsg) const;

    /**
     * @brief Convert certificate DER to PEM format
     * @param certData DER-encoded certificate binary
     * @return PEM-encoded certificate string
     */
    static std::string certToPem(const std::vector<unsigned char>& certData);

    /**
     * @brief Ensure parent DN hierarchy exists (create if missing)
     * @param ld LDAP connection handle
     * @param certType Certificate type
     * @param countryCode Country code
     * @param errorMsg Error message output
     * @return true if hierarchy exists or was created
     */
    bool ensureParentDnExists(LDAP* ld,
                             const std::string& certType,
                             const std::string& countryCode,
                             std::string& errorMsg) const;

    /**
     * @brief Build LDAP Distinguished Name for CRL
     * @param countryCode Country code
     * @param fingerprint SHA-256 fingerprint (hex)
     * @return Fully qualified LDAP DN for CRL
     */
    std::string buildCrlDn(const std::string& countryCode,
                          const std::string& fingerprint) const;

    /**
     * @brief Add CRL to LDAP
     * @param ld LDAP connection handle
     * @param crl CRL information
     * @param errorMsg Error message output
     * @return true if successful
     */
    bool addCrl(LDAP* ld,
               const CrlInfo& crl,
               std::string& errorMsg) const;

private:
    const Config& config_;

    /**
     * @brief Create LDAP entry if it does not already exist
     * @param ld LDAP connection handle
     * @param dn Distinguished Name
     * @param objectClasses Object class values
     * @param attributes Additional attributes
     * @return true if entry exists or was created
     */
    bool createEntryIfNotExists(LDAP* ld,
                               const std::string& dn,
                               const std::vector<std::string>& objectClasses,
                               const std::map<std::string, std::string>& attributes) const;
};

} // namespace relay
} // namespace icao
