#pragma once

/**
 * @file cvc_certificate_repository.h
 * @brief CVC certificate CRUD operations via IQueryExecutor
 */

#include "domain/cvc_models.h"

#include <json/json.h>
#include <optional>
#include <string>
#include <vector>

namespace common {
class IQueryExecutor;
}

namespace eac::repositories {

class CvcCertificateRepository {
public:
    explicit CvcCertificateRepository(common::IQueryExecutor* queryExecutor);

    // CRUD
    bool save(const domain::CvcCertificateRecord& record);
    std::optional<domain::CvcCertificateRecord> findById(const std::string& id);
    std::optional<domain::CvcCertificateRecord> findByFingerprint(const std::string& fingerprint);
    std::optional<domain::CvcCertificateRecord> findByChr(const std::string& chr);
    bool existsByFingerprint(const std::string& fingerprint);

    // Search
    Json::Value findAll(const std::string& country, const std::string& type,
                        const std::string& status, int page, int pageSize);
    int countAll(const std::string& country, const std::string& type,
                 const std::string& status);

    // Statistics
    Json::Value getStatistics();
    Json::Value getCountryList();

    // Chain lookup
    std::vector<domain::CvcCertificateRecord> findByCar(const std::string& car);

private:
    common::IQueryExecutor* queryExecutor_;

    domain::CvcCertificateRecord rowToModel(const Json::Value& row) const;
};

} // namespace eac::repositories
