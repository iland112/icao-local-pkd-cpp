#pragma once

#include "domain/cvc_models.h"
#include <json/json.h>
#include <string>

namespace eac::repositories {
class CvcCertificateRepository;
}

namespace eac::services {

class EacChainValidator {
public:
    explicit EacChainValidator(repositories::CvcCertificateRepository* repo);

    /**
     * @brief Build and validate the trust chain for a certificate
     * @param certId Certificate ID to validate
     * @return JSON with chain path, depth, and validation result
     */
    Json::Value validateChain(const std::string& certId);

private:
    repositories::CvcCertificateRepository* repo_;
};

} // namespace eac::services
