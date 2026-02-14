/**
 * @file reconciliation_service.h
 * @brief Service layer for DB-LDAP reconciliation operations
 */
#pragma once

#include "../repositories/reconciliation_repository.h"
#include "../repositories/certificate_repository.h"
#include "../repositories/crl_repository.h"
#include <memory>
#include <json/json.h>

namespace icao::relay::services {

/**
 * @brief Service layer for reconciliation operations
 *
 * Orchestrates DB-LDAP reconciliation process with business logic.
 * Uses Repository pattern for database access.
 */
class ReconciliationService {
public:
    /**
     * @brief Constructor with dependency injection
     * @param reconciliationRepo ReconciliationRepository instance
     * @param certificateRepo CertificateRepository instance
     * @param crlRepo CrlRepository instance
     */
    ReconciliationService(
        std::shared_ptr<repositories::ReconciliationRepository> reconciliationRepo,
        std::shared_ptr<repositories::CertificateRepository> certificateRepo,
        std::shared_ptr<repositories::CrlRepository> crlRepo
    );

    /**
     * @brief Start a new reconciliation process
     * @param triggeredBy Who triggered the reconciliation (user/auto)
     * @param dryRun Whether this is a dry run (no actual changes)
     * @return JSON response with reconciliation ID
     */
    Json::Value startReconciliation(const std::string& triggeredBy, bool dryRun = false);

    /**
     * @brief Record a reconciliation log entry
     * @param reconciliationId Reconciliation ID
     * @param certFingerprint Certificate/CRL fingerprint
     * @param certType Certificate type (CSCA, MLSC, DSC, DSC_NC, CRL)
     * @param countryCode Country code
     * @param action Action taken (SYNC_TO_LDAP, DELETE_FROM_LDAP, SKIP)
     * @param result Result (SUCCESS, FAILED)
     * @param errorMessage Optional error message
     * @return true if logged successfully
     */
    bool logReconciliationOperation(
        const std::string& reconciliationId,
        const std::string& certFingerprint,
        const std::string& certType,
        const std::string& countryCode,
        const std::string& action,
        const std::string& result,
        const std::string& errorMessage = ""
    );

    /**
     * @brief Complete a reconciliation process
     * @param reconciliationId Reconciliation ID
     * @param status Final status (COMPLETED, FAILED, PARTIAL)
     * @param summary Reconciliation summary with counters
     * @return JSON response with completion status
     */
    Json::Value completeReconciliation(
        const std::string& reconciliationId,
        const std::string& status,
        const domain::ReconciliationSummary& summary
    );

    /**
     * @brief Get reconciliation history with pagination
     * @param limit Maximum number of results (default: 50)
     * @param offset Number of results to skip (default: 0)
     * @return JSON response with history array
     */
    Json::Value getReconciliationHistory(int limit = 50, int offset = 0);

    /**
     * @brief Get detailed reconciliation info (summary + logs)
     * @param reconciliationId Reconciliation ID
     * @param logLimit Maximum log entries to return (default: 1000)
     * @param logOffset Log offset for pagination (default: 0)
     * @return JSON response with summary and logs
     */
    Json::Value getReconciliationDetails(
        const std::string& reconciliationId,
        int logLimit = 1000,
        int logOffset = 0
    );

    /**
     * @brief Get reconciliation statistics
     * @return JSON response with statistics
     */
    Json::Value getReconciliationStatistics();

private:
    /**
     * @brief Convert ReconciliationSummary domain object to JSON
     * @param summary Domain object
     * @return JSON representation
     */
    Json::Value summaryToJson(const domain::ReconciliationSummary& summary);

    /**
     * @brief Convert ReconciliationLog domain object to JSON
     * @param log Domain object
     * @return JSON representation
     */
    Json::Value logToJson(const domain::ReconciliationLog& log);

    std::shared_ptr<repositories::ReconciliationRepository> reconciliationRepo_;
    std::shared_ptr<repositories::CertificateRepository> certificateRepo_;
    std::shared_ptr<repositories::CrlRepository> crlRepo_;
};

} // namespace icao::relay::services
