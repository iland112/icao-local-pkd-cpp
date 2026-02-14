/**
 * @file ldif_processor.h
 * @brief LDIF file processor for certificate, CRL, and Master List extraction
 */

#pragma once

#include <string>
#include <vector>
#include <libpq-fe.h>
#include <ldap.h>
#include "common.h"
#include "common/progress_manager.h"

/**
 * @brief LDIF file processor
 *
 * Handles parsing and processing of LDIF files including:
 * - Parsing LDIF content
 * - Extracting certificates, CRLs, and master lists
 * - Saving to PostgreSQL database
 * - Validating trust chains
 * - Uploading to LDAP
 */
class LdifProcessor {
public:
    /**
     * @brief Parse LDIF content
     * @param content Raw LDIF file content as string
     * @return Vector of parsed LDIF entries
     */
    static std::vector<LdifEntry> parseLdifContent(const std::string& content);

    /**
     * @brief Process LDIF entries (save to DB and validate)
     * @param uploadId Upload record UUID
     * @param entries Parsed LDIF entries
     * @param ld LDAP connection (can be nullptr to skip LDAP upload)
     * @param stats Output validation statistics
     * @return Processing counts (csca, dsc, dsc_nc, crl, ml)
     * @note Uses global certificateRepository and uploadRepository for database operations
     */
    struct ProcessingCounts {
        int cscaCount = 0;
        int dscCount = 0;
        int dscNcCount = 0;
        int crlCount = 0;
        int mlCount = 0;
        int mlscCount = 0;  // Master List Signer Certificate count (v2.1.1)
        int ldapCertStoredCount = 0;
        int ldapCrlStoredCount = 0;
        int ldapMlStoredCount = 0;
    };

    struct TotalCounts {
        int totalCerts = 0;
        int totalCrl = 0;
        int totalMl = 0;
    };

    static ProcessingCounts processEntries(
        const std::string& uploadId,
        const std::vector<LdifEntry>& entries,
        LDAP* ld,
        ValidationStats& stats,
        common::ValidationStatistics& enhancedStats,
        const TotalCounts* totalCounts = nullptr  // Optional: for "X/Total" progress display
    );

    /**
     * @brief Upload certificates from DB to LDAP
     * @param uploadId Upload record UUID
     * @param ld LDAP connection
     * @return Number of entries uploaded
     * @note Uses global certificateRepository for database operations
     */
    static int uploadToLdap(
        const std::string& uploadId,
        LDAP* ld
    );
};
