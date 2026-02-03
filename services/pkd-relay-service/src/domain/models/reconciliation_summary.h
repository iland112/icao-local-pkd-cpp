#pragma once

#include <string>
#include <chrono>
#include <optional>
#include <json/json.h>

namespace icao::relay::domain {

/**
 * @brief Domain model for reconciliation_summary table
 *
 * Represents a single reconciliation run that synchronizes
 * certificates and CRLs from PostgreSQL to LDAP.
 */
class ReconciliationSummary {
public:
    // Constructors
    ReconciliationSummary() = default;

    ReconciliationSummary(
        int id,
        const std::string& triggered_by,
        const std::chrono::system_clock::time_point& triggered_at,
        const std::optional<std::chrono::system_clock::time_point>& completed_at,
        const std::string& status,
        bool dry_run,
        int success_count, int failed_count,
        int csca_added, int csca_deleted,
        int dsc_added, int dsc_deleted,
        int dsc_nc_added, int dsc_nc_deleted,
        int crl_added, int crl_deleted,
        int total_added,
        int duration_ms,
        const std::optional<std::string>& error_message,
        const std::optional<int>& sync_status_id
    )
        : id_(id), triggered_by_(triggered_by),
          triggered_at_(triggered_at), completed_at_(completed_at),
          status_(status), dry_run_(dry_run),
          success_count_(success_count), failed_count_(failed_count),
          csca_added_(csca_added), csca_deleted_(csca_deleted),
          dsc_added_(dsc_added), dsc_deleted_(dsc_deleted),
          dsc_nc_added_(dsc_nc_added), dsc_nc_deleted_(dsc_nc_deleted),
          crl_added_(crl_added), crl_deleted_(crl_deleted),
          total_added_(total_added),
          duration_ms_(duration_ms),
          error_message_(error_message),
          sync_status_id_(sync_status_id)
    {}

    // Getters
    int getId() const { return id_; }
    std::string getTriggeredBy() const { return triggered_by_; }
    std::chrono::system_clock::time_point getTriggeredAt() const { return triggered_at_; }
    std::optional<std::chrono::system_clock::time_point> getCompletedAt() const {
        return completed_at_;
    }
    std::string getStatus() const { return status_; }
    bool isDryRun() const { return dry_run_; }

    // Success/failure counts
    int getSuccessCount() const { return success_count_; }
    int getFailedCount() const { return failed_count_; }

    // Certificate additions
    int getCscaAdded() const { return csca_added_; }
    int getDscAdded() const { return dsc_added_; }
    int getDscNcAdded() const { return dsc_nc_added_; }
    int getCrlAdded() const { return crl_added_; }
    int getTotalAdded() const { return total_added_; }

    // Certificate deletions
    int getCscaDeleted() const { return csca_deleted_; }
    int getDscDeleted() const { return dsc_deleted_; }
    int getDscNcDeleted() const { return dsc_nc_deleted_; }
    int getCrlDeleted() const { return crl_deleted_; }

    // Metadata
    int getDurationMs() const { return duration_ms_; }
    std::optional<std::string> getErrorMessage() const { return error_message_; }
    std::optional<int> getSyncStatusId() const { return sync_status_id_; }

    // Setters
    void setId(int id) { id_ = id; }
    void setCompletedAt(const std::chrono::system_clock::time_point& completed_at) {
        completed_at_ = completed_at;
    }
    void setStatus(const std::string& status) { status_ = status; }
    void setDurationMs(int duration_ms) { duration_ms_ = duration_ms; }
    void setErrorMessage(const std::optional<std::string>& error_message) {
        error_message_ = error_message;
    }

    // Update counters
    void incrementSuccessCount() { success_count_++; }
    void incrementFailedCount() { failed_count_++; }
    void incrementCscaAdded() { csca_added_++; total_added_++; }
    void incrementDscAdded() { dsc_added_++; total_added_++; }
    void incrementDscNcAdded() { dsc_nc_added_++; total_added_++; }
    void incrementCrlAdded() { crl_added_++; total_added_++; }

    /**
     * @brief Convert to JSON representation
     */
    Json::Value toJson() const;

private:
    int id_ = 0;
    std::string triggered_by_;
    std::chrono::system_clock::time_point triggered_at_;
    std::optional<std::chrono::system_clock::time_point> completed_at_;
    std::string status_ = "IN_PROGRESS";
    bool dry_run_ = false;

    // Success/failure counts
    int success_count_ = 0;
    int failed_count_ = 0;

    // Certificate additions
    int csca_added_ = 0;
    int dsc_added_ = 0;
    int dsc_nc_added_ = 0;
    int crl_added_ = 0;
    int total_added_ = 0;

    // Certificate deletions
    int csca_deleted_ = 0;
    int dsc_deleted_ = 0;
    int dsc_nc_deleted_ = 0;
    int crl_deleted_ = 0;

    // Metadata
    int duration_ms_ = 0;
    std::optional<std::string> error_message_;
    std::optional<int> sync_status_id_;
};

} // namespace icao::relay::domain
