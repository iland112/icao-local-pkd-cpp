/**
 * @file processing_strategy.h
 * @brief Processing strategy for upload processing
 *
 * AUTO mode processes files in one go:
 * 1. Parse
 * 2. Save to DB with validation
 * 3. Upload to LDAP (if connection available, otherwise DB-only with later reconciliation)
 *
 * On failure: status=FAILED, original file preserved on disk.
 * Retry via POST /api/upload/{uploadId}/retry cleans up partial data and re-processes.
 */

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <ldap.h>
#include "common.h"

/**
 * @brief AUTO mode processing strategy
 *
 * Processes files in one go:
 * 1. Parse
 * 2. Save to DB with validation
 * 3. Upload to LDAP (if connection available)
 *
 * If LDAP is unavailable (ld=nullptr), certificates are saved to DB with
 * stored_in_ldap=FALSE. The reconciliation engine syncs them to LDAP later.
 */
class AutoProcessingStrategy {
public:
    /**
     * @brief Process LDIF entries (parse → validate → save to DB + LDAP)
     * @param uploadId Upload record UUID
     * @param entries Parsed LDIF entries
     * @param ld LDAP connection (can be nullptr for DB-only mode)
     */
    void processLdifEntries(
        const std::string& uploadId,
        const std::vector<LdifEntry>& entries,
        LDAP* ld
    );

    /**
     * @brief Process Master List content (CMS parse → validate → save to DB + LDAP)
     * @param uploadId Upload record UUID
     * @param content Raw file content
     * @param ld LDAP connection (can be nullptr for DB-only mode)
     */
    void processMasterListContent(
        const std::string& uploadId,
        const std::vector<uint8_t>& content,
        LDAP* ld
    );
};
