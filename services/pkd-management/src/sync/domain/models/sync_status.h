/**
 * @file sync_status.h
 * @brief Sync status domain model for DB-LDAP synchronization tracking
 */
#pragma once

#include <string>
#include <chrono>
#include <optional>
#include <json/json.h>

namespace icao::relay::domain {

/**
 * @brief Domain model for sync_status table
 *
 * Represents the synchronization status between PostgreSQL database
 * and LDAP directory at a specific point in time.
 */
class SyncStatus {
public:
    /// @name Constructors
    /// @{
    SyncStatus() = default;

    SyncStatus(
        const std::string& id,
        const std::chrono::system_clock::time_point& checked_at,
        int db_csca_count, int ldap_csca_count, int csca_discrepancy,
        int db_mlsc_count, int ldap_mlsc_count, int mlsc_discrepancy,
        int db_dsc_count, int ldap_dsc_count, int dsc_discrepancy,
        int db_dsc_nc_count, int ldap_dsc_nc_count, int dsc_nc_discrepancy,
        int db_crl_count, int ldap_crl_count, int crl_discrepancy,
        int total_discrepancy,
        int db_stored_in_ldap_count, int ldap_total_entries,
        const std::optional<Json::Value>& db_country_stats,
        const std::optional<Json::Value>& ldap_country_stats,
        const std::string& status,
        const std::optional<std::string>& error_message,
        int check_duration_ms
    )
        : id_(id), checked_at_(checked_at),
          db_csca_count_(db_csca_count), ldap_csca_count_(ldap_csca_count),
          csca_discrepancy_(csca_discrepancy),
          db_mlsc_count_(db_mlsc_count), ldap_mlsc_count_(ldap_mlsc_count),
          mlsc_discrepancy_(mlsc_discrepancy),
          db_dsc_count_(db_dsc_count), ldap_dsc_count_(ldap_dsc_count),
          dsc_discrepancy_(dsc_discrepancy),
          db_dsc_nc_count_(db_dsc_nc_count), ldap_dsc_nc_count_(ldap_dsc_nc_count),
          dsc_nc_discrepancy_(dsc_nc_discrepancy),
          db_crl_count_(db_crl_count), ldap_crl_count_(ldap_crl_count),
          crl_discrepancy_(crl_discrepancy),
          total_discrepancy_(total_discrepancy),
          db_stored_in_ldap_count_(db_stored_in_ldap_count),
          ldap_total_entries_(ldap_total_entries),
          db_country_stats_(db_country_stats),
          ldap_country_stats_(ldap_country_stats),
          status_(status),
          error_message_(error_message),
          check_duration_ms_(check_duration_ms)
    {}
    /// @}

    /// @name Getters
    /// @{
    std::string getId() const { return id_; }
    std::chrono::system_clock::time_point getCheckedAt() const { return checked_at_; }
    /// @}

    /// @name Database statistics
    /// @{
    int getDbCscaCount() const { return db_csca_count_; }
    int getDbMlscCount() const { return db_mlsc_count_; }
    int getDbDscCount() const { return db_dsc_count_; }
    int getDbDscNcCount() const { return db_dsc_nc_count_; }
    int getDbCrlCount() const { return db_crl_count_; }
    int getDbStoredInLdapCount() const { return db_stored_in_ldap_count_; }
    /// @}

    /// @name LDAP statistics
    /// @{
    int getLdapCscaCount() const { return ldap_csca_count_; }
    int getLdapMlscCount() const { return ldap_mlsc_count_; }
    int getLdapDscCount() const { return ldap_dsc_count_; }
    int getLdapDscNcCount() const { return ldap_dsc_nc_count_; }
    int getLdapCrlCount() const { return ldap_crl_count_; }
    int getLdapTotalEntries() const { return ldap_total_entries_; }
    /// @}

    /// @name Discrepancies
    /// @{
    int getCscaDiscrepancy() const { return csca_discrepancy_; }
    int getMlscDiscrepancy() const { return mlsc_discrepancy_; }
    int getDscDiscrepancy() const { return dsc_discrepancy_; }
    int getDscNcDiscrepancy() const { return dsc_nc_discrepancy_; }
    int getCrlDiscrepancy() const { return crl_discrepancy_; }
    int getTotalDiscrepancy() const { return total_discrepancy_; }
    /// @}

    /// @name Country statistics (JSONB)
    /// @{
    std::optional<Json::Value> getDbCountryStats() const { return db_country_stats_; }
    std::optional<Json::Value> getLdapCountryStats() const { return ldap_country_stats_; }
    /// @}

    /// @name Status
    /// @{
    std::string getStatus() const { return status_; }
    std::optional<std::string> getErrorMessage() const { return error_message_; }
    int getCheckDurationMs() const { return check_duration_ms_; }
    /// @}

    /// @name Setters
    /// @{
    void setId(const std::string& id) { id_ = id; }
    void setCheckedAt(const std::chrono::system_clock::time_point& checked_at) {
        checked_at_ = checked_at;
    }
    void setStatus(const std::string& status) { status_ = status; }
    void setErrorMessage(const std::optional<std::string>& error_message) {
        error_message_ = error_message;
    }
    /// @}

    /**
     * @brief Convert to JSON representation
     */
    Json::Value toJson() const;

private:
    std::string id_;
    std::chrono::system_clock::time_point checked_at_;

    // Database counts
    int db_csca_count_ = 0;
    int db_mlsc_count_ = 0;
    int db_dsc_count_ = 0;
    int db_dsc_nc_count_ = 0;
    int db_crl_count_ = 0;
    int db_stored_in_ldap_count_ = 0;

    // LDAP counts
    int ldap_csca_count_ = 0;
    int ldap_mlsc_count_ = 0;
    int ldap_dsc_count_ = 0;
    int ldap_dsc_nc_count_ = 0;
    int ldap_crl_count_ = 0;
    int ldap_total_entries_ = 0;

    // Discrepancies
    int csca_discrepancy_ = 0;
    int mlsc_discrepancy_ = 0;
    int dsc_discrepancy_ = 0;
    int dsc_nc_discrepancy_ = 0;
    int crl_discrepancy_ = 0;
    int total_discrepancy_ = 0;

    // Country statistics (JSONB)
    std::optional<Json::Value> db_country_stats_;
    std::optional<Json::Value> ldap_country_stats_;

    // Status
    std::string status_ = "UNKNOWN";
    std::optional<std::string> error_message_;
    int check_duration_ms_ = 0;
};

} // namespace icao::relay::domain
