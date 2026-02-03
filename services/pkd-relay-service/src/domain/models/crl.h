#pragma once

#include <string>
#include <chrono>
#include <vector>
#include <json/json.h>

namespace icao::relay::domain {

/**
 * @brief Domain model for crl table
 *
 * Represents a Certificate Revocation List (CRL) from LDAP/Database.
 * Used for tracking CRL synchronization status between DB and LDAP.
 */
class Crl {
public:
    // Constructors
    Crl() = default;

    Crl(
        const std::string& id,
        const std::string& fingerprint_sha256,
        const std::string& issuer_dn,
        const std::string& country_code,
        const std::chrono::system_clock::time_point& this_update,
        const std::chrono::system_clock::time_point& next_update,
        bool stored_in_ldap,
        const std::vector<unsigned char>& crl_data
    )
        : id_(id), fingerprint_sha256_(fingerprint_sha256),
          issuer_dn_(issuer_dn), country_code_(country_code),
          this_update_(this_update), next_update_(next_update),
          stored_in_ldap_(stored_in_ldap), crl_data_(crl_data)
    {}

    // Getters
    std::string getId() const { return id_; }
    std::string getFingerprintSha256() const { return fingerprint_sha256_; }
    std::string getIssuerDn() const { return issuer_dn_; }
    std::string getCountryCode() const { return country_code_; }
    std::chrono::system_clock::time_point getThisUpdate() const { return this_update_; }
    std::chrono::system_clock::time_point getNextUpdate() const { return next_update_; }
    bool isStoredInLdap() const { return stored_in_ldap_; }
    std::vector<unsigned char> getCrlData() const { return crl_data_; }

    // Setters
    void setId(const std::string& id) { id_ = id; }
    void setFingerprintSha256(const std::string& fingerprint) { fingerprint_sha256_ = fingerprint; }
    void setIssuerDn(const std::string& issuer_dn) { issuer_dn_ = issuer_dn; }
    void setCountryCode(const std::string& country_code) { country_code_ = country_code; }
    void setThisUpdate(const std::chrono::system_clock::time_point& this_update) {
        this_update_ = this_update;
    }
    void setNextUpdate(const std::chrono::system_clock::time_point& next_update) {
        next_update_ = next_update;
    }
    void setStoredInLdap(bool stored) { stored_in_ldap_ = stored; }
    void setCrlData(const std::vector<unsigned char>& data) { crl_data_ = data; }

    /**
     * @brief Convert to JSON representation (without binary CRL data)
     */
    Json::Value toJson() const;

private:
    std::string id_;                           // UUID primary key
    std::string fingerprint_sha256_;           // SHA-256 fingerprint
    std::string issuer_dn_;                    // CRL issuer DN
    std::string country_code_;                 // Country code (e.g., KR)
    std::chrono::system_clock::time_point this_update_;  // CRL thisUpdate time
    std::chrono::system_clock::time_point next_update_;  // CRL nextUpdate time
    bool stored_in_ldap_ = false;              // Sync status flag
    std::vector<unsigned char> crl_data_;      // Binary CRL data (DER format)
};

} // namespace icao::relay::domain
