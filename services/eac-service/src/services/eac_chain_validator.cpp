/**
 * @file eac_chain_validator.cpp
 * @brief CVCA → DV → IS trust chain validator
 */

#include "services/eac_chain_validator.h"
#include "repositories/cvc_certificate_repository.h"

#include <spdlog/spdlog.h>
#include <set>

namespace eac::services {

EacChainValidator::EacChainValidator(repositories::CvcCertificateRepository* repo)
    : repo_(repo) {}

Json::Value EacChainValidator::validateChain(const std::string& certId) {
    Json::Value result;
    result["certificateId"] = certId;
    result["chainValid"] = false;
    result["chainPath"] = "";
    result["chainDepth"] = 0;

    auto cert = repo_->findById(certId);
    if (!cert) {
        result["message"] = "Certificate not found";
        return result;
    }

    // Build chain by following CAR references
    std::vector<domain::CvcCertificateRecord> chain;
    chain.push_back(*cert);

    std::set<std::string> visited;
    visited.insert(cert->chr);

    const domain::CvcCertificateRecord* current = &chain.back();
    int maxDepth = 10;  // Prevent infinite loops

    while (current->car != current->chr && maxDepth-- > 0) {
        // Find issuer by CHR matching current CAR
        auto issuers = repo_->findByCar(current->car);
        if (issuers.empty()) {
            result["message"] = "Issuer not found for CAR: " + current->car;
            break;
        }

        // Circular reference check
        if (visited.count(issuers[0].chr)) {
            result["message"] = "Circular reference detected at: " + issuers[0].chr;
            break;
        }
        visited.insert(issuers[0].chr);
        chain.push_back(issuers[0]);
        current = &chain.back();

        // Check if we reached a self-signed CVCA
        if (current->car == current->chr) {
            // Full chain built
            std::string path;
            for (size_t i = 0; i < chain.size(); i++) {
                if (i > 0) path += " -> ";
                path += chain[i].chr + " (" + chain[i].cvcType + ")";
            }

            result["chainValid"] = true;
            result["chainPath"] = path;
            result["chainDepth"] = static_cast<int>(chain.size());
            result["message"] = "Trust chain valid";

            // Chain certificates
            Json::Value chainJson(Json::arrayValue);
            for (const auto& c : chain) {
                Json::Value cj;
                cj["id"] = c.id;
                cj["chr"] = c.chr;
                cj["car"] = c.car;
                cj["cvcType"] = c.cvcType;
                cj["countryCode"] = c.countryCode;
                cj["validationStatus"] = c.validationStatus;
                chainJson.append(cj);
            }
            result["certificates"] = chainJson;
            return result;
        }
    }

    // Partial chain
    result["chainDepth"] = static_cast<int>(chain.size());
    if (result["message"].asString().empty()) {
        result["message"] = "Incomplete chain (max depth reached or issuer missing)";
    }

    return result;
}

} // namespace eac::services
