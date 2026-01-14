#include "reconciliation_engine.h"
#include <spdlog/spdlog.h>
#include <chrono>

namespace icao {
namespace sync {

ReconciliationEngine::ReconciliationEngine(const Config& config)
    : config_(config),
      ldapOps_(std::make_unique<LdapOperations>(config)) {
}

LDAP* ReconciliationEngine::connectToLdapWrite(std::string& errorMsg) const {
    std::string ldapUri = "ldap://" + config_.ldapWriteHost + ":" +
                         std::to_string(config_.ldapWritePort);

    LDAP* ld = nullptr;
    int rc = ldap_initialize(&ld, ldapUri.c_str());
    if (rc != LDAP_SUCCESS) {
        errorMsg = "LDAP connection failed: " + std::string(ldap_err2string(rc));
        return nullptr;
    }

    // Set LDAP version
    int version = LDAP_VERSION3;
    ldap_set_option(ld, LDAP_OPT_PROTOCOL_VERSION, &version);

    // Bind to LDAP with admin credentials
    berval cred;
    cred.bv_val = const_cast<char*>(config_.ldapBindPassword.c_str());
    cred.bv_len = config_.ldapBindPassword.length();

    rc = ldap_sasl_bind_s(ld, config_.ldapBindDn.c_str(), LDAP_SASL_SIMPLE,
                         &cred, nullptr, nullptr, nullptr);
    if (rc != LDAP_SUCCESS) {
        errorMsg = "LDAP bind failed: " + std::string(ldap_err2string(rc));
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        return nullptr;
    }

    spdlog::info("Connected to LDAP write host: {}", ldapUri);
    return ld;
}

std::vector<CertificateInfo> ReconciliationEngine::findMissingInLdap(
    PGconn* pgConn,
    const std::string& certType,
    int limit) const {

    std::vector<CertificateInfo> result;

    const char* query = R"(
        SELECT id, certificate_type, country_code, subject, issuer, certificate_data
        FROM certificate
        WHERE certificate_type = $1
          AND stored_in_ldap = FALSE
        ORDER BY id
        LIMIT $2
    )";

    std::string limitStr = std::to_string(limit);
    const char* paramValues[] = {certType.c_str(), limitStr.c_str()};

    PGresult* res = PQexecParams(pgConn, query, 2, nullptr, paramValues,
                                nullptr, nullptr, 0);

    if (PQresultStatus(res) == PGRES_TUPLES_OK) {
        int rows = PQntuples(res);
        for (int i = 0; i < rows; i++) {
            CertificateInfo cert;
            cert.id = PQgetvalue(res, i, 0);  // ID as string (UUID or integer)
            cert.certType = PQgetvalue(res, i, 1);
            cert.countryCode = PQgetvalue(res, i, 2);
            cert.subject = PQgetvalue(res, i, 3);
            cert.issuer = PQgetvalue(res, i, 4);

            // Get binary certificate data
            const char* certHex = PQgetvalue(res, i, 5);
            size_t certLen = PQgetlength(res, i, 5);
            if (certLen > 2 && certHex[0] == '\\' && certHex[1] == 'x') {
                // Parse hex format: \x414243...
                for (size_t j = 2; j < certLen; j += 2) {
                    if (j + 1 < certLen) {
                        unsigned char byte = (unsigned char)std::stoi(
                            std::string(certHex + j, 2), nullptr, 16);
                        cert.certData.push_back(byte);
                    }
                }
            }

            cert.ldapDn = ldapOps_->buildDn(cert.certType, cert.countryCode, cert.id);
            result.push_back(cert);
        }
    }

    PQclear(res);
    return result;
}

void ReconciliationEngine::markAsStoredInLdap(PGconn* pgConn, const std::string& certId) const {
    const char* query = "UPDATE certificate SET stored_in_ldap = TRUE WHERE id = $1";
    const char* paramValues[] = {certId.c_str()};

    PGresult* res = PQexecParams(pgConn, query, 1, nullptr, paramValues,
                                nullptr, nullptr, 0);
    PQclear(res);
}

void ReconciliationEngine::processCertificateType(
    PGconn* pgConn,
    LDAP* ld,
    const std::string& certType,
    bool dryRun,
    ReconciliationResult& result,
    int reconciliationId) const {

    spdlog::info("Processing {} certificates...", certType);

    // Find certificates missing in LDAP
    auto missingCerts = findMissingInLdap(pgConn, certType, config_.maxReconcileBatchSize);
    spdlog::info("Found {} {} certificates missing in LDAP", missingCerts.size(), certType);

    for (const auto& cert : missingCerts) {
        result.totalProcessed++;

        auto opStartTime = std::chrono::steady_clock::now();
        std::string errorMsg;
        bool success = false;

        if (dryRun) {
            spdlog::info("[DRY-RUN] Would add {} to LDAP: {} ({})",
                       certType, cert.subject, cert.ldapDn);
            success = true;
        } else {
            success = ldapOps_->addCertificate(ld, cert, errorMsg);
            if (success) {
                markAsStoredInLdap(pgConn, cert.id);
            }
        }

        auto opEndTime = std::chrono::steady_clock::now();
        int opDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            opEndTime - opStartTime).count();

        // Log operation to reconciliation_log table
        if (reconciliationId > 0) {
            logReconciliationOperation(
                pgConn, reconciliationId, "ADD", certType, cert,
                success ? "SUCCESS" : "FAILED", errorMsg, opDurationMs);
        }

        if (success) {
            result.successCount++;
            if (certType == "CSCA") result.cscaAdded++;
            else if (certType == "DSC") result.dscAdded++;
            else if (certType == "DSC_NC") result.dscNcAdded++;
        } else {
            result.failedCount++;
            result.status = "PARTIAL";

            ReconciliationFailure failure;
            failure.certType = certType;
            failure.operation = "ADD";
            failure.countryCode = cert.countryCode;
            failure.subject = cert.subject;
            failure.error = errorMsg;
            result.failures.push_back(failure);

            spdlog::error("Failed to add {} to LDAP: {} - {}",
                        certType, cert.subject, errorMsg);
        }
    }
}

ReconciliationResult ReconciliationEngine::performReconciliation(
    PGconn* pgConn,
    bool dryRun,
    const std::string& triggeredBy,
    int syncStatusId) {

    auto startTime = std::chrono::steady_clock::now();
    ReconciliationResult result;
    result.status = "COMPLETED";

    spdlog::info("Starting reconciliation (dryRun={}, triggeredBy={}, syncStatusId={})",
                dryRun, triggeredBy, syncStatusId);

    // Create reconciliation summary record
    int reconciliationId = createReconciliationSummary(pgConn, triggeredBy, dryRun, syncStatusId);
    if (reconciliationId == 0) {
        result.success = false;
        result.status = "FAILED";
        result.errorMessage = "Failed to create reconciliation_summary record";
        return result;
    }

    // Connect to LDAP (write host)
    std::string errorMsg;
    LDAP* ld = connectToLdapWrite(errorMsg);
    if (!ld) {
        result.success = false;
        result.status = "FAILED";
        result.errorMessage = errorMsg;
        spdlog::error("Reconciliation failed: {}", result.errorMessage);
        return result;
    }

    // Process each certificate type in order: CSCA, DSC, DSC_NC
    std::vector<std::string> certTypes = {"CSCA", "DSC", "DSC_NC"};

    for (const auto& certType : certTypes) {
        processCertificateType(pgConn, ld, certType, dryRun, result, reconciliationId);
    }

    // Cleanup LDAP connection
    ldap_unbind_ext_s(ld, nullptr, nullptr);

    // Calculate duration
    auto endTime = std::chrono::steady_clock::now();
    result.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime).count();

    result.success = (result.failedCount == 0);
    if (result.failedCount > 0 && result.successCount == 0) {
        result.status = "FAILED";
    }

    // Update reconciliation summary with final results
    if (reconciliationId > 0) {
        updateReconciliationSummary(pgConn, reconciliationId, result);
    }

    spdlog::info("Reconciliation completed: {} processed, {} succeeded, {} failed ({}ms)",
                result.totalProcessed, result.successCount, result.failedCount,
                result.durationMs);

    return result;
}

int ReconciliationEngine::createReconciliationSummary(
    PGconn* pgConn,
    const std::string& triggeredBy,
    bool dryRun,
    int syncStatusId) const {

    const char* query =
        "INSERT INTO reconciliation_summary "
        "(triggered_by, dry_run, sync_status_id, status) "
        "VALUES ($1, $2, $3, 'IN_PROGRESS') "
        "RETURNING id";

    const char* paramValues[3];
    paramValues[0] = triggeredBy.c_str();
    std::string dryRunStr = dryRun ? "true" : "false";
    paramValues[1] = dryRunStr.c_str();
    std::string syncStatusIdStr = std::to_string(syncStatusId);
    paramValues[2] = (syncStatusId > 0) ? syncStatusIdStr.c_str() : nullptr;

    PGresult* res = PQexecParams(pgConn, query, 3, nullptr,
                                paramValues, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        spdlog::error("Failed to create reconciliation_summary: {}",
                     PQerrorMessage(pgConn));
        PQclear(res);
        return 0;
    }

    int reconciliationId = std::atoi(PQgetvalue(res, 0, 0));
    PQclear(res);

    spdlog::debug("Created reconciliation_summary id={}", reconciliationId);
    return reconciliationId;
}

void ReconciliationEngine::updateReconciliationSummary(
    PGconn* pgConn,
    int reconciliationId,
    const ReconciliationResult& result) const {

    const char* query =
        "UPDATE reconciliation_summary SET "
        "completed_at = CURRENT_TIMESTAMP, "
        "status = $1, "
        "total_processed = $2, "
        "success_count = $3, "
        "failed_count = $4, "
        "csca_added = $5, "
        "csca_deleted = $6, "
        "dsc_added = $7, "
        "dsc_deleted = $8, "
        "dsc_nc_added = $9, "
        "dsc_nc_deleted = $10, "
        "crl_added = $11, "
        "crl_deleted = $12, "
        "duration_ms = $13, "
        "error_message = $14 "
        "WHERE id = $15";

    const char* paramValues[15];
    paramValues[0] = result.status.c_str();
    std::string totalProcessed = std::to_string(result.totalProcessed);
    paramValues[1] = totalProcessed.c_str();
    std::string successCount = std::to_string(result.successCount);
    paramValues[2] = successCount.c_str();
    std::string failedCount = std::to_string(result.failedCount);
    paramValues[3] = failedCount.c_str();
    std::string cscaAdded = std::to_string(result.cscaAdded);
    paramValues[4] = cscaAdded.c_str();
    std::string cscaDeleted = std::to_string(result.cscaDeleted);
    paramValues[5] = cscaDeleted.c_str();
    std::string dscAdded = std::to_string(result.dscAdded);
    paramValues[6] = dscAdded.c_str();
    std::string dscDeleted = std::to_string(result.dscDeleted);
    paramValues[7] = dscDeleted.c_str();
    std::string dscNcAdded = std::to_string(result.dscNcAdded);
    paramValues[8] = dscNcAdded.c_str();
    std::string dscNcDeleted = std::to_string(result.dscNcDeleted);
    paramValues[9] = dscNcDeleted.c_str();
    std::string crlAdded = std::to_string(result.crlAdded);
    paramValues[10] = crlAdded.c_str();
    std::string crlDeleted = std::to_string(result.crlDeleted);
    paramValues[11] = crlDeleted.c_str();
    std::string durationMs = std::to_string(result.durationMs);
    paramValues[12] = durationMs.c_str();
    paramValues[13] = result.errorMessage.empty() ? nullptr : result.errorMessage.c_str();
    std::string idStr = std::to_string(reconciliationId);
    paramValues[14] = idStr.c_str();

    PGresult* res = PQexecParams(pgConn, query, 15, nullptr,
                                paramValues, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("Failed to update reconciliation_summary: {}",
                     PQerrorMessage(pgConn));
    } else {
        spdlog::debug("Updated reconciliation_summary id={}", reconciliationId);
    }

    PQclear(res);
}

void ReconciliationEngine::logReconciliationOperation(
    PGconn* pgConn,
    int reconciliationId,
    const std::string& operation,
    const std::string& certType,
    const CertificateInfo& cert,
    const std::string& status,
    const std::string& errorMsg,
    int durationMs) const {

    const char* query =
        "INSERT INTO reconciliation_log "
        "(reconciliation_id, operation, cert_type, cert_id, "
        "country_code, subject, issuer, ldap_dn, status, error_message, duration_ms) "
        "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11)";

    const char* paramValues[11];
    std::string reconIdStr = std::to_string(reconciliationId);
    paramValues[0] = reconIdStr.c_str();
    paramValues[1] = operation.c_str();
    paramValues[2] = certType.c_str();
    paramValues[3] = cert.id.c_str();  // ID is already a string
    paramValues[4] = cert.countryCode.c_str();
    paramValues[5] = cert.subject.c_str();
    paramValues[6] = cert.issuer.c_str();
    paramValues[7] = cert.ldapDn.c_str();
    paramValues[8] = status.c_str();
    paramValues[9] = errorMsg.empty() ? nullptr : errorMsg.c_str();
    std::string durationStr = std::to_string(durationMs);
    paramValues[10] = durationStr.c_str();

    PGresult* res = PQexecParams(pgConn, query, 11, nullptr,
                                paramValues, nullptr, nullptr, 0);

    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::warn("Failed to log reconciliation operation: {}",
                    PQerrorMessage(pgConn));
    }

    PQclear(res);
}

} // namespace sync
} // namespace icao
