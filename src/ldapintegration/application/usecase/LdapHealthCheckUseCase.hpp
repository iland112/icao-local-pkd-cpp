/**
 * @file LdapHealthCheckUseCase.hpp
 * @brief LDAP Health Check Use Case
 */

#pragma once

#include "ldapintegration/domain/port/ILdapConnectionPort.hpp"
#include "shared/exception/ApplicationException.hpp"
#include <memory>
#include <chrono>
#include <spdlog/spdlog.h>

namespace ldapintegration::application::usecase {

using namespace ldapintegration::domain::port;

/**
 * @brief LDAP Health Status
 */
enum class LdapHealthStatus {
    HEALTHY,        // Connection is working
    DEGRADED,       // Connection works but has issues
    UNHEALTHY,      // Connection is not working
    UNKNOWN         // Status cannot be determined
};

inline std::string toString(LdapHealthStatus status) {
    switch (status) {
        case LdapHealthStatus::HEALTHY:   return "HEALTHY";
        case LdapHealthStatus::DEGRADED:  return "DEGRADED";
        case LdapHealthStatus::UNHEALTHY: return "UNHEALTHY";
        default:                          return "UNKNOWN";
    }
}

/**
 * @brief LDAP Health Check Result
 */
struct LdapHealthCheckResult {
    LdapHealthStatus status;
    bool connectionAvailable;
    std::string poolStats;
    int64_t responseTimeMs;
    std::string baseDn;
    int entryCount;
    std::string message;
    std::chrono::system_clock::time_point checkedAt;

    [[nodiscard]] bool isHealthy() const noexcept {
        return status == LdapHealthStatus::HEALTHY;
    }
};

/**
 * @brief LDAP Statistics Result
 */
struct LdapStatisticsResult {
    int totalCscaCount;
    int totalDscCount;
    int totalDscNcCount;
    int totalCrlCount;
    int totalMasterListCount;
    std::map<std::string, int> countryStats;
    std::chrono::system_clock::time_point retrievedAt;
};

/**
 * @brief LDAP Health Check Use Case
 *
 * Provides health monitoring and statistics for LDAP connection.
 */
class LdapHealthCheckUseCase {
private:
    std::shared_ptr<ILdapConnectionPort> ldapPort_;

public:
    explicit LdapHealthCheckUseCase(std::shared_ptr<ILdapConnectionPort> ldapPort)
        : ldapPort_(std::move(ldapPort)) {
        if (!ldapPort_) {
            throw shared::exception::ApplicationException(
                "INVALID_LDAP_PORT",
                "LDAP port cannot be null"
            );
        }
    }

    /**
     * @brief Perform LDAP health check
     */
    LdapHealthCheckResult checkHealth() {
        spdlog::debug("Performing LDAP health check");

        LdapHealthCheckResult result{
            .status = LdapHealthStatus::UNKNOWN,
            .connectionAvailable = false,
            .poolStats = "",
            .responseTimeMs = 0,
            .baseDn = "",
            .entryCount = 0,
            .message = "",
            .checkedAt = std::chrono::system_clock::now()
        };

        try {
            auto startTime = std::chrono::steady_clock::now();

            // Check connection
            result.connectionAvailable = ldapPort_->isConnected();
            result.poolStats = ldapPort_->getPoolStats();
            result.baseDn = ldapPort_->getBaseDn();

            // Test actual connection
            bool connectionTest = ldapPort_->testConnection();

            auto endTime = std::chrono::steady_clock::now();
            result.responseTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                endTime - startTime
            ).count();

            if (!connectionTest) {
                result.status = LdapHealthStatus::UNHEALTHY;
                result.message = "Connection test failed";
                spdlog::warn("LDAP health check failed: {}", result.message);
                return result;
            }

            // Count entries in base DN
            result.entryCount = ldapPort_->countEntries(
                LdapSearchFilter::subtree(result.baseDn, "(objectClass=*)")
            );

            // Determine health status
            if (result.responseTimeMs < 1000) {
                result.status = LdapHealthStatus::HEALTHY;
                result.message = "LDAP connection is healthy";
            } else if (result.responseTimeMs < 5000) {
                result.status = LdapHealthStatus::DEGRADED;
                result.message = "LDAP response time is slow";
            } else {
                result.status = LdapHealthStatus::DEGRADED;
                result.message = "LDAP response time is very slow";
            }

            spdlog::info("LDAP health check: {} ({}ms)", toString(result.status), result.responseTimeMs);

        } catch (const std::exception& e) {
            result.status = LdapHealthStatus::UNHEALTHY;
            result.message = std::string("Health check error: ") + e.what();
            spdlog::error("LDAP health check error: {}", e.what());
        }

        return result;
    }

    /**
     * @brief Get LDAP statistics
     */
    LdapStatisticsResult getStatistics() {
        spdlog::debug("Retrieving LDAP statistics");

        LdapStatisticsResult result{
            .totalCscaCount = 0,
            .totalDscCount = 0,
            .totalDscNcCount = 0,
            .totalCrlCount = 0,
            .totalMasterListCount = 0,
            .countryStats = {},
            .retrievedAt = std::chrono::system_clock::now()
        };

        try {
            std::string baseDn = ldapPort_->getBaseDn();

            // Count certificates by type
            result.totalCscaCount = countEntriesByType(LdapEntryType::CSCA, baseDn);
            result.totalDscCount = countEntriesByType(LdapEntryType::DSC, baseDn);
            result.totalDscNcCount = countEntriesByType(LdapEntryType::DSC_NC, baseDn);
            result.totalCrlCount = countEntriesByType(LdapEntryType::CRL, baseDn);
            result.totalMasterListCount = countEntriesByType(LdapEntryType::MASTER_LIST, baseDn);

            // Get country statistics
            result.countryStats = getCountryStatistics(baseDn);

            spdlog::info("LDAP statistics: CSCA={}, DSC={}, DSC_NC={}, CRL={}, ML={}",
                result.totalCscaCount,
                result.totalDscCount,
                result.totalDscNcCount,
                result.totalCrlCount,
                result.totalMasterListCount
            );

        } catch (const std::exception& e) {
            spdlog::error("Failed to retrieve LDAP statistics: {}", e.what());
        }

        return result;
    }

    /**
     * @brief Quick connectivity check
     */
    bool isConnected() {
        return ldapPort_->testConnection();
    }

private:
    int countEntriesByType(LdapEntryType type, const std::string& baseDn) {
        std::string ouPath = getOuPath(type, baseDn);
        return ldapPort_->countEntries(
            LdapSearchFilter::subtree(ouPath, "(objectClass=*)")
        );
    }

    std::map<std::string, int> getCountryStatistics(const std::string& baseDn) {
        std::map<std::string, int> stats;

        // Search for country entries
        std::string dataPath = "dc=data,dc=download,dc=pkd," + baseDn;
        auto filter = LdapSearchFilter::oneLevel(dataPath, "(objectClass=country)");

        auto results = ldapPort_->search(filter);

        for (const auto& entry : results) {
            // Extract country code from DN
            auto countryCode = entry.getAttributeValue("c");
            if (countryCode) {
                // Count entries under this country
                std::string countryPath = "c=" + *countryCode + "," + dataPath;
                int count = ldapPort_->countEntries(
                    LdapSearchFilter::subtree(countryPath, "(objectClass=*)")
                );
                stats[*countryCode] = count;
            }
        }

        return stats;
    }
};

} // namespace ldapintegration::application::usecase
