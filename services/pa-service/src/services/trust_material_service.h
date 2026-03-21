#pragma once

#include <string>
#include <json/json.h>

namespace repositories {
    class LdapCertificateRepository;
    class LdapCrlRepository;
    class TrustMaterialRequestRepository;
}

namespace services {

class TrustMaterialService {
public:
    TrustMaterialService(
        repositories::LdapCertificateRepository* certRepo,
        repositories::LdapCrlRepository* crlRepo,
        repositories::TrustMaterialRequestRepository* requestRepo);

    ~TrustMaterialService() = default;

    TrustMaterialService(const TrustMaterialService&) = delete;
    TrustMaterialService& operator=(const TrustMaterialService&) = delete;

    struct TrustMaterialRequest {
        std::string countryCode;
        std::string dscIssuerDn;
        std::string encryptedMrz;
        std::string requestedBy;
        std::string clientIp;
        std::string userAgent;
        std::string apiClientId;
    };

    struct TrustMaterialResponse {
        bool success = false;
        Json::Value data;
        std::string errorMessage;
    };

    TrustMaterialResponse fetchTrustMaterials(const TrustMaterialRequest& request);

private:
    repositories::LdapCertificateRepository* certRepo_;
    repositories::LdapCrlRepository* crlRepo_;
    repositories::TrustMaterialRequestRepository* requestRepo_;

    struct MrzFields {
        std::string nationality;
        std::string documentType;
        std::string documentNumber;
    };

    MrzFields parseMrz(const std::string& mrzText);
    std::string base64Encode(const std::vector<uint8_t>& data);
};

} // namespace services
