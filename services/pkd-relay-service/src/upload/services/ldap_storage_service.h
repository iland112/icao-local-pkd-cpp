#pragma once

/**
 * @file ldap_storage_service.h
 * @brief LDAP storage operations for certificates, CRLs, and Master Lists
 *
 * Encapsulates all LDAP write operations including DN construction,
 * OU auto-creation, and certificate/CRL/ML storage.
 */

#include <string>
#include <vector>
#include <cstdint>
#include <atomic>
#include <utility>

// Forward declarations
typedef struct ldap LDAP;
struct AppConfig;

namespace services {

/**
 * @brief LDAP storage service for PKD data
 *
 * Manages LDAP connections and storage for:
 * - Certificates (CSCA, DSC, DSC_NC, LC, MLSC)
 * - CRLs (Certificate Revocation Lists)
 * - Master Lists
 *
 * All DN construction follows ICAO PKD DIT structure with RFC 4514 escaping.
 */
class LdapStorageService {
public:
    /**
     * @brief Construct LdapStorageService
     * @param config Application configuration (non-owning reference, must outlive service)
     */
    explicit LdapStorageService(const AppConfig& config);

    // --- LDAP Connection Management ---

    /**
     * @brief Get LDAP connection for write operations (direct to primary master)
     * @return LDAP connection or nullptr on failure. Caller must call ldap_unbind_ext_s().
     */
    LDAP* getLdapWriteConnection();

    /**
     * @brief Get LDAP connection for read operations with round-robin load balancing
     * @return LDAP connection or nullptr on failure. Caller must call ldap_unbind_ext_s().
     */
    LDAP* getLdapReadConnection();

    // --- DN Building ---

    /**
     * @brief Escape special characters in LDAP DN attribute values (RFC 4514)
     */
    static std::string escapeLdapDnValue(const std::string& value);

    /**
     * @brief Extract standard vs non-standard DN attributes
     * @return Pair of (standardDn, nonStandardAttrs)
     */
    static std::pair<std::string, std::string> extractStandardAttributes(const std::string& subjectDn);

    /**
     * @brief Build legacy certificate DN (Subject DN + Serial based)
     */
    std::string buildCertificateDn(const std::string& certType, const std::string& countryCode,
                                    const std::string& subjectDn, const std::string& serialNumber);

    /**
     * @brief Build v2 certificate DN (Fingerprint based)
     */
    std::string buildCertificateDnV2(const std::string& fingerprint, const std::string& certType,
                                      const std::string& countryCode);

    /**
     * @brief Build CRL DN
     */
    std::string buildCrlDn(const std::string& countryCode, const std::string& fingerprint);

    /**
     * @brief Build Master List DN
     */
    std::string buildMasterListDn(const std::string& countryCode, const std::string& fingerprint);

    // --- LDAP OU Management ---

    /**
     * @brief Ensure country organizational unit and sub-OUs exist in LDAP
     */
    bool ensureCountryOuExists(LDAP* ld, const std::string& countryCode, bool isNcData = false);

    /**
     * @brief Ensure Master List OU (o=ml) exists under country entry
     */
    bool ensureMasterListOuExists(LDAP* ld, const std::string& countryCode);

    // --- LDAP Storage ---

    /**
     * @brief Save certificate to LDAP
     * @return LDAP DN on success, empty string on failure
     */
    std::string saveCertificateToLdap(LDAP* ld, const std::string& certType,
                                       const std::string& countryCode,
                                       const std::string& subjectDn, const std::string& issuerDn,
                                       const std::string& serialNumber, const std::string& fingerprint,
                                       const std::vector<uint8_t>& certBinary,
                                       const std::string& pkdConformanceCode = "",
                                       const std::string& pkdConformanceText = "",
                                       const std::string& pkdVersion = "",
                                       bool useLegacyDn = false);

    /**
     * @brief Save CRL to LDAP
     * @return LDAP DN on success, empty string on failure
     */
    std::string saveCrlToLdap(LDAP* ld, const std::string& countryCode,
                               const std::string& issuerDn, const std::string& fingerprint,
                               const std::vector<uint8_t>& crlBinary);

    /**
     * @brief Save Master List to LDAP
     * @return LDAP DN on success, empty string on failure
     */
    std::string saveMasterListToLdap(LDAP* ld, const std::string& countryCode,
                                      const std::string& signerDn, const std::string& fingerprint,
                                      const std::vector<uint8_t>& mlBinary);

private:
    const AppConfig& config_;
    std::atomic<size_t> ldapReadRoundRobinIndex_{0};
};

} // namespace services
