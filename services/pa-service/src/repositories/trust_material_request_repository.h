#pragma once

#include <string>
#include <json/json.h>

namespace common {
    class IQueryExecutor;
}

namespace repositories {

class TrustMaterialRequestRepository {
public:
    explicit TrustMaterialRequestRepository(common::IQueryExecutor* executor);
    ~TrustMaterialRequestRepository() = default;

    TrustMaterialRequestRepository(const TrustMaterialRequestRepository&) = delete;
    TrustMaterialRequestRepository& operator=(const TrustMaterialRequestRepository&) = delete;

    struct RequestRecord {
        std::string countryCode;
        std::string dscIssuerDn;
        std::string mrzNationality;      // encrypted
        std::string mrzDocumentType;     // encrypted
        std::string mrzDocumentNumber;   // encrypted
        int cscaCount = 0;
        int linkCertCount = 0;
        int crlCount = 0;
        std::string clientIp;            // encrypted
        std::string userAgent;
        std::string requestedBy;
        std::string apiClientId;
        int processingTimeMs = 0;
        std::string status = "SUCCESS";
        std::string errorMessage;
    };

    std::string insert(const RequestRecord& record);
    Json::Value getStatistics();

private:
    common::IQueryExecutor* queryExecutor_;
};

} // namespace repositories
