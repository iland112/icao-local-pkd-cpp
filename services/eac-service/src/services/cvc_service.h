#pragma once

#include "domain/cvc_models.h"
#include <icao/cvc/cvc_certificate.h>
#include <json/json.h>
#include <optional>
#include <string>
#include <vector>

namespace eac::repositories {
class CvcCertificateRepository;
}

namespace eac::services {

class CvcService {
public:
    explicit CvcService(repositories::CvcCertificateRepository* repo);

    /**
     * @brief Parse and save a CVC certificate from binary data
     * @return Saved record, or nullopt on failure
     */
    std::optional<domain::CvcCertificateRecord> uploadCvc(
        const std::vector<uint8_t>& binary, const std::string& sourceType = "FILE_UPLOAD");

    /**
     * @brief Parse CVC binary for preview (no DB save)
     */
    std::optional<icao::cvc::CvcCertificate> previewCvc(const std::vector<uint8_t>& binary);

    /**
     * @brief Convert parsed CvcCertificate to JSON response
     */
    static Json::Value cvcToJson(const icao::cvc::CvcCertificate& cert);

private:
    repositories::CvcCertificateRepository* repo_;

    domain::CvcCertificateRecord toRecord(const icao::cvc::CvcCertificate& cert,
                                           const std::string& sourceType);
};

} // namespace eac::services
