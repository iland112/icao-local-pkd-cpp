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
        int cscaCount = 0;
        int linkCertCount = 0;
        int crlCount = 0;
        std::string clientIp;            // encrypted
        std::string userAgent;
        std::string requestedBy;
        std::string apiClientId;
        int processingTimeMs = 0;
        std::string status = "REQUESTED";
        std::string errorMessage;
    };

    struct ResultRecord {
        std::string requestId;
        std::string verificationStatus;  // VALID, INVALID, ERROR
        std::string verificationMessage;
        bool trustChainValid = false;
        bool sodSignatureValid = false;
        bool dgHashValid = false;
        bool crlCheckPassed = false;
        int clientProcessingTimeMs = 0;
        // MRZ fields (plaintext — will be encrypted before DB insert)
        std::string mrzNationality;
        std::string mrzDocumentType;
        std::string mrzDocumentNumber;
    };

    std::string insert(const RequestRecord& record);
    bool updateResult(const ResultRecord& result);
    Json::Value findAll(int limit = 20, int offset = 0, const std::string& countryFilter = "");
    Json::Value getStatistics();

private:
    common::IQueryExecutor* queryExecutor_;
};

} // namespace repositories
