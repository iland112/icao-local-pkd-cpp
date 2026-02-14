#include "reconciliation_engine.h"
#include <spdlog/spdlog.h>
#include <chrono>

namespace icao {
namespace relay {

// Helper: Oracle returns all values as strings, so .asInt() fails.
static int getInt(const Json::Value& json, const std::string& field, int defaultValue = 0) {
    if (!json.isMember(field) || json[field].isNull()) return defaultValue;
    const auto& v = json[field];
    if (v.isInt()) return v.asInt();
    if (v.isUInt()) return static_cast<int>(v.asUInt());
    if (v.isString()) {
        try { return std::stoi(v.asString()); }
        catch (...) { return defaultValue; }
    }
    if (v.isDouble()) return static_cast<int>(v.asDouble());
    return defaultValue;
}

// Phase 6.4: Constructor now accepts IQueryExecutor* for database-agnostic operation
ReconciliationEngine::ReconciliationEngine(
    const Config& config,
    common::LdapConnectionPool* ldapPool,
    common::IQueryExecutor* queryExecutor)
    : config_(config),
      ldapPool_(ldapPool),
      queryExecutor_(queryExecutor),
      ldapOps_(std::make_unique<LdapOperations>(config)) {

    if (!ldapPool_) {
        throw std::runtime_error("ReconciliationEngine: ldapPool cannot be null");
    }
    if (!queryExecutor_) {
        throw std::runtime_error("ReconciliationEngine: queryExecutor cannot be null");
    }
    dbType_ = queryExecutor_->getDatabaseType();
}

// Helper: Get database-specific boolean literal for SQL WHERE clauses
std::string ReconciliationEngine::boolLiteral(bool value) const {
    if (dbType_ == "oracle") {
        return value ? "1" : "0";
    }
    return value ? "TRUE" : "FALSE";
}

// Helper: Parse hex-encoded binary data (\x414243... format)
// Both PostgreSQL bytea and Oracle BLOB (via OracleQueryExecutor) use this format
std::vector<unsigned char> ReconciliationEngine::parseHexBinary(const std::string& hexStr) {
    std::vector<unsigned char> result;

    if (hexStr.size() > 2 && hexStr[0] == '\\' && hexStr[1] == 'x') {
        // Parse hex format: \x414243...
        for (size_t j = 2; j < hexStr.size(); j += 2) {
            if (j + 1 < hexStr.size()) {
                unsigned char byte = (unsigned char)std::stoi(
                    hexStr.substr(j, 2), nullptr, 16);
                result.push_back(byte);
            }
        }
    }

    return result;
}

std::vector<CertificateInfo> ReconciliationEngine::findMissingInLdap(
    const std::string& certType,
    int limit) const {

    std::vector<CertificateInfo> result;

    try {
        // Phase 6.4: Use database-specific boolean literal for stored_in_ldap filter
        std::string query =
            "SELECT id, certificate_type, country_code, subject_dn, issuer_dn, "
            "fingerprint_sha256, certificate_data, is_self_signed "
            "FROM certificate "
            "WHERE certificate_type = $1 AND stored_in_ldap = " + boolLiteral(false) + " "
            "ORDER BY id "
            "LIMIT $2";

        std::vector<std::string> params = {certType, std::to_string(limit)};
        Json::Value rows = queryExecutor_->executeQuery(query, params);

        if (rows.empty()) {
            spdlog::info("Found 0 {} certificates missing in LDAP", certType);
            return result;
        }

        // Connect to LDAP for existence checks
        LDAP* ldRead = nullptr;
        int rc = ldap_initialize(&ldRead, ("ldap://" + config_.ldapWriteHost + ":" +
                                           std::to_string(config_.ldapWritePort)).c_str());

        if (rc != LDAP_SUCCESS) {
            spdlog::error("Failed to initialize LDAP for existence check: {}", ldap_err2string(rc));
            return result;
        }

        int version = LDAP_VERSION3;
        ldap_set_option(ldRead, LDAP_OPT_PROTOCOL_VERSION, &version);
        ldap_set_option(ldRead, LDAP_OPT_REFERRALS, LDAP_OPT_OFF);

        // Bind
        struct berval cred;
        cred.bv_val = const_cast<char*>(config_.ldapBindPassword.c_str());
        cred.bv_len = config_.ldapBindPassword.length();
        rc = ldap_sasl_bind_s(ldRead, config_.ldapBindDn.c_str(), LDAP_SASL_SIMPLE,
                             &cred, nullptr, nullptr, nullptr);

        if (rc != LDAP_SUCCESS) {
            spdlog::error("Failed to bind LDAP for existence check: {}", ldap_err2string(rc));
            ldap_unbind_ext_s(ldRead, nullptr, nullptr);
            return result;
        }

        for (const auto& row : rows) {
            CertificateInfo cert;
            cert.id = row["id"].asString();
            cert.certType = row["certificate_type"].asString();
            cert.countryCode = row["country_code"].asString();
            cert.subject = row["subject_dn"].asString();
            cert.issuer = row["issuer_dn"].asString();
            cert.fingerprint = row["fingerprint_sha256"].asString();

            // v2.2.2 FIX: Detect link certificates using DB is_self_signed field
            // (set by X509_NAME_cmp which is case-insensitive per RFC 5280)
            if (cert.certType == "CSCA") {
                bool isSelfSigned = row.get("is_self_signed", true).asBool();
                // Oracle returns "1"/"0" as string
                if (row["is_self_signed"].isString()) {
                    isSelfSigned = (row["is_self_signed"].asString() == "1" ||
                                    row["is_self_signed"].asString() == "true" ||
                                    row["is_self_signed"].asString() == "TRUE");
                }
                if (!isSelfSigned) {
                    cert.certType = "LC";
                    spdlog::debug("Detected link certificate: {} (is_self_signed=false)", cert.id);
                }
            }

            // Build DN with fingerprint
            cert.ldapDn = ldapOps_->buildDn(cert.certType, cert.countryCode, cert.fingerprint);

            // Check if entry exists in LDAP
            LDAPMessage* searchRes = nullptr;
            const char* attrs[] = {"dn", nullptr};
            struct timeval timeout = {5, 0};

            rc = ldap_search_ext_s(ldRead, cert.ldapDn.c_str(), LDAP_SCOPE_BASE,
                                  "(objectClass=*)", const_cast<char**>(attrs), 0,
                                  nullptr, nullptr, &timeout, 0, &searchRes);

            if (rc == LDAP_NO_SUCH_OBJECT) {
                // Entry does not exist in LDAP - parse binary certificate data
                std::string certHex = row["certificate_data"].asString();
                cert.certData = parseHexBinary(certHex);

                result.push_back(cert);

                if (result.size() >= static_cast<size_t>(limit)) {
                    if (searchRes) ldap_msgfree(searchRes);
                    break;
                }
            } else if (rc == LDAP_SUCCESS) {
                spdlog::debug("Certificate {} already exists in LDAP: {}",
                            cert.id, cert.ldapDn);
            } else {
                spdlog::warn("LDAP search error for {}: {}", cert.ldapDn, ldap_err2string(rc));
            }

            if (searchRes) ldap_msgfree(searchRes);
        }

        ldap_unbind_ext_s(ldRead, nullptr, nullptr);

    } catch (const std::exception& e) {
        spdlog::error("Failed to find missing {} in LDAP: {}", certType, e.what());
    }

    spdlog::info("Found {} {} certificates missing in LDAP (verified against actual LDAP state)",
                result.size(), certType);
    return result;
}

void ReconciliationEngine::markAsStoredInLdap(const std::string& certId) const {
    try {
        std::string query = "UPDATE certificate SET stored_in_ldap = " + boolLiteral(true) + " WHERE id = $1";
        queryExecutor_->executeCommand(query, {certId});
    } catch (const std::exception& e) {
        spdlog::error("Failed to mark certificate {} as stored in LDAP: {}", certId, e.what());
    }
}

void ReconciliationEngine::markCrlAsStoredInLdap(const std::string& crlId) const {
    try {
        std::string query = "UPDATE crl SET stored_in_ldap = " + boolLiteral(true) + " WHERE id = $1";
        queryExecutor_->executeCommand(query, {crlId});
    } catch (const std::exception& e) {
        spdlog::error("Failed to mark CRL {} as stored in LDAP: {}", crlId, e.what());
    }
}

void ReconciliationEngine::processCertificateType(
    LDAP* ld,
    const std::string& certType,
    bool dryRun,
    ReconciliationResult& result,
    const std::string& reconciliationId) const {

    spdlog::info("Processing {} certificates...", certType);

    auto missingCerts = findMissingInLdap(certType, config_.maxReconcileBatchSize);

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
                markAsStoredInLdap(cert.id);
            }
        }

        auto opEndTime = std::chrono::steady_clock::now();
        int opDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            opEndTime - opStartTime).count();

        if (!reconciliationId.empty()) {
            logReconciliationOperation(
                reconciliationId, "ADD", certType, cert,
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
    bool dryRun,
    const std::string& triggeredBy,
    int syncStatusId) {

    auto startTime = std::chrono::steady_clock::now();
    ReconciliationResult result;
    result.status = "COMPLETED";

    spdlog::info("Starting reconciliation (dryRun={}, triggeredBy={}, syncStatusId={})",
                dryRun, triggeredBy, syncStatusId);

    // Create reconciliation summary record
    std::string reconciliationId = createReconciliationSummary(triggeredBy, dryRun, syncStatusId);
    if (reconciliationId.empty()) {
        result.success = false;
        result.status = "FAILED";
        result.errorMessage = "Failed to create reconciliation_summary record";
        return result;
    }

    // v2.4.3: Acquire LDAP connection from pool (RAII - auto-release on scope exit)
    auto conn = ldapPool_->acquire();
    if (!conn.isValid()) {
        result.success = false;
        result.status = "FAILED";
        result.errorMessage = "Failed to acquire LDAP connection from pool";
        spdlog::error("Reconciliation failed: {}", result.errorMessage);
        return result;
    }

    LDAP* ld = conn.get();
    spdlog::info("Acquired LDAP connection from pool for reconciliation");

    // Process each certificate type in order: CSCA, DSC
    // Note: DSC_NC excluded - ICAO deprecated nc-data branch in 2021
    std::vector<std::string> certTypes = {"CSCA", "DSC"};

    for (const auto& certType : certTypes) {
        processCertificateType(ld, certType, dryRun, result, reconciliationId);
    }

    // v2.0.5: Process CRLs
    processCrls(ld, dryRun, result, reconciliationId);

    // Calculate duration
    auto endTime = std::chrono::steady_clock::now();
    result.durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime).count();

    result.success = (result.failedCount == 0);
    if (result.failedCount > 0 && result.successCount == 0) {
        result.status = "FAILED";
    }

    // Update reconciliation summary with final results
    if (!reconciliationId.empty()) {
        updateReconciliationSummary(reconciliationId, result);
    }

    spdlog::info("Reconciliation completed: {} processed, {} succeeded, {} failed ({}ms)",
                result.totalProcessed, result.successCount, result.failedCount,
                result.durationMs);

    return result;
}

std::string ReconciliationEngine::createReconciliationSummary(
    const std::string& triggeredBy,
    bool dryRun,
    int syncStatusId) const {

    try {
        // Phase 6.4: Generate ID using database-specific sequence (same pattern as ReconciliationRepository)
        std::string idQuery;
        if (dbType_ == "postgres") {
            idQuery = "SELECT nextval('reconciliation_summary_id_seq') as id";
        } else {
            idQuery = "SELECT SEQ_RECON_SUMMARY.NEXTVAL as id FROM DUAL";
        }

        Json::Value idResult = queryExecutor_->executeQuery(idQuery, {});
        if (idResult.empty()) {
            spdlog::error("Failed to generate reconciliation_summary ID");
            return "";
        }
        std::string generatedId = std::to_string(getInt(idResult[0], "id", 0));

        // Insert with generated ID (no RETURNING clause - works with both PostgreSQL and Oracle)
        std::string query =
            "INSERT INTO reconciliation_summary "
            "(id, triggered_by, dry_run, sync_status_id, status) "
            "VALUES ($1, $2, $3, $4, 'IN_PROGRESS')";

        // Oracle NUMBER(1) needs "1"/"0", PostgreSQL BOOLEAN needs "TRUE"/"FALSE"
        std::string dryRunStr = boolLiteral(dryRun);
        std::string syncStatusIdStr = (syncStatusId > 0) ? std::to_string(syncStatusId) : "";

        std::vector<std::string> params = {
            generatedId,
            triggeredBy,
            dryRunStr,
            syncStatusIdStr
        };

        queryExecutor_->executeCommand(query, params);

        spdlog::debug("Created reconciliation_summary id={}", generatedId);
        return generatedId;

    } catch (const std::exception& e) {
        spdlog::error("Failed to create reconciliation_summary: {}", e.what());
        return "";
    }
}

void ReconciliationEngine::updateReconciliationSummary(
    const std::string& reconciliationId,
    const ReconciliationResult& result) const {

    try {
        std::string query =
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

        std::vector<std::string> params = {
            result.status,
            std::to_string(result.totalProcessed),
            std::to_string(result.successCount),
            std::to_string(result.failedCount),
            std::to_string(result.cscaAdded),
            std::to_string(result.cscaDeleted),
            std::to_string(result.dscAdded),
            std::to_string(result.dscDeleted),
            std::to_string(result.dscNcAdded),
            std::to_string(result.dscNcDeleted),
            std::to_string(result.crlAdded),
            std::to_string(result.crlDeleted),
            std::to_string(result.durationMs),
            result.errorMessage,
            reconciliationId
        };

        queryExecutor_->executeCommand(query, params);
        spdlog::debug("Updated reconciliation_summary id={}", reconciliationId);

    } catch (const std::exception& e) {
        spdlog::error("Failed to update reconciliation_summary: {}", e.what());
    }
}

void ReconciliationEngine::logReconciliationOperation(
    const std::string& reconciliationId,
    const std::string& operation,
    const std::string& certType,
    const CertificateInfo& cert,
    const std::string& status,
    const std::string& errorMsg,
    int durationMs) const {

    try {
        // v2.0.5: Use cert_fingerprint instead of cert_id (UUID type incompatibility fix)
        std::string query =
            "INSERT INTO reconciliation_log "
            "(reconciliation_id, operation, cert_type, cert_fingerprint, "
            "country_code, subject, issuer, ldap_dn, status, error_message, duration_ms) "
            "VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11)";

        std::vector<std::string> params = {
            reconciliationId,
            operation,
            certType,
            cert.fingerprint,
            cert.countryCode,
            cert.subject,
            cert.issuer,
            cert.ldapDn,
            status,
            errorMsg,
            std::to_string(durationMs)
        };

        queryExecutor_->executeCommand(query, params);

    } catch (const std::exception& e) {
        spdlog::warn("Failed to log reconciliation operation: {}", e.what());
    }
}

// v2.0.5: Find CRLs missing in LDAP
std::vector<CrlInfo> ReconciliationEngine::findMissingCrlsInLdap(
    int limit) const {

    std::vector<CrlInfo> result;

    try {
        // Phase 6.4: Use database-specific boolean literal
        std::string query =
            "SELECT id, country_code, issuer_dn, fingerprint_sha256, crl_binary "
            "FROM crl "
            "WHERE stored_in_ldap = " + boolLiteral(false) + " "
            "ORDER BY id "
            "LIMIT $1";

        std::vector<std::string> params = {std::to_string(limit)};
        Json::Value rows = queryExecutor_->executeQuery(query, params);

        if (rows.empty()) {
            spdlog::info("Found 0 CRLs missing in LDAP");
            return result;
        }

        // Connect to LDAP for existence checks
        LDAP* ldRead = nullptr;
        int rc = ldap_initialize(&ldRead, ("ldap://" + config_.ldapWriteHost + ":" +
                                           std::to_string(config_.ldapWritePort)).c_str());

        if (rc != LDAP_SUCCESS) {
            spdlog::error("Failed to initialize LDAP for CRL existence check: {}", ldap_err2string(rc));
            return result;
        }

        int version = LDAP_VERSION3;
        ldap_set_option(ldRead, LDAP_OPT_PROTOCOL_VERSION, &version);
        ldap_set_option(ldRead, LDAP_OPT_REFERRALS, LDAP_OPT_OFF);

        // Bind
        struct berval cred;
        cred.bv_val = const_cast<char*>(config_.ldapBindPassword.c_str());
        cred.bv_len = config_.ldapBindPassword.length();
        rc = ldap_sasl_bind_s(ldRead, config_.ldapBindDn.c_str(), LDAP_SASL_SIMPLE,
                             &cred, nullptr, nullptr, nullptr);

        if (rc != LDAP_SUCCESS) {
            spdlog::error("Failed to bind to LDAP for CRL existence check: {}", ldap_err2string(rc));
            ldap_unbind_ext_s(ldRead, nullptr, nullptr);
            return result;
        }

        for (const auto& row : rows) {
            CrlInfo crl;
            crl.id = row["id"].asString();
            crl.countryCode = row["country_code"].asString();
            crl.issuerDn = row["issuer_dn"].asString();
            crl.fingerprint = row["fingerprint_sha256"].asString();

            // Parse binary CRL data from hex string
            std::string crlHex = row["crl_binary"].asString();
            crl.crlData = parseHexBinary(crlHex);

            // Build DN for LDAP existence check
            crl.ldapDn = ldapOps_->buildCrlDn(crl.countryCode, crl.fingerprint);

            // Check if CRL already exists in LDAP
            LDAPMessage* searchRes = nullptr;
            const char* attrs[] = {"dn", nullptr};
            struct timeval timeout = {5, 0};

            rc = ldap_search_ext_s(ldRead, crl.ldapDn.c_str(), LDAP_SCOPE_BASE,
                                  "(objectClass=*)", const_cast<char**>(attrs), 0,
                                  nullptr, nullptr, &timeout, 0, &searchRes);

            if (rc == LDAP_NO_SUCH_OBJECT) {
                result.push_back(crl);

                if (result.size() >= static_cast<size_t>(limit)) {
                    if (searchRes) ldap_msgfree(searchRes);
                    break;
                }
            } else if (rc == LDAP_SUCCESS) {
                spdlog::debug("CRL {} already exists in LDAP: {}",
                            crl.id, crl.ldapDn);
            } else {
                spdlog::warn("LDAP search error for CRL {}: {}", crl.ldapDn, ldap_err2string(rc));
            }

            if (searchRes) ldap_msgfree(searchRes);
        }

        ldap_unbind_ext_s(ldRead, nullptr, nullptr);

    } catch (const std::exception& e) {
        spdlog::error("Failed to find missing CRLs in LDAP: {}", e.what());
    }

    spdlog::info("Found {} CRLs missing in LDAP (verified against actual LDAP state)",
                result.size());
    return result;
}

// v2.0.5: Process CRLs
void ReconciliationEngine::processCrls(
    LDAP* ld,
    bool dryRun,
    ReconciliationResult& result,
    const std::string& reconciliationId) const {

    spdlog::info("Processing CRLs...");

    auto missingCrls = findMissingCrlsInLdap(config_.maxReconcileBatchSize);

    for (const auto& crl : missingCrls) {
        result.totalProcessed++;

        auto opStartTime = std::chrono::steady_clock::now();
        std::string errorMsg;
        bool success = false;

        if (dryRun) {
            spdlog::info("[DRY-RUN] Would add CRL to LDAP: {} ({})",
                       crl.issuerDn, crl.ldapDn);
            success = true;
        } else {
            success = ldapOps_->addCrl(ld, crl, errorMsg);
            if (success) {
                markCrlAsStoredInLdap(crl.id);
            }
        }

        auto opEndTime = std::chrono::steady_clock::now();
        int opDurationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            opEndTime - opStartTime).count();

        // Log CRL reconciliation operation
        CertificateInfo crlAsInfo;
        crlAsInfo.certType = "CRL";
        crlAsInfo.countryCode = crl.countryCode;
        crlAsInfo.subject = crl.issuerDn;
        crlAsInfo.issuer = crl.issuerDn;
        crlAsInfo.fingerprint = crl.fingerprint;
        logReconciliationOperation(
            reconciliationId, "ADD", "CRL", crlAsInfo,
            success ? "SUCCESS" : "FAILED", errorMsg, opDurationMs);

        if (success) {
            result.successCount++;
            result.crlAdded++;
        } else {
            result.failedCount++;
            result.status = "PARTIAL";

            ReconciliationFailure failure;
            failure.certType = "CRL";
            failure.operation = "ADD";
            failure.countryCode = crl.countryCode;
            failure.subject = crl.issuerDn;
            failure.error = errorMsg;
            result.failures.push_back(failure);

            spdlog::error("Failed to add CRL to LDAP: {} - {}",
                        crl.issuerDn, errorMsg);
        }
    }
}

} // namespace relay
} // namespace icao
