/**
 * @file LdapController.cpp
 * @brief LDAP REST API Controller Implementation
 */

#include "LdapController.hpp"
#include "ldapintegration/infrastructure/adapter/OpenLdapAdapter.hpp"
#include <spdlog/spdlog.h>

namespace ldapintegration::infrastructure::controller {

using namespace ldapintegration::infrastructure::adapter;
using namespace ldapintegration::domain::model;

LdapController::LdapController() {
    // Initialize LDAP connection
    // In production, these values would come from configuration
    OpenLdapConfig config;
    config.host = "localhost";
    config.port = 389;
    config.bindDn = "cn=admin,dc=ldap,dc=smartcoreinc,dc=com";
    config.bindPassword = "admin";
    config.baseDn = "dc=ldap,dc=smartcoreinc,dc=com";
    config.poolSize = 5;

    auto ldapPort = std::make_shared<OpenLdapAdapter>(config);

    healthCheckUseCase_ = std::make_shared<LdapHealthCheckUseCase>(ldapPort);
    searchUseCase_ = std::make_shared<SearchLdapUseCase>(ldapPort);

    spdlog::info("LdapController initialized");
}

void LdapController::healthCheck(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback
) {
    try {
        auto result = healthCheckUseCase_->checkHealth();

        json response = {
            {"status", toString(result.status)},
            {"connectionAvailable", result.connectionAvailable},
            {"poolStats", result.poolStats},
            {"responseTimeMs", result.responseTimeMs},
            {"baseDn", result.baseDn},
            {"entryCount", result.entryCount},
            {"message", result.message},
            {"checkedAt", std::chrono::duration_cast<std::chrono::milliseconds>(
                result.checkedAt.time_since_epoch()
            ).count()}
        };

        auto statusCode = result.isHealthy() ? k200OK : k503ServiceUnavailable;
        callback(createJsonResponse(response, statusCode));

    } catch (const std::exception& e) {
        spdlog::error("LDAP health check failed: {}", e.what());
        callback(createErrorResponse(e.what(), k500InternalServerError));
    }
}

void LdapController::getStatistics(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback
) {
    try {
        auto result = healthCheckUseCase_->getStatistics();

        json countryStats = json::object();
        for (const auto& [country, count] : result.countryStats) {
            countryStats[country] = count;
        }

        json response = {
            {"cscaCount", result.totalCscaCount},
            {"dscCount", result.totalDscCount},
            {"dscNcCount", result.totalDscNcCount},
            {"crlCount", result.totalCrlCount},
            {"masterListCount", result.totalMasterListCount},
            {"countryStats", countryStats},
            {"retrievedAt", std::chrono::duration_cast<std::chrono::milliseconds>(
                result.retrievedAt.time_since_epoch()
            ).count()}
        };

        callback(createJsonResponse(response));

    } catch (const std::exception& e) {
        spdlog::error("Failed to get LDAP statistics: {}", e.what());
        callback(createErrorResponse(e.what(), k500InternalServerError));
    }
}

void LdapController::searchCertificates(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback
) {
    try {
        CertificateSearchQuery query;

        // Parse query parameters
        auto countryCode = req->getParameter("countryCode");
        if (!countryCode.empty()) {
            query.countryCode = countryCode;
        }

        auto fingerprint = req->getParameter("fingerprint");
        if (!fingerprint.empty()) {
            query.fingerprint = fingerprint;
        }

        auto issuerDn = req->getParameter("issuerDn");
        if (!issuerDn.empty()) {
            query.issuerDn = issuerDn;
        }

        auto typeParam = req->getParameter("type");
        if (!typeParam.empty()) {
            query.entryType = parseEntryType(typeParam);
        }

        auto includeExpired = req->getParameter("includeExpired");
        query.includeExpired = (includeExpired == "true");

        auto limitParam = req->getParameter("limit");
        if (!limitParam.empty()) {
            query.limit = std::stoi(limitParam);
        }

        auto offsetParam = req->getParameter("offset");
        if (!offsetParam.empty()) {
            query.offset = std::stoi(offsetParam);
        }

        auto result = searchUseCase_->searchCertificates(query);

        json certificates = json::array();
        for (const auto& cert : result.certificates) {
            certificates.push_back({
                {"dn", cert.getDn().getValue()},
                {"fingerprint", cert.getFingerprint()},
                {"serialNumber", cert.getSerialNumber()},
                {"issuerDn", cert.getIssuerDn()},
                {"countryCode", cert.getCountryCode()},
                {"entryType", toString(cert.getEntryType())},
                {"validationStatus", cert.getValidationStatus()},
                {"isExpired", cert.isExpired()},
                {"isCurrentlyValid", cert.isCurrentlyValid()}
            });
        }

        json response = {
            {"certificates", certificates},
            {"totalCount", result.totalCount},
            {"page", result.page},
            {"pageSize", result.pageSize},
            {"hasMore", result.hasMore}
        };

        callback(createJsonResponse(response));

    } catch (const std::exception& e) {
        spdlog::error("Certificate search failed: {}", e.what());
        callback(createErrorResponse(e.what(), k500InternalServerError));
    }
}

void LdapController::getCertificateByFingerprint(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback,
    const std::string& fingerprint
) {
    try {
        CertificateSearchQuery query;
        query.fingerprint = fingerprint;

        auto typeParam = req->getParameter("type");
        if (!typeParam.empty()) {
            query.entryType = parseEntryType(typeParam);
        }

        auto result = searchUseCase_->searchCertificates(query);

        if (result.certificates.empty()) {
            callback(createErrorResponse("Certificate not found", k404NotFound));
            return;
        }

        const auto& cert = result.certificates[0];
        json response = {
            {"dn", cert.getDn().getValue()},
            {"fingerprint", cert.getFingerprint()},
            {"serialNumber", cert.getSerialNumber()},
            {"issuerDn", cert.getIssuerDn()},
            {"countryCode", cert.getCountryCode()},
            {"entryType", toString(cert.getEntryType())},
            {"validationStatus", cert.getValidationStatus()},
            {"isExpired", cert.isExpired()},
            {"isCurrentlyValid", cert.isCurrentlyValid()},
            {"certificateBase64", cert.getX509CertificateBase64()}
        };

        callback(createJsonResponse(response));

    } catch (const std::exception& e) {
        spdlog::error("Get certificate by fingerprint failed: {}", e.what());
        callback(createErrorResponse(e.what(), k500InternalServerError));
    }
}

void LdapController::searchCrls(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback
) {
    try {
        CrlSearchQuery query;

        auto countryCode = req->getParameter("countryCode");
        if (!countryCode.empty()) {
            query.countryCode = countryCode;
        }

        auto issuerDn = req->getParameter("issuerDn");
        if (!issuerDn.empty()) {
            query.issuerDn = issuerDn;
        }

        auto includeExpired = req->getParameter("includeExpired");
        query.includeExpired = (includeExpired == "true");

        auto limitParam = req->getParameter("limit");
        if (!limitParam.empty()) {
            query.limit = std::stoi(limitParam);
        }

        auto result = searchUseCase_->searchCrls(query);

        json crls = json::array();
        for (const auto& crl : result.crls) {
            crls.push_back({
                {"dn", crl.getDn().getValue()},
                {"issuerDn", crl.getIssuerDn()},
                {"issuerName", crl.getIssuerName()},
                {"countryCode", crl.getCountryCode()},
                {"revokedCount", crl.getRevokedCount()},
                {"isExpired", crl.isExpired()}
            });
        }

        json response = {
            {"crls", crls},
            {"totalCount", result.totalCount},
            {"page", result.page},
            {"pageSize", result.pageSize},
            {"hasMore", result.hasMore}
        };

        callback(createJsonResponse(response));

    } catch (const std::exception& e) {
        spdlog::error("CRL search failed: {}", e.what());
        callback(createErrorResponse(e.what(), k500InternalServerError));
    }
}

void LdapController::getCrlByIssuer(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback
) {
    try {
        auto issuerDn = req->getParameter("issuerDn");
        if (issuerDn.empty()) {
            callback(createErrorResponse("issuerDn parameter is required", k400BadRequest));
            return;
        }

        auto crl = searchUseCase_->findCrlForCertificate(issuerDn);
        if (!crl) {
            callback(createErrorResponse("CRL not found", k404NotFound));
            return;
        }

        json response = {
            {"dn", crl->getDn().getValue()},
            {"issuerDn", crl->getIssuerDn()},
            {"issuerName", crl->getIssuerName()},
            {"countryCode", crl->getCountryCode()},
            {"revokedCount", crl->getRevokedCount()},
            {"revokedSerialNumbers", crl->getRevokedSerialNumbersString()},
            {"isExpired", crl->isExpired()},
            {"needsUpdate", crl->needsUpdate()},
            {"crlBase64", crl->getX509CrlBase64()}
        };

        callback(createJsonResponse(response));

    } catch (const std::exception& e) {
        spdlog::error("Get CRL by issuer failed: {}", e.what());
        callback(createErrorResponse(e.what(), k500InternalServerError));
    }
}

void LdapController::checkRevocation(
    const HttpRequestPtr& req,
    std::function<void(const HttpResponsePtr&)>&& callback
) {
    try {
        auto issuerDn = req->getParameter("issuerDn");
        auto serialNumber = req->getParameter("serialNumber");

        if (issuerDn.empty() || serialNumber.empty()) {
            callback(createErrorResponse(
                "issuerDn and serialNumber parameters are required",
                k400BadRequest
            ));
            return;
        }

        bool isRevoked = searchUseCase_->isCertificateRevoked(issuerDn, serialNumber);

        json response = {
            {"issuerDn", issuerDn},
            {"serialNumber", serialNumber},
            {"isRevoked", isRevoked},
            {"status", isRevoked ? "REVOKED" : "VALID"}
        };

        callback(createJsonResponse(response));

    } catch (const std::exception& e) {
        spdlog::error("Revocation check failed: {}", e.what());
        callback(createErrorResponse(e.what(), k500InternalServerError));
    }
}

HttpResponsePtr LdapController::createJsonResponse(const json& data, HttpStatusCode status) {
    auto resp = HttpResponse::newHttpResponse();
    resp->setStatusCode(status);
    resp->setContentTypeCode(CT_APPLICATION_JSON);
    resp->setBody(data.dump());
    return resp;
}

HttpResponsePtr LdapController::createErrorResponse(const std::string& message, HttpStatusCode status) {
    json error = {
        {"error", true},
        {"message", message},
        {"status", static_cast<int>(status)}
    };
    return createJsonResponse(error, status);
}

std::optional<LdapEntryType> LdapController::parseEntryType(const std::string& type) {
    if (type == "CSCA") return LdapEntryType::CSCA;
    if (type == "DSC") return LdapEntryType::DSC;
    if (type == "DSC_NC") return LdapEntryType::DSC_NC;
    if (type == "CRL") return LdapEntryType::CRL;
    if (type == "MASTER_LIST") return LdapEntryType::MASTER_LIST;
    return std::nullopt;
}

} // namespace ldapintegration::infrastructure::controller
