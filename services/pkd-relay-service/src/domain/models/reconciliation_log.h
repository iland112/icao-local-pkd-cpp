#pragma once

#include <string>
#include <chrono>
#include <optional>
#include <json/json.h>

namespace icao::relay::domain {

/**
 * @brief Domain model for reconciliation_log table
 *
 * Represents a single certificate/CRL operation during reconciliation.
 * Tracks individual sync operations for debugging and auditing.
 */
class ReconciliationLog {
public:
    // Constructors
    ReconciliationLog() = default;

    ReconciliationLog(
        const std::string& id,
        const std::string& reconciliation_id,
        const std::chrono::system_clock::time_point& created_at,
        const std::string& cert_fingerprint,
        const std::string& cert_type,
        const std::string& country_code,
        const std::string& action,
        const std::string& result,
        const std::optional<std::string>& error_message
    )
        : id_(id), reconciliation_id_(reconciliation_id),
          created_at_(created_at),
          cert_fingerprint_(cert_fingerprint),
          cert_type_(cert_type),
          country_code_(country_code),
          action_(action),
          result_(result),
          error_message_(error_message)
    {}

    // Getters
    std::string getId() const { return id_; }
    std::string getReconciliationId() const { return reconciliation_id_; }
    std::chrono::system_clock::time_point getCreatedAt() const { return created_at_; }
    std::string getCertFingerprint() const { return cert_fingerprint_; }
    std::string getCertType() const { return cert_type_; }
    std::string getCountryCode() const { return country_code_; }
    std::string getAction() const { return action_; }
    std::string getResult() const { return result_; }
    std::optional<std::string> getErrorMessage() const { return error_message_; }

    // Setters
    void setId(const std::string& id) { id_ = id; }
    void setReconciliationId(const std::string& reconciliation_id) { reconciliation_id_ = reconciliation_id; }
    void setResult(const std::string& result) { result_ = result; }
    void setErrorMessage(const std::optional<std::string>& error_message) {
        error_message_ = error_message;
    }

    /**
     * @brief Convert to JSON representation
     */
    Json::Value toJson() const;

private:
    std::string id_;
    std::string reconciliation_id_;
    std::chrono::system_clock::time_point created_at_;
    std::string cert_fingerprint_;
    std::string cert_type_;       // CSCA, MLSC, DSC, DSC_NC, CRL
    std::string country_code_;
    std::string action_;           // SYNC_TO_LDAP, DELETE_FROM_LDAP, SKIP
    std::string result_;           // SUCCESS, FAILED
    std::optional<std::string> error_message_;
};

} // namespace icao::relay::domain
