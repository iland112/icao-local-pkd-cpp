#pragma once

#include <string>
#include <vector>
#include <memory>
#include <json/json.h>
#include "../repositories/statistics_repository.h"
#include "../repositories/upload_repository.h"

/**
 * @file statistics_service.h
 * @brief Statistics Service - System Statistics and Analytics Business Logic Layer
 *
 * Handles statistical data aggregation, reporting, and analytics for
 * uploads, certificates, countries, and system-wide metrics.
 * Following DDD (Domain-Driven Design) and SRP (Single Responsibility Principle).
 *
 * Responsibilities:
 * - Upload statistics (total, by status, by format)
 * - Certificate statistics (by type, by country)
 * - Country statistics (certificates per country, detailed breakdown)
 * - System-wide statistics (total counts, trends)
 * - Validation statistics (success rates, trust chain metrics)
 *
 * Does NOT handle:
 * - HTTP request/response (Controller's job)
 * - Direct database access implementation (Repository's job - but currently mixed)
 * - Business logic for uploads/validation (UploadService/ValidationService's job)
 *
 * @note Part of main.cpp refactoring Phase 1
 * @date 2026-01-29
 */

namespace services {

/**
 * @brief Statistics Service Class
 *
 * Encapsulates all business logic related to statistical data and analytics.
 * Extracted from main.cpp to improve maintainability and testability.
 */
class StatisticsService {
public:
    /**
     * @brief Constructor with Repository Dependency Injection
     * @param statsRepo Statistics repository (non-owning pointer)
     * @param uploadRepo Upload repository (non-owning pointer)
     */
    StatisticsService(
        repositories::StatisticsRepository* statsRepo,
        repositories::UploadRepository* uploadRepo
    );

    /**
     * @brief Destructor
     */
    ~StatisticsService() = default;

    // ========================================================================
    // Upload Statistics
    // ========================================================================

    /**
     * @brief Get upload statistics
     *
     * @return Json::Value Upload statistics
     *
     * Response format:
     * {
     *   "success": true,
     *   "totalUploads": 100,
     *   "byStatus": {
     *     "COMPLETED": 80,
     *     "FAILED": 15,
     *     "PENDING": 5,
     *     "PARSING": 0
     *   },
     *   "byFormat": {
     *     "LDIF": 70,
     *     "MASTER_LIST": 30
     *   },
     *   "recentUploads": 10,  // Last 24 hours
     *   "totalCertificates": 50000,
     *   "totalCrls": 200
     * }
     *
     * Business Logic:
     * 1. Query uploaded_file table for counts
     * 2. Group by status, format
     * 3. Count recent uploads (last 24 hours)
     * 4. Aggregate certificate counts from all uploads
     * 5. Return comprehensive statistics
     */
    Json::Value getUploadStatistics();

    /**
     * @brief Get upload trend data
     *
     * @param days Number of days to analyze (default: 30)
     * @return Json::Value Upload trend data
     *
     * Response includes:
     * - Daily upload counts
     * - Daily certificate counts
     * - Success rate per day
     * - Average processing time per day
     */
    Json::Value getUploadTrend(int days = 30);

    // ========================================================================
    // Certificate Statistics
    // ========================================================================

    /**
     * @brief Get certificate statistics
     *
     * @return Json::Value Certificate statistics by type
     *
     * Response format:
     * {
     *   "success": true,
     *   "totalCertificates": 50000,
     *   "byType": {
     *     "CSCA": 500,
     *     "MLSC": 50,
     *     "DSC": 45000,
     *     "DSC_NC": 500,
     *     "LINK": 100
     *   },
     *   "storedInLdap": 49800,
     *   "notInLdap": 200,
     *   "validationStatus": {
     *     "VALID": 40000,
     *     "INVALID": 5000,
     *     "PENDING": 5000
     *   }
     * }
     */
    Json::Value getCertificateStatistics();

    /**
     * @brief Get CRL statistics
     *
     * @return Json::Value CRL statistics
     *
     * Response includes:
     * - Total CRL count
     * - CRLs by country
     * - CRLs stored in LDAP
     * - Average CRL size
     */
    Json::Value getCrlStatistics();

    // ========================================================================
    // Country Statistics
    // ========================================================================

    /**
     * @brief Get country statistics (summary)
     *
     * @return Json::Value Country statistics (country → total certificate count)
     *
     * Response format:
     * {
     *   "success": true,
     *   "totalCountries": 95,
     *   "countries": {
     *     "KR": 1500,
     *     "US": 2000,
     *     "JP": 1800,
     *     ...
     *   },
     *   "topCountries": [
     *     {"countryCode": "US", "count": 2000},
     *     {"countryCode": "JP", "count": 1800},
     *     {"countryCode": "KR", "count": 1500}
     *   ]
     * }
     */
    Json::Value getCountryStatistics();

    /**
     * @brief Get detailed country statistics
     *
     * @param limit Maximum number of countries to return (0 = all)
     * @return Json::Value Detailed statistics per country
     *
     * Response format:
     * {
     *   "success": true,
     *   "count": 95,
     *   "countries": [
     *     {
     *       "countryCode": "KR",
     *       "countryName": "Korea, Republic of",
     *       "mlscCount": 1,
     *       "cscaSelfSignedCount": 5,
     *       "cscaLinkCertCount": 2,
     *       "dscCount": 1400,
     *       "dscNcCount": 10,
     *       "crlCount": 5,
     *       "totalCertificates": 1423
     *     },
     *     ...
     *   ]
     * }
     *
     * Business Logic:
     * 1. Query certificate table grouped by country_code and certificate_type
     * 2. Distinguish CSCA self-signed vs link certificates
     * 3. Count DSC, DSC_NC, MLSC separately
     * 4. Query CRL table for CRL counts
     * 5. Sort by total certificate count (descending)
     * 6. Limit results if specified
     */
    Json::Value getDetailedCountryStatistics(int limit = 0);

    /**
     * @brief Get country detail
     *
     * @param countryCode ISO 3166-1 alpha-2 country code
     * @return Json::Value Detailed country information
     *
     * Response includes:
     * - Certificate counts by type
     * - Recent uploads for this country
     * - Validation statistics
     * - Available CRLs
     */
    Json::Value getCountryDetail(const std::string& countryCode);

    // ========================================================================
    // Validation Statistics
    // ========================================================================

    /**
     * @brief Get validation statistics
     *
     * @return Json::Value Validation statistics
     *
     * Response format:
     * {
     *   "success": true,
     *   "totalValidations": 30000,
     *   "validCount": 20000,
     *   "invalidCount": 8000,
     *   "pendingCount": 2000,
     *   "successRate": 66.67,
     *   "trustChainStats": {
     *     "trustChainValid": 18000,
     *     "trustChainInvalid": 12000,
     *     "trustChainSuccessRate": 60.0
     *   },
     *   "signatureStats": {
     *     "signatureValid": 25000,
     *     "signatureInvalid": 5000
     *   },
     *   "crlStats": {
     *     "crlChecked": 15000,
     *     "revoked": 500,
     *     "revocationRate": 3.33
     *   }
     * }
     */
    Json::Value getValidationStatistics();

    /**
     * @brief Get validation statistics for specific upload
     *
     * @param uploadId Upload UUID
     * @return Json::Value Upload-specific validation statistics
     */
    Json::Value getValidationStatisticsByUpload(const std::string& uploadId);

    // ========================================================================
    // System-Wide Statistics
    // ========================================================================

    /**
     * @brief Get system-wide statistics (dashboard)
     *
     * @return Json::Value Comprehensive system statistics
     *
     * Response includes:
     * - Total uploads, certificates, CRLs
     * - Upload statistics (by status, format)
     * - Certificate statistics (by type)
     * - Country statistics (total countries, top countries)
     * - Validation statistics (success rate)
     * - Recent activity (last upload, last validation)
     * - System health metrics
     */
    Json::Value getSystemStatistics();

    /**
     * @brief Get database size statistics
     *
     * @return Json::Value Database size information
     *
     * Response includes:
     * - Total database size
     * - Size per table
     * - Row counts per table
     * - Index sizes
     */
    Json::Value getDatabaseStatistics();

    // ========================================================================
    // Trend Analysis
    // ========================================================================

    /**
     * @brief Get certificate growth trend
     *
     * @param days Number of days to analyze (default: 30)
     * @return Json::Value Certificate growth data
     *
     * Response includes:
     * - Daily certificate additions
     * - Cumulative certificate count
     * - Growth rate per day
     * - By certificate type
     */
    Json::Value getCertificateGrowthTrend(int days = 30);

    /**
     * @brief Get validation trend
     *
     * @param days Number of days to analyze (default: 30)
     * @return Json::Value Validation trend data
     *
     * Response includes:
     * - Daily validation counts
     * - Success rate trend
     * - Trust chain validation trend
     * - CRL check trend
     */
    Json::Value getValidationTrend(int days = 30);

    // ========================================================================
    // Export Statistics
    // ========================================================================

    /**
     * @brief Export statistics to CSV format
     *
     * @param statisticsType Type of statistics to export
     *        ("upload", "certificate", "country", "validation")
     * @return std::string CSV formatted statistics
     */
    std::string exportStatisticsToCSV(const std::string& statisticsType);

    /**
     * @brief Generate statistics report
     *
     * @param format Report format ("json", "csv", "html")
     * @return std::string Formatted statistics report
     *
     * Business Logic:
     * 1. Gather all system statistics
     * 2. Format according to specified format
     * 3. Include timestamp and metadata
     * 4. Return formatted report
     */
    std::string generateStatisticsReport(const std::string& format = "json");

private:
    // Repository Dependencies
    repositories::StatisticsRepository* statsRepo_;
    repositories::UploadRepository* uploadRepo_;

    // ========================================================================
    // Helper Methods
    // ========================================================================

    /**
     * @brief Calculate percentage
     *
     * @param part Part value
     * @param total Total value
     * @return double Percentage (0.0 - 100.0)
     */
    double calculatePercentage(int part, int total);
};

} // namespace services
