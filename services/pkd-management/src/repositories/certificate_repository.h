#pragma once

#include <string>
#include <vector>
#include <optional>
#include <libpq-fe.h>
#include <json/json.h>

/**
 * @file certificate_repository.h
 * @brief Certificate Repository - Database Access Layer for certificate table
 *
 * Handles all database operations related to certificates (CSCA, DSC, DSC_NC, MLSC, Link Certs).
 * Database-agnostic interface (currently PostgreSQL, future: Oracle support).
 *
 * @note Part of main.cpp refactoring Phase 1.5
 * @date 2026-01-29
 */

namespace repositories {

/**
 * @brief Certificate Search Filter
 */
struct CertificateSearchFilter {
    std::optional<std::string> fingerprint;
    std::optional<std::string> subjectDn;
    std::optional<std::string> issuerDn;
    std::optional<std::string> countryCode;
    std::optional<std::string> certificateType;  // "CSCA", "DSC", "DSC_NC", "MLSC"
    int limit = 100;
    int offset = 0;
};

/**
 * @brief Repository for certificate table
 *
 * Handles database operations for certificate CRUD and search.
 * Database-agnostic design for future Oracle migration.
 */
class CertificateRepository {
public:
    explicit CertificateRepository(PGconn* dbConn);
    ~CertificateRepository() = default;

    // ========================================================================
    // Search Operations
    // ========================================================================

    /**
     * @brief Search certificates with filters
     * @param filter Search filter
     * @return JSON array of certificates
     */
    Json::Value search(const CertificateSearchFilter& filter);

    /**
     * @brief Find certificate by fingerprint (SHA-256)
     * @param fingerprint 64-character hex string
     * @return JSON object if found, null otherwise
     */
    Json::Value findByFingerprint(const std::string& fingerprint);

    /**
     * @brief Find certificates by country code
     * @param countryCode ISO 3166-1 alpha-2 code
     * @param limit Maximum results
     * @param offset Pagination offset
     * @return JSON array of certificates
     */
    Json::Value findByCountry(const std::string& countryCode, int limit, int offset);

    /**
     * @brief Find certificates by subject DN
     * @param subjectDn Subject Distinguished Name
     * @param limit Maximum results
     * @return JSON array of certificates
     */
    Json::Value findBySubjectDn(const std::string& subjectDn, int limit);

    // ========================================================================
    // Certificate Counts
    // ========================================================================

    /**
     * @brief Count certificates by type
     * @param certType "CSCA", "DSC", "DSC_NC", "MLSC"
     * @return Count
     */
    int countByType(const std::string& certType);

    /**
     * @brief Count total certificates
     * @return Total count
     */
    int countAll();

    /**
     * @brief Count certificates by country
     * @param countryCode Country code
     * @return Count
     */
    int countByCountry(const std::string& countryCode);

    // ========================================================================
    // LDAP Storage Tracking
    // ========================================================================

    /**
     * @brief Find certificates not yet stored in LDAP
     * @param limit Maximum results
     * @return JSON array of certificates
     */
    Json::Value findNotStoredInLdap(int limit);

    /**
     * @brief Mark certificate as stored in LDAP
     * @param fingerprint Certificate fingerprint
     * @return true if updated successfully
     */
    bool markStoredInLdap(const std::string& fingerprint);

private:
    PGconn* dbConn_;

    PGresult* executeParamQuery(const std::string& query, const std::vector<std::string>& params);
    PGresult* executeQuery(const std::string& query);
    Json::Value pgResultToJson(PGresult* res);
};

} // namespace repositories
