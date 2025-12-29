/**
 * @file LdapController.hpp
 * @brief LDAP REST API Controller
 */

#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include "ldapintegration/application/usecase/UploadToLdapUseCase.hpp"
#include "ldapintegration/application/usecase/LdapHealthCheckUseCase.hpp"
#include "ldapintegration/application/usecase/SearchLdapUseCase.hpp"
#include <memory>
#include <nlohmann/json.hpp>

namespace ldapintegration::infrastructure::controller {

using namespace drogon;
using namespace ldapintegration::application::usecase;
using json = nlohmann::json;

/**
 * @brief LDAP REST API Controller
 *
 * Provides REST endpoints for LDAP operations:
 * - Health check
 * - Statistics
 * - Certificate search
 * - CRL search
 */
class LdapController : public HttpController<LdapController> {
public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(LdapController::healthCheck, "/api/ldap/health", Get);
    ADD_METHOD_TO(LdapController::getStatistics, "/api/ldap/statistics", Get);
    ADD_METHOD_TO(LdapController::searchCertificates, "/api/ldap/certificates", Get);
    ADD_METHOD_TO(LdapController::getCertificateByFingerprint, "/api/ldap/certificates/{fingerprint}", Get);
    ADD_METHOD_TO(LdapController::searchCrls, "/api/ldap/crls", Get);
    ADD_METHOD_TO(LdapController::getCrlByIssuer, "/api/ldap/crls/issuer", Get);
    ADD_METHOD_TO(LdapController::checkRevocation, "/api/ldap/revocation/check", Get);
    METHOD_LIST_END

    /**
     * @brief Constructor with dependency injection
     */
    LdapController();

    /**
     * @brief GET /api/ldap/health - LDAP health check
     */
    void healthCheck(
        const HttpRequestPtr& req,
        std::function<void(const HttpResponsePtr&)>&& callback
    );

    /**
     * @brief GET /api/ldap/statistics - Get LDAP statistics
     */
    void getStatistics(
        const HttpRequestPtr& req,
        std::function<void(const HttpResponsePtr&)>&& callback
    );

    /**
     * @brief GET /api/ldap/certificates - Search certificates
     */
    void searchCertificates(
        const HttpRequestPtr& req,
        std::function<void(const HttpResponsePtr&)>&& callback
    );

    /**
     * @brief GET /api/ldap/certificates/{fingerprint} - Get certificate by fingerprint
     */
    void getCertificateByFingerprint(
        const HttpRequestPtr& req,
        std::function<void(const HttpResponsePtr&)>&& callback,
        const std::string& fingerprint
    );

    /**
     * @brief GET /api/ldap/crls - Search CRLs
     */
    void searchCrls(
        const HttpRequestPtr& req,
        std::function<void(const HttpResponsePtr&)>&& callback
    );

    /**
     * @brief GET /api/ldap/crls/issuer - Get CRL by issuer DN
     */
    void getCrlByIssuer(
        const HttpRequestPtr& req,
        std::function<void(const HttpResponsePtr&)>&& callback
    );

    /**
     * @brief GET /api/ldap/revocation/check - Check certificate revocation
     */
    void checkRevocation(
        const HttpRequestPtr& req,
        std::function<void(const HttpResponsePtr&)>&& callback
    );

private:
    std::shared_ptr<LdapHealthCheckUseCase> healthCheckUseCase_;
    std::shared_ptr<SearchLdapUseCase> searchUseCase_;

    HttpResponsePtr createJsonResponse(const json& data, HttpStatusCode status = k200OK);
    HttpResponsePtr createErrorResponse(const std::string& message, HttpStatusCode status);

    std::optional<LdapEntryType> parseEntryType(const std::string& type);
};

} // namespace ldapintegration::infrastructure::controller
