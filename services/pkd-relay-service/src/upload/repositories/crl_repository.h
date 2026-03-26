#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <json/json.h>
#include "i_query_executor.h"

/**
 * @file crl_repository.h
 * @brief CRL Repository - Database Access Layer for crl and revoked_certificate tables
 *
 * Database-agnostic interface using IQueryExecutor (supports PostgreSQL and Oracle).
 *
 * @date 2026-02-11
 */

namespace repositories {

class CrlRepository {
public:
    explicit CrlRepository(common::IQueryExecutor* queryExecutor);
    ~CrlRepository() = default;

    /**
     * @brief Save CRL to database
     * @return CRL ID or empty string on failure
     */
    std::string save(const std::string& uploadId,
                     const std::string& countryCode,
                     const std::string& issuerDn,
                     const std::string& thisUpdate,
                     const std::string& nextUpdate,
                     const std::string& crlNumber,
                     const std::string& fingerprint,
                     const std::vector<uint8_t>& crlBinary);

    /**
     * @brief Save revoked certificate to database
     * @note revoked_certificate table only exists in PostgreSQL schema
     */
    void saveRevokedCertificate(const std::string& crlId,
                                const std::string& serialNumber,
                                const std::string& revocationDate,
                                const std::string& reason);

    /**
     * @brief Update CRL LDAP status after successful LDAP storage
     */
    void updateLdapStatus(const std::string& crlId, const std::string& ldapDn);

    /**
     * @brief Find CRL binary data by country code
     * @param countryCode ISO 3166-1 alpha-2 country code
     * @return JSON object with crl_binary (hex), this_update, next_update, or null if not found
     */
    Json::Value findByCountryCode(const std::string& countryCode);

    /**
     * @brief Find all CRLs stored in LDAP for bulk export
     * @return JSON array with country_code, issuer_dn, crl_binary (hex), fingerprint_sha256
     */
    Json::Value findAllForExport();

    /**
     * @brief Find all CRLs with metadata (paginated, filtered)
     */
    Json::Value findAll(const std::string& countryFilter = "",
                        const std::string& statusFilter = "",
                        int limit = 100,
                        int offset = 0);

    /**
     * @brief Count total CRLs matching filters
     */
    int countAll(const std::string& countryFilter = "",
                 const std::string& statusFilter = "");

    /**
     * @brief Find CRL by ID (includes crl_binary for detail parsing)
     */
    Json::Value findById(const std::string& crlId);

private:
    common::IQueryExecutor* queryExecutor_;

    std::string generateUuid();
};

} // namespace repositories
