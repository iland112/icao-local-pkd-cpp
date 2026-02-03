#pragma once

#include <string>
#include <json/json.h>

namespace icao::relay::domain {

/**
 * @brief Minimal Certificate domain model for reconciliation
 *
 * Subset of the full certificate table focused on reconciliation needs.
 * Only includes fields required for DB-LDAP synchronization operations.
 */
class Certificate {
public:
    // Constructors
    Certificate() = default;

    Certificate(
        const std::string& id,
        const std::string& fingerprint_sha256,
        const std::string& certificate_type,
        const std::string& country_code,
        const std::string& subject_dn,
        const std::string& issuer_dn,
        bool stored_in_ldap
    )
        : id_(id), fingerprint_sha256_(fingerprint_sha256),
          certificate_type_(certificate_type), country_code_(country_code),
          subject_dn_(subject_dn), issuer_dn_(issuer_dn),
          stored_in_ldap_(stored_in_ldap)
    {}

    // Getters
    std::string getId() const { return id_; }
    std::string getFingerprintSha256() const { return fingerprint_sha256_; }
    std::string getCertificateType() const { return certificate_type_; }
    std::string getCountryCode() const { return country_code_; }
    std::string getSubjectDn() const { return subject_dn_; }
    std::string getIssuerDn() const { return issuer_dn_; }
    bool isStoredInLdap() const { return stored_in_ldap_; }

    // Setters
    void setId(const std::string& id) { id_ = id; }
    void setFingerprintSha256(const std::string& fingerprint) { fingerprint_sha256_ = fingerprint; }
    void setCertificateType(const std::string& type) { certificate_type_ = type; }
    void setCountryCode(const std::string& country_code) { country_code_ = country_code; }
    void setSubjectDn(const std::string& subject_dn) { subject_dn_ = subject_dn; }
    void setIssuerDn(const std::string& issuer_dn) { issuer_dn_ = issuer_dn; }
    void setStoredInLdap(bool stored) { stored_in_ldap_ = stored; }

    /**
     * @brief Convert to JSON representation
     */
    Json::Value toJson() const;

private:
    std::string id_;                    // UUID primary key
    std::string fingerprint_sha256_;    // SHA-256 fingerprint
    std::string certificate_type_;      // CSCA, MLSC, DSC, DSC_NC
    std::string country_code_;          // Country code (e.g., KR)
    std::string subject_dn_;            // Certificate subject DN
    std::string issuer_dn_;             // Certificate issuer DN
    bool stored_in_ldap_ = false;       // Sync status flag
};

} // namespace icao::relay::domain
