#pragma once

#include <string>
#include <map>
#include <vector>
#include <cstdint>

/**
 * @brief LDIF Entry structure
 */
struct LdifEntry {
    std::string dn;
    std::map<std::string, std::vector<std::string>> attributes;

    bool hasAttribute(const std::string& name) const {
        return attributes.find(name) != attributes.end();
    }

    std::string getFirstAttribute(const std::string& name) const {
        auto it = attributes.find(name);
        if (it != attributes.end() && !it->second.empty()) {
            return it->second[0];
        }
        return "";
    }
};

/**
 * @brief Validation statistics tracking
 */
struct ValidationStats {
    int validCount = 0;
    int invalidCount = 0;
    int pendingCount = 0;
    int errorCount = 0;
    int trustChainValidCount = 0;
    int trustChainInvalidCount = 0;
    int cscaNotFoundCount = 0;
    int expiredCount = 0;
    int revokedCount = 0;
};

// Forward declarations
struct pg_conn;
typedef struct pg_conn PGconn;
struct ldap;
typedef struct ldap LDAP;

/**
 * @brief Master List processing core function
 *
 * Processes ICAO Master List (CMS/PKCS7 format) containing CSCA certificates.
 * Parses CMS structure, extracts certificates, validates, and saves to DB.
 * Optionally uploads to LDAP if connection is provided.
 *
 * @param uploadId Upload record UUID
 * @param content Raw Master List file content
 * @param conn PostgreSQL connection
 * @param ld LDAP connection (can be nullptr for MANUAL mode Stage 2)
 */
void processMasterListContentCore(
    const std::string& uploadId,
    const std::vector<uint8_t>& content,
    PGconn* conn,
    LDAP* ld
);
