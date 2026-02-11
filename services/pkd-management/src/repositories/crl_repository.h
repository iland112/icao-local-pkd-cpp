#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "i_query_executor.h"

/**
 * @file crl_repository.h
 * @brief CRL Repository - Database Access Layer for crl and revoked_certificate tables
 *
 * Database-agnostic interface using IQueryExecutor (supports PostgreSQL and Oracle).
 *
 * @note Phase 6.4: Extracted from main.cpp inline SQL
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

private:
    common::IQueryExecutor* queryExecutor_;

    std::string generateUuid();
};

} // namespace repositories
