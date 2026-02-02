/**
 * @file pa_verification_repository.cpp
 * @brief Implementation of PaVerificationRepository
 */

#include "pa_verification_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>

namespace repositories {

PaVerificationRepository::PaVerificationRepository(PGconn* conn)
    : dbConn_(conn)
{
    if (!dbConn_) {
        throw std::invalid_argument("Database connection cannot be null");
    }

    if (PQstatus(dbConn_) != CONNECTION_OK) {
        throw std::invalid_argument("Database connection is not in OK state");
    }

    spdlog::debug("PaVerificationRepository initialized");
}

// ==========================================================================
// CRUD Operations
// ==========================================================================

std::string PaVerificationRepository::insert(const domain::models::PaVerification& verification) {
    spdlog::debug("Inserting PA verification record");

    const char* query =
        "INSERT INTO pa_verification ("
        "issuing_country, document_number, verification_status, sod_hash, sod_binary, "
        "dsc_subject_dn, dsc_serial_number, dsc_issuer_dn, dsc_fingerprint, "
        "csca_subject_dn, csca_fingerprint, "
        "trust_chain_valid, trust_chain_message, "
        "sod_signature_valid, sod_signature_message, "
        "dg_hashes_valid, dg_hashes_message, "
        "crl_status, crl_message, "
        "verification_message, "
        "client_ip, user_agent"
        ") VALUES ("
        "$1, $2, $3, $4, $5, "
        "$6, $7, $8, $9, "
        "$10, $11, "
        "$12, $13, "
        "$14, $15, "
        "$16, $17, "
        "$18, $19, "
        "$20, "
        "$21, $22"
        ") RETURNING id";

    std::vector<std::string> params;
    // Match new INSERT query column order
    params.push_back(verification.countryCode);           // $1: issuing_country
    params.push_back(verification.documentNumber);        // $2: document_number
    params.push_back(verification.verificationStatus);    // $3: verification_status
    params.push_back(verification.sodHash);               // $4: sod_hash
    params.push_back("");                                 // $5: sod_binary (TODO: add to domain model)

    params.push_back(verification.dscSubject);            // $6: dsc_subject_dn
    params.push_back(verification.dscSerialNumber);       // $7: dsc_serial_number
    params.push_back(verification.dscIssuer);             // $8: dsc_issuer_dn
    params.push_back("");                                 // $9: dsc_fingerprint (TODO: add to domain model)

    params.push_back(verification.cscaSubject);           // $10: csca_subject_dn
    params.push_back("");                                 // $11: csca_fingerprint (TODO: add to domain model)

    params.push_back(verification.certificateChainValid ? "true" : "false");  // $12: trust_chain_valid
    params.push_back("");                                 // $13: trust_chain_message (TODO: add to domain model)

    params.push_back(verification.sodSignatureValid ? "true" : "false");      // $14: sod_signature_valid
    params.push_back("");                                 // $15: sod_signature_message (TODO: add to domain model)

    params.push_back(verification.dataGroupsValid ? "true" : "false");        // $16: dg_hashes_valid
    params.push_back("");                                 // $17: dg_hashes_message (TODO: add to domain model)

    params.push_back(verification.crlStatus);             // $18: crl_status
    params.push_back(verification.crlMessage.value_or(""));  // $19: crl_message

    params.push_back(verification.validationErrors.value_or(""));  // $20: verification_message

    params.push_back(verification.ipAddress.value_or(""));    // $21: client_ip
    params.push_back(verification.userAgent.value_or(""));    // $22: user_agent

    PGresult* res = executeParamQuery(query, params);

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::string error = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("Failed to insert PA verification: " + error);
    }

    std::string id = PQgetvalue(res, 0, 0);
    PQclear(res);

    spdlog::info("PA verification inserted with ID: {}", id);
    return id;
}

Json::Value PaVerificationRepository::findById(const std::string& id) {
    spdlog::debug("Finding PA verification by ID: {}", id);

    const char* query =
        "SELECT id, document_number, issuing_country, verification_status, sod_hash, "
        "dsc_subject_dn, dsc_serial_number, dsc_issuer_dn, dsc_fingerprint, "
        "csca_subject_dn, csca_fingerprint, "
        "trust_chain_valid, trust_chain_message, "
        "sod_signature_valid, sod_signature_message, "
        "dg_hashes_valid, dg_hashes_message, "
        "crl_status, crl_message, "
        "verification_message, "
        "request_timestamp, completed_timestamp, client_ip, user_agent "
        "FROM pa_verification WHERE id = $1";

    std::vector<std::string> params = {id};
    PGresult* res = executeParamQuery(query, params);

    Json::Value result;
    if (PQntuples(res) > 0) {
        Json::Value jsonArray = pgResultToJson(res);
        result = jsonArray[Json::ArrayIndex(0)];
    }

    PQclear(res);
    return result;
}

Json::Value PaVerificationRepository::findAll(
    int limit,
    int offset,
    const std::string& status,
    const std::string& countryCode)
{
    spdlog::debug("Finding all PA verifications (limit: {}, offset: {}, status: {}, country: {})",
        limit, offset, status, countryCode);

    std::vector<std::string> params;
    std::string whereClause = buildWhereClause(status, countryCode, params);

    // Count query
    std::ostringstream countQuery;
    countQuery << "SELECT COUNT(*) FROM pa_verification";
    if (!whereClause.empty()) {
        countQuery << " WHERE " << whereClause;
    }

    PGresult* countRes = params.empty() ?
        executeQuery(countQuery.str()) :
        executeParamQuery(countQuery.str(), params);

    int total = 0;
    if (PQntuples(countRes) > 0) {
        total = std::atoi(PQgetvalue(countRes, 0, 0));
    }
    PQclear(countRes);

    // Data query
    std::ostringstream dataQuery;
    dataQuery << "SELECT id, document_number, issuing_country, verification_status, sod_hash, "
              << "dsc_subject_dn, dsc_serial_number, dsc_fingerprint, "
              << "csca_subject_dn, csca_fingerprint, "
              << "trust_chain_valid, sod_signature_valid, dg_hashes_valid, "
              << "crl_status, crl_message, "
              << "verification_message, request_timestamp "
              << "FROM pa_verification";

    if (!whereClause.empty()) {
        dataQuery << " WHERE " << whereClause;
    }

    dataQuery << " ORDER BY request_timestamp DESC";

    // Add LIMIT and OFFSET
    int paramCount = params.size() + 1;
    dataQuery << " LIMIT $" << paramCount;
    paramCount++;
    dataQuery << " OFFSET $" << paramCount;
    params.push_back(std::to_string(limit));
    params.push_back(std::to_string(offset));

    PGresult* dataRes = executeParamQuery(dataQuery.str(), params);
    Json::Value data = pgResultToJson(dataRes);
    PQclear(dataRes);

    // Build response
    Json::Value response;
    response["success"] = true;
    response["data"] = data;
    response["total"] = total;
    response["limit"] = limit;
    response["offset"] = offset;
    response["page"] = offset / limit;
    response["size"] = data.size();

    return response;
}

Json::Value PaVerificationRepository::getStatistics() {
    spdlog::debug("Getting PA verification statistics");

    Json::Value stats;

    // Total verifications
    const char* totalQuery = "SELECT COUNT(*) FROM pa_verification";
    PGresult* totalRes = executeQuery(totalQuery);
    stats["totalVerifications"] = std::atoi(PQgetvalue(totalRes, 0, 0));
    PQclear(totalRes);

    // By status
    const char* statusQuery =
        "SELECT verification_status, COUNT(*) "
        "FROM pa_verification "
        "GROUP BY verification_status";
    PGresult* statusRes = executeQuery(statusQuery);

    Json::Value byStatus;
    for (int i = 0; i < PQntuples(statusRes); i++) {
        std::string status = PQgetvalue(statusRes, i, 0);
        int count = std::atoi(PQgetvalue(statusRes, i, 1));
        byStatus[status] = count;
    }
    PQclear(statusRes);
    stats["byStatus"] = byStatus;

    // By country
    const char* countryQuery =
        "SELECT issuing_country, COUNT(*) "
        "FROM pa_verification "
        "GROUP BY issuing_country "
        "ORDER BY COUNT(*) DESC "
        "LIMIT 10";
    PGresult* countryRes = executeQuery(countryQuery);

    Json::Value byCountry = Json::arrayValue;
    for (int i = 0; i < PQntuples(countryRes); i++) {
        Json::Value item;
        item["country"] = PQgetvalue(countryRes, i, 0);
        item["count"] = std::atoi(PQgetvalue(countryRes, i, 1));
        byCountry.append(item);
    }
    PQclear(countryRes);
    stats["topCountries"] = byCountry;

    // Success rate
    const char* successQuery =
        "SELECT "
        "CAST(SUM(CASE WHEN verification_status = 'VALID' THEN 1 ELSE 0 END) AS FLOAT) / "
        "NULLIF(COUNT(*), 0) * 100 AS success_rate "
        "FROM pa_verification";
    PGresult* successRes = executeQuery(successQuery);
    if (PQntuples(successRes) > 0 && !PQgetisnull(successRes, 0, 0)) {
        stats["successRate"] = std::atof(PQgetvalue(successRes, 0, 0));
    } else {
        stats["successRate"] = 0.0;
    }
    PQclear(successRes);

    Json::Value response;
    response["success"] = true;
    response["data"] = stats;

    return response;
}

bool PaVerificationRepository::deleteById(const std::string& id) {
    spdlog::debug("Deleting PA verification by ID: {}", id);

    const char* query = "DELETE FROM pa_verification WHERE id = $1";
    std::vector<std::string> params = {id};

    PGresult* res = executeParamQuery(query, params);
    int affectedRows = std::atoi(PQcmdTuples(res));
    PQclear(res);

    spdlog::info("Deleted {} PA verification record(s)", affectedRows);
    return affectedRows > 0;
}

bool PaVerificationRepository::updateStatus(const std::string& id, const std::string& status) {
    spdlog::debug("Updating PA verification status: ID={}, status={}", id, status);

    const char* query =
        "UPDATE pa_verification "
        "SET verification_status = $1, completed_timestamp = NOW() "
        "WHERE id = $2";

    std::vector<std::string> params = {status, id};
    PGresult* res = executeParamQuery(query, params);
    int affectedRows = std::atoi(PQcmdTuples(res));
    PQclear(res);

    return affectedRows > 0;
}

// ==========================================================================
// Helper Methods
// ==========================================================================

PGresult* PaVerificationRepository::executeParamQuery(
    const std::string& query,
    const std::vector<std::string>& params)
{
    std::vector<const char*> paramValues;
    for (const auto& param : params) {
        paramValues.push_back(param.c_str());
    }

    PGresult* res = PQexecParams(
        dbConn_,
        query.c_str(),
        paramValues.size(),
        nullptr,
        paramValues.data(),
        nullptr,
        nullptr,
        0
    );

    if (!res) {
        throw std::runtime_error("PQexecParams returned nullptr");
    }

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        std::string error = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("Query execution failed: " + error);
    }

    return res;
}

PGresult* PaVerificationRepository::executeQuery(const std::string& query) {
    PGresult* res = PQexec(dbConn_, query.c_str());

    if (!res) {
        throw std::runtime_error("PQexec returned nullptr");
    }

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        std::string error = PQresultErrorMessage(res);
        PQclear(res);
        throw std::runtime_error("Query execution failed: " + error);
    }

    return res;
}

Json::Value PaVerificationRepository::pgResultToJson(PGresult* res) {
    Json::Value array = Json::arrayValue;

    int rows = PQntuples(res);
    int cols = PQnfields(res);

    for (int i = 0; i < rows; i++) {
        Json::Value row;

        for (int j = 0; j < cols; j++) {
            const char* colName = PQfname(res, j);

            if (PQgetisnull(res, i, j)) {
                row[colName] = Json::Value::null;
            } else {
                const char* value = PQgetvalue(res, i, j);

                // Handle boolean fields
                if (std::string(colName).find("_valid") != std::string::npos ||
                    std::string(colName).find("_checked") != std::string::npos ||
                    std::string(colName).find("_expired") != std::string::npos ||
                    std::string(colName) == "revoked") {
                    row[colName] = (value[0] == 't' || value[0] == '1');
                }
                // Handle metadata JSON
                else if (std::string(colName) == "metadata") {
                    Json::CharReaderBuilder builder;
                    Json::Value jsonValue;
                    std::string errors;
                    std::istringstream iss(value);
                    if (Json::parseFromStream(builder, iss, &jsonValue, &errors)) {
                        row[colName] = jsonValue;
                    } else {
                        row[colName] = value;
                    }
                }
                else {
                    row[colName] = value;
                }
            }
        }

        array.append(row);
    }

    return array;
}

domain::models::PaVerification PaVerificationRepository::resultToVerification(PGresult* res, int row) {
    domain::models::PaVerification pv;

    pv.id = PQgetvalue(res, row, PQfnumber(res, "id"));
    pv.documentNumber = PQgetvalue(res, row, PQfnumber(res, "document_number"));
    pv.countryCode = PQgetvalue(res, row, PQfnumber(res, "issuing_country"));
    pv.verificationStatus = PQgetvalue(res, row, PQfnumber(res, "verification_status"));

    // ... (additional field mapping as needed)

    return pv;
}

std::string PaVerificationRepository::buildWhereClause(
    const std::string& status,
    const std::string& countryCode,
    std::vector<std::string>& params)
{
    std::ostringstream where;
    int paramCount = 1;

    if (!status.empty()) {
        if (paramCount > 1) where << " AND ";
        where << "verification_status = $" << paramCount++;
        params.push_back(status);
    }

    if (!countryCode.empty()) {
        if (paramCount > 1) where << " AND ";
        where << "issuing_country = $" << paramCount++;
        params.push_back(countryCode);
    }

    return where.str();
}

} // namespace repositories
