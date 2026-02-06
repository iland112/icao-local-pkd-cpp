#include "certificate_repository.h"
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <sstream>

namespace icao::relay::repositories {

CertificateRepository::CertificateRepository(common::IQueryExecutor* executor)
    : queryExecutor_(executor)
{
    if (!queryExecutor_) {
        throw std::invalid_argument("CertificateRepository: queryExecutor cannot be nullptr");
    }

    spdlog::debug("[CertificateRepository] Initialized (DB type: {})",
        queryExecutor_->getDatabaseType());
}

int CertificateRepository::countByType(const std::string& certificateType) {
    try {
        const char* query = "SELECT COUNT(*) FROM certificate WHERE certificate_type = $1";

        std::vector<std::string> params = {certificateType};

        Json::Value result = queryExecutor_->executeScalar(query, params);
        return result.asInt();

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] Exception in countByType(): {}", e.what());
        return 0;
    }
}

std::vector<domain::Certificate> CertificateRepository::findNotInLdap(
    const std::string& certificateType,
    int limit
) {
    std::vector<domain::Certificate> results;

    try {
        std::string query =
            "SELECT id, fingerprint_sha256, certificate_type, country_code, "
            "subject_dn, issuer_dn, stored_in_ldap "
            "FROM certificate "
            "WHERE stored_in_ldap = FALSE";

        std::vector<std::string> params;
        int paramIndex = 1;

        // Add certificate type filter if provided
        if (!certificateType.empty()) {
            query += " AND certificate_type = $" + std::to_string(paramIndex++);
            params.push_back(certificateType);
        }

        // Add ORDER BY and LIMIT
        query += " ORDER BY created_at ASC LIMIT $" + std::to_string(paramIndex);
        params.push_back(std::to_string(limit));

        Json::Value result = queryExecutor_->executeQuery(query, params);

        for (const auto& row : result) {
            results.push_back(jsonToCertificate(row));
        }

        spdlog::debug("[CertificateRepository] Found {} certificates not in LDAP", results.size());
        return results;

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] Exception in findNotInLdap(): {}", e.what());
        return results;
    }
}

int CertificateRepository::markStoredInLdap(const std::vector<std::string>& fingerprints) {
    if (fingerprints.empty()) {
        return 0;
    }

    try {
        // Build parameterized query with IN clause
        // UPDATE certificate SET stored_in_ldap = TRUE WHERE fingerprint_sha256 IN ($1, $2, ...)
        std::ostringstream queryBuilder;
        queryBuilder << "UPDATE certificate SET stored_in_ldap = TRUE WHERE fingerprint_sha256 IN (";

        for (size_t i = 0; i < fingerprints.size(); i++) {
            if (i > 0) {
                queryBuilder << ", ";
            }
            queryBuilder << "$" << (i + 1);
        }
        queryBuilder << ")";

        std::string query = queryBuilder.str();

        // Convert fingerprints to std::vector<std::string> for Query Executor
        std::vector<std::string> params(fingerprints.begin(), fingerprints.end());

        int count = queryExecutor_->executeCommand(query, params);

        spdlog::debug("[CertificateRepository] Marked {} certificates as stored in LDAP", count);
        return count;

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] Exception in markStoredInLdap(batch): {}", e.what());
        return 0;
    }
}

bool CertificateRepository::markStoredInLdap(const std::string& fingerprint) {
    try {
        const char* query =
            "UPDATE certificate SET stored_in_ldap = TRUE WHERE fingerprint_sha256 = $1";

        std::vector<std::string> params = {fingerprint};

        int rowsAffected = queryExecutor_->executeCommand(query, params);
        return (rowsAffected > 0);

    } catch (const std::exception& e) {
        spdlog::error("[CertificateRepository] Exception in markStoredInLdap(single): {}", e.what());
        return false;
    }
}

domain::Certificate CertificateRepository::jsonToCertificate(const Json::Value& row) {
    // Parse fields from JSON row
    std::string id = row["id"].asString();
    std::string fingerprintSha256 = row["fingerprint_sha256"].asString();
    std::string certificateType = row["certificate_type"].asString();
    std::string countryCode = row["country_code"].asString();
    std::string subjectDn = row["subject_dn"].asString();
    std::string issuerDn = row["issuer_dn"].asString();

    // Handle boolean field (Query Executor returns proper boolean)
    bool storedInLdap = false;
    if (row["stored_in_ldap"].isBool()) {
        storedInLdap = row["stored_in_ldap"].asBool();
    } else if (row["stored_in_ldap"].isString()) {
        std::string val = row["stored_in_ldap"].asString();
        storedInLdap = (val == "t" || val == "true" || val == "1");
    }

    // Construct and return domain object
    return domain::Certificate(
        id,
        fingerprintSha256,
        certificateType,
        countryCode,
        subjectDn,
        issuerDn,
        storedInLdap
    );
}

} // namespace icao::relay::repositories
