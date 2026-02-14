#pragma once

/**
 * @file validation_statistics.h
 * @brief Domain Model for Validation Statistics
 */

namespace domain {
namespace models {

/**
 * @brief Validation statistics for an upload batch
 *
 * This struct aggregates validation results for all certificates
 * in an upload batch, used to update the uploaded_file table.
 */
struct ValidationStatistics {
    // Validation status counts
    int validCount = 0;          // Number of VALID certificates
    int invalidCount = 0;        // Number of INVALID certificates
    int pendingCount = 0;        // Number of PENDING certificates
    int errorCount = 0;          // Number of ERROR certificates

    // Trust chain validation counts
    int trustChainValidCount = 0;    // Number with valid trust chain
    int trustChainInvalidCount = 0;  // Number with invalid trust chain

    // Specific validation failure counts
    int cscaNotFoundCount = 0;   // Number where CSCA was not found
    int expiredCount = 0;        // Number of expired certificates
    int revokedCount = 0;        // Number of revoked certificates
};

} // namespace models
} // namespace domain
