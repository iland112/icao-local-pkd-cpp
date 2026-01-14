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
            cert.id = std::stoi(PQgetvalue(res, i, 0));
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

void ReconciliationEngine::markAsStoredInLdap(PGconn* pgConn, int certId) const {
    const char* query = "UPDATE certificate SET stored_in_ldap = TRUE WHERE id = $1";
    std::string idStr = std::to_string(certId);
    const char* paramValues[] = {idStr.c_str()};

    PGresult* res = PQexecParams(pgConn, query, 1, nullptr, paramValues,
                                nullptr, nullptr, 0);
    PQclear(res);
}

void ReconciliationEngine::processCertificateType(
    PGconn* pgConn,
    LDAP* ld,
    const std::string& certType,
    bool dryRun,
    ReconciliationResult& result) const {

    spdlog::info("Processing {} certificates...", certType);

    // Find certificates missing in LDAP
    auto missingCerts = findMissingInLdap(pgConn, certType, config_.maxReconcileBatchSize);
    spdlog::info("Found {} {} certificates missing in LDAP", missingCerts.size(), certType);

    for (const auto& cert : missingCerts) {
        result.totalProcessed++;

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
    PGconn* pgConn, bool dryRun) {

    auto startTime = std::chrono::steady_clock::now();
    ReconciliationResult result;
    result.status = "COMPLETED";

    spdlog::info("Starting reconciliation (dryRun={})", dryRun);

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
        processCertificateType(pgConn, ld, certType, dryRun, result);
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

    spdlog::info("Reconciliation completed: {} processed, {} succeeded, {} failed ({}ms)",
                result.totalProcessed, result.successCount, result.failedCount,
                result.durationMs);

    return result;
}

} // namespace sync
} // namespace icao
