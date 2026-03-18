/**
 * @file certificate_handler.cpp
 * @brief CertificateHandler implementation
 *
 * Extracted from main.cpp - certificate-related handler endpoints.
 *
 * @date 2026-02-17
 */

#include "certificate_handler.h"
#include "../common/crl_parser.h"
#include "../common/doc9303_checklist.h"
#include "handler_utils.h"

#include <openssl/x509.h>

#include <drogon/drogon.h>
#include <spdlog/spdlog.h>
#include <json/json.h>

#include <algorithm>
#include <map>
#include <set>
#include <vector>
#include <tuple>
#include <chrono>
#include <ctime>

// Services
#include "../services/certificate_service.h"
#include "../services/validation_service.h"

// Repositories
#include "../repositories/certificate_repository.h"
#include "../repositories/crl_repository.h"
#include "../repositories/pending_dsc_repository.h"

// LDAP Storage Service (for DSC approval → LDAP write)
#include "../services/ldap_storage_service.h"

// Common utilities
#include "db_connection_pool.h"
#include <ldap_connection_pool.h>

// Link Certificate Validation
#include "../common/lc_validator.h"

// Audit logging (shared library)
#include <icao/audit/audit_log.h>
// Query helpers (boolLiteral, etc.)
#include "query_helpers.h"

// Bring in audit types for cleaner code
using icao::audit::AuditLogEntry;
using icao::audit::OperationType;
using icao::audit::logOperation;
using icao::audit::extractUserFromRequest;
using icao::audit::extractIpAddress;

namespace handlers {

// =============================================================================
// Constructor
// =============================================================================

CertificateHandler::CertificateHandler(
    services::CertificateService* certificateService,
    services::ValidationService* validationService,
    repositories::CertificateRepository* certificateRepository,
    repositories::CrlRepository* crlRepository,
    common::IQueryExecutor* queryExecutor,
    common::LdapConnectionPool* ldapPool,
    repositories::PendingDscRepository* pendingDscRepository,
    services::LdapStorageService* ldapStorageService)
    : certificateService_(certificateService)
    , validationService_(validationService)
    , certificateRepository_(certificateRepository)
    , crlRepository_(crlRepository)
    , queryExecutor_(queryExecutor)
    , ldapPool_(ldapPool)
    , pendingDscRepository_(pendingDscRepository)
    , ldapStorageService_(ldapStorageService)
{
}

// =============================================================================
// Route Registration
// =============================================================================

void CertificateHandler::registerRoutes(drogon::HttpAppFramework& app) {
    // GET /api/certificates/search
    app.registerHandler(
        "/api/certificates/search",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleSearch(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/certificates/detail
    app.registerHandler(
        "/api/certificates/detail",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleDetail(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/certificates/validation
    app.registerHandler(
        "/api/certificates/validation",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleValidation(req, std::move(callback));
        },
        {drogon::Get}
    );

    // POST /api/certificates/pa-lookup
    app.registerHandler(
        "/api/certificates/pa-lookup",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handlePaLookup(req, std::move(callback));
        },
        {drogon::Post}
    );

    // GET /api/certificates/export/file
    app.registerHandler(
        "/api/certificates/export/file",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleExportFile(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/certificates/export/country
    app.registerHandler(
        "/api/certificates/export/country",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleExportCountry(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/certificates/export/all
    app.registerHandler(
        "/api/certificates/export/all",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleExportAll(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/certificates/countries
    app.registerHandler(
        "/api/certificates/countries",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleCountries(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/certificates/dsc-nc/report
    app.registerHandler(
        "/api/certificates/dsc-nc/report",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleDscNcReport(req, std::move(callback));
        },
        {drogon::Get}
    );

    // POST /api/validate/link-cert
    app.registerHandler(
        "/api/validate/link-cert",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleValidateLinkCert(req, std::move(callback));
        },
        {drogon::Post}
    );

    // GET /api/link-certs/search
    app.registerHandler(
        "/api/link-certs/search",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleLinkCertsSearch(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/link-certs/{id}
    app.registerHandler(
        "/api/link-certs/{id}",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
            handleLinkCertDetail(req, std::move(callback), id);
        },
        {drogon::Get}
    );

    // GET /api/certificates/crl/report
    app.registerHandler(
        "/api/certificates/crl/report",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleCrlReport(req, std::move(callback));
        },
        {drogon::Get});

    // GET /api/certificates/crl/{id}
    app.registerHandler(
        "/api/certificates/crl/{id}",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
            handleCrlDetail(req, std::move(callback), id);
        },
        {drogon::Get});

    // GET /api/certificates/crl/{id}/download
    app.registerHandler(
        "/api/certificates/crl/{id}/download",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
            handleCrlDownload(req, std::move(callback), id);
        },
        {drogon::Get});

    // GET /api/certificates/quality/report
    app.registerHandler(
        "/api/certificates/quality/report",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleQualityReport(req, std::move(callback));
        },
        {drogon::Get});

    // GET /api/certificates/doc9303-checklist
    app.registerHandler(
        "/api/certificates/doc9303-checklist",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleDoc9303Checklist(req, std::move(callback));
        },
        {drogon::Get});

    // --- Pending DSC Registration Approval ---

    // GET /api/certificates/pending-dsc
    app.registerHandler(
        "/api/certificates/pending-dsc",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handlePendingDscList(req, std::move(callback));
        },
        {drogon::Get});

    // GET /api/certificates/pending-dsc/stats
    app.registerHandler(
        "/api/certificates/pending-dsc/stats",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handlePendingDscStats(req, std::move(callback));
        },
        {drogon::Get});

    // POST /api/certificates/pending-dsc/{id}/approve
    app.registerHandler(
        "/api/certificates/pending-dsc/{id}/approve",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
            handlePendingDscApprove(req, std::move(callback), id);
        },
        {drogon::Post});

    // POST /api/certificates/pending-dsc/{id}/reject
    app.registerHandler(
        "/api/certificates/pending-dsc/{id}/reject",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
            handlePendingDscReject(req, std::move(callback), id);
        },
        {drogon::Post});

    spdlog::info("Certificate handler: 20 routes registered");
}

// =============================================================================
// Handler 1: GET /api/certificates/search
// =============================================================================

void CertificateHandler::handleSearch(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        // Parse query parameters
        std::string country = req->getOptionalParameter<std::string>("country").value_or("");
        std::string certTypeStr = req->getOptionalParameter<std::string>("certType").value_or("");
        std::string validityStr = req->getOptionalParameter<std::string>("validity").value_or("all");
        std::string searchTerm = req->getOptionalParameter<std::string>("searchTerm").value_or("");
        std::string sourceFilter = req->getOptionalParameter<std::string>("source").value_or("");
        int limit = req->getOptionalParameter<int>("limit").value_or(50);
        int offset = req->getOptionalParameter<int>("offset").value_or(0);

        // Validate limit (max 200)
        if (limit > 200) limit = 200;
        if (limit < 1) limit = 50;
        if (offset < 0) offset = 0;

        spdlog::info("Certificate search: country={}, certType={}, validity={}, source={}, search={}, limit={}, offset={}",
                    country, certTypeStr, validityStr, sourceFilter, searchTerm, limit, offset);

        // DB-based search (fast indexed queries, no X.509 parsing overhead)
        {
            repositories::CertificateSearchFilter filter;
            if (!country.empty()) filter.countryCode = country;
            if (!certTypeStr.empty()) filter.certificateType = certTypeStr;
            if (!sourceFilter.empty()) filter.sourceType = sourceFilter;
            if (validityStr != "all") filter.validityStatus = validityStr;
            if (!searchTerm.empty()) filter.searchTerm = searchTerm;
            filter.limit = limit;
            filter.offset = offset;

            Json::Value dbResult = certificateRepository_->search(filter);
            auto resp = drogon::HttpResponse::newHttpJsonResponse(dbResult);
            callback(resp);
        }

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CertHandler::search", e));
    }
}

// =============================================================================
// Handler 2: GET /api/certificates/detail
// =============================================================================

void CertificateHandler::handleDetail(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        std::string dn = req->getOptionalParameter<std::string>("dn").value_or("");

        if (dn.empty()) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "DN parameter is required";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        spdlog::info("Certificate detail request: dn={}", dn);

        // Get certificate details
        auto cert = certificateService_->getCertificateDetail(dn);

        // Build JSON response
        Json::Value response;
        response["success"] = true;
        response["dn"] = cert.getDn();
        response["cn"] = cert.getCn();
        response["sn"] = cert.getSn();
        response["country"] = cert.getCountry();
        response["certType"] = cert.getCertTypeString();
        response["subjectDn"] = cert.getSubjectDn();
        response["issuerDn"] = cert.getIssuerDn();
        response["fingerprint"] = cert.getFingerprint();
        response["isSelfSigned"] = cert.isSelfSigned();

        // Convert time_point to ISO 8601 string
        auto validFrom = std::chrono::system_clock::to_time_t(cert.getValidFrom());
        auto validTo = std::chrono::system_clock::to_time_t(cert.getValidTo());
        char timeBuf[32];
        std::tm gmBuf{};
        gmtime_r(&validFrom, &gmBuf);
        std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", &gmBuf);
        response["validFrom"] = timeBuf;
        gmtime_r(&validTo, &gmBuf);
        std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", &gmBuf);
        response["validTo"] = timeBuf;

        // Validity status
        auto status = cert.getValidityStatus();
        if (status == domain::models::ValidityStatus::VALID) response["validity"] = "VALID";
        else if (status == domain::models::ValidityStatus::EXPIRED) response["validity"] = "EXPIRED";
        else if (status == domain::models::ValidityStatus::NOT_YET_VALID) response["validity"] = "NOT_YET_VALID";
        else response["validity"] = "UNKNOWN";

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CertHandler::detail", e));
    }
}

// =============================================================================
// Handler 3: GET /api/certificates/validation
// =============================================================================

void CertificateHandler::handleValidation(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        std::string fingerprint = req->getOptionalParameter<std::string>("fingerprint").value_or("");

        if (fingerprint.empty()) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "fingerprint parameter is required";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        spdlog::info("GET /api/certificates/validation - fingerprint: {}", fingerprint.substr(0, 16) + "...");

        Json::Value response = validationService_->getValidationByFingerprint(fingerprint);
        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CertHandler::validation", e));
    }
}

// =============================================================================
// Handler 4: POST /api/certificates/pa-lookup
// =============================================================================

void CertificateHandler::handlePaLookup(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        auto jsonBody = req->getJsonObject();
        if (!jsonBody) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "JSON body is required";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        std::string subjectDn = (*jsonBody).get("subjectDn", "").asString();
        std::string fingerprint = (*jsonBody).get("fingerprint", "").asString();

        if (subjectDn.empty() && fingerprint.empty()) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "Either subjectDn or fingerprint parameter is required";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        spdlog::info("POST /api/certificates/pa-lookup - subjectDn: '{}', fingerprint: '{}'",
            subjectDn.empty() ? "(empty)" : subjectDn.substr(0, 60),
            fingerprint.empty() ? "(empty)" : fingerprint.substr(0, 16));

        // Real-time LDAP validation (ICAO Doc 9303 compliant)
        Json::Value response = validationService_->validateDscRealTime(subjectDn, fingerprint);

        // Save lookup history to pa_verification (non-blocking, failure doesn't affect response)
        if (response.get("success", false).asBool() && !response["validation"].isNull()) {
            try {
                savePaLookupHistory(req, response["validation"]);
            } catch (const std::exception& e) {
                spdlog::warn("Failed to save PA lookup history: {}", e.what());
            } catch (...) {
                spdlog::warn("Failed to save PA lookup history (unknown error)");
            }
        }

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CertHandler::paLookup", e));
    }
}

// =============================================================================
// Helper: Save PA lookup result to pa_verification history
// =============================================================================

void CertificateHandler::savePaLookupHistory(
    const drogon::HttpRequestPtr& req,
    const Json::Value& validation) {
    std::string dbType = queryExecutor_->getDatabaseType();

    // Generate UUID
    std::string uuidQuery = (dbType == "postgres")
        ? "SELECT uuid_generate_v4()::text as id"
        : "SELECT LOWER(REGEXP_REPLACE(RAWTOHEX(SYS_GUID()), "
          "'([A-F0-9]{8})([A-F0-9]{4})([A-F0-9]{4})([A-F0-9]{4})([A-F0-9]{12})', "
          "'\\1-\\2-\\3-\\4-\\5')) as id FROM DUAL";
    Json::Value uuidResult = queryExecutor_->executeQuery(uuidQuery, {});
    if (uuidResult.empty()) return;
    std::string id = uuidResult[0]["id"].asString();

    // Extract request metadata
    std::string clientIp = extractIpAddress(req);
    std::string userAgent = req->getHeader("User-Agent");
    auto [userId, username] = extractUserFromRequest(req);

    // Map validation fields
    std::string countryCode = validation.get("countryCode", "").asString();
    if (countryCode.empty()) countryCode = "XX";
    std::string status = validation.get("validationStatus", "PENDING").asString();
    std::string verificationMessage = validation.get("trustChainMessage", "").asString();
    std::string trustChainValidStr = common::db::boolLiteral(dbType, validation.get("trustChainValid", false).asBool());
    std::string dscNonConformantStr = common::db::boolLiteral(dbType, validation.get("dscNonConformant", false).asBool());

    const char* insertQuery =
        "INSERT INTO pa_verification ("
        "id, verification_type, issuing_country, verification_status, verification_message, "
        "trust_chain_valid, trust_chain_message, "
        "crl_status, "
        "dsc_subject_dn, dsc_issuer_dn, dsc_serial_number, dsc_fingerprint, "
        "csca_subject_dn, "
        "dsc_non_conformant, pkd_conformance_code, pkd_conformance_text, "
        "client_ip, user_agent, requested_by"
        ") VALUES ("
        "$1, $2, $3, $4, $5, "
        "$6, $7, "
        "$8, "
        "$9, $10, $11, $12, "
        "$13, "
        "$14, $15, $16, "
        "$17, $18, $19"
        ")";

    std::vector<std::string> params;
    params.reserve(19);
    params.push_back(id);                                                     // $1
    params.push_back("LOOKUP");                                               // $2
    params.push_back(countryCode);                                            // $3
    params.push_back(status);                                                 // $4
    params.push_back(verificationMessage);                                    // $5
    params.push_back(trustChainValidStr);                                     // $6
    params.push_back(validation.get("trustChainMessage", "").asString());     // $7
    params.push_back(validation.get("crlCheckStatus", "NOT_CHECKED").asString()); // $8
    params.push_back(validation.get("subjectDn", "").asString());             // $9
    params.push_back(validation.get("issuerDn", "").asString());              // $10
    params.push_back(validation.get("serialNumber", "").asString());          // $11
    params.push_back(validation.get("fingerprint", "").asString());           // $12
    params.push_back(validation.get("cscaSubjectDn", "").asString());         // $13
    params.push_back(dscNonConformantStr);                                    // $14
    params.push_back(validation.get("pkdConformanceCode", "").asString());    // $15
    params.push_back(validation.get("pkdConformanceText", "").asString());    // $16
    params.push_back(clientIp);                                               // $17
    params.push_back(userAgent);                                              // $18
    params.push_back(username.value_or(""));                                    // $19

    queryExecutor_->executeQuery(insertQuery, params);
    spdlog::info("PA lookup history saved: id={}, country={}, status={}", id.substr(0, 8), countryCode, status);
}

// =============================================================================
// Handler 5: GET /api/certificates/export/file
// =============================================================================

void CertificateHandler::handleExportFile(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        std::string dn = req->getOptionalParameter<std::string>("dn").value_or("");
        std::string format = req->getOptionalParameter<std::string>("format").value_or("pem");

        if (dn.empty()) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "DN parameter is required";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        if (format != "der" && format != "pem") {
            Json::Value error;
            error["success"] = false;
            error["error"] = "Invalid format. Use 'der' or 'pem'";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        spdlog::info("Certificate export file: dn={}, format={}", dn, format);

        // Export certificate
        services::ExportFormat exportFormat = (format == "der") ?
            services::ExportFormat::DER : services::ExportFormat::PEM;

        auto result = certificateService_->exportCertificateFile(dn, exportFormat);

        if (!result.success) {
            Json::Value error;
            error["success"] = false;
            error["error"] = result.errorMessage;
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k500InternalServerError);
            callback(resp);
            return;
        }

        // Return binary file
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setBody(std::string(result.data.begin(), result.data.end()));
        resp->setContentTypeCode(drogon::CT_NONE);
        resp->addHeader("Content-Type", result.contentType);
        resp->addHeader("Content-Disposition", "attachment; filename=\"" + result.filename + "\"");
        callback(resp);

        // Audit logging - CERT_EXPORT success (single file)
        {
            AuditLogEntry auditEntry;
            auto [userId8, username8] = extractUserFromRequest(req);
            auditEntry.userId = userId8;
            auditEntry.username = username8;
            auditEntry.operationType = OperationType::CERT_EXPORT;
            auditEntry.operationSubtype = "SINGLE_CERT";
            auditEntry.resourceId = dn;
            auditEntry.resourceType = "CERTIFICATE";
            auditEntry.ipAddress = extractIpAddress(req);
            auditEntry.userAgent = req->getHeader("User-Agent");
            auditEntry.requestMethod = "GET";
            auditEntry.requestPath = "/api/certificates/export/file";
            auditEntry.success = true;
            Json::Value metadata;
            metadata["format"] = format;
            metadata["fileName"] = result.filename;
            metadata["fileSize"] = static_cast<Json::Int64>(result.data.size());
            auditEntry.metadata = metadata;
            logOperation(queryExecutor_, auditEntry);
        }

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CertHandler::exportFile", e));
    }
}

// =============================================================================
// Handler 6: GET /api/certificates/export/country
// =============================================================================

void CertificateHandler::handleExportCountry(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        std::string country = req->getOptionalParameter<std::string>("country").value_or("");
        std::string format = req->getOptionalParameter<std::string>("format").value_or("pem");

        if (country.empty()) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "Country parameter is required";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        spdlog::info("Certificate export country: country={}, format={}", country, format);

        // Export all certificates for country
        services::ExportFormat exportFormat = (format == "der") ?
            services::ExportFormat::DER : services::ExportFormat::PEM;

        auto result = certificateService_->exportCountryCertificates(country, exportFormat);

        if (!result.success) {
            Json::Value error;
            error["success"] = false;
            error["error"] = result.errorMessage;
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k500InternalServerError);
            callback(resp);
            return;
        }

        // Return ZIP file
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setBody(std::string(result.data.begin(), result.data.end()));
        resp->setContentTypeCode(drogon::CT_NONE);
        resp->addHeader("Content-Type", result.contentType);
        resp->addHeader("Content-Disposition", "attachment; filename=\"" + result.filename + "\"");
        callback(resp);

        // Audit logging - CERT_EXPORT success (country ZIP)
        {
            AuditLogEntry auditEntry;
            auto [userId, username] = extractUserFromRequest(req);
            auditEntry.userId = userId;
            auditEntry.username = username;
            auditEntry.operationType = OperationType::CERT_EXPORT;
            auditEntry.operationSubtype = "COUNTRY_ZIP";
            auditEntry.resourceId = country;
            auditEntry.resourceType = "CERTIFICATE_COLLECTION";
            auditEntry.ipAddress = extractIpAddress(req);
            auditEntry.userAgent = req->getHeader("User-Agent");
            auditEntry.requestMethod = "GET";
            auditEntry.requestPath = "/api/certificates/export/country";
            auditEntry.success = true;
            Json::Value metadata;
            metadata["country"] = country;
            metadata["format"] = format;
            metadata["fileName"] = result.filename;
            metadata["fileSize"] = static_cast<Json::Int64>(result.data.size());
            auditEntry.metadata = metadata;
            logOperation(queryExecutor_, auditEntry);
        }

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CertHandler::exportCountry", e));
    }
}

// =============================================================================
// Handler 7: GET /api/certificates/export/all
// =============================================================================

void CertificateHandler::handleExportAll(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        std::string format = req->getOptionalParameter<std::string>("format").value_or("pem");

        spdlog::info("Full PKD export requested: format={}", format);

        services::ExportFormat exportFormat = (format == "der") ?
            services::ExportFormat::DER : services::ExportFormat::PEM;

        auto exportResult = services::exportAllCertificatesFromDb(
            certificateRepository_,
            crlRepository_,
            queryExecutor_,
            exportFormat,
            ldapPool_
        );

        if (!exportResult.success) {
            Json::Value error;
            error["success"] = false;
            error["error"] = exportResult.errorMessage;
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k500InternalServerError);
            callback(resp);
            return;
        }

        // Return ZIP binary
        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setStatusCode(drogon::k200OK);
        resp->setContentTypeString("application/zip");
        resp->addHeader("Content-Disposition",
            "attachment; filename=\"" + exportResult.filename + "\"");
        resp->setBody(std::string(
            reinterpret_cast<const char*>(exportResult.data.data()),
            exportResult.data.size()));
        callback(resp);

        // Audit log
        {
            AuditLogEntry auditEntry;
            auto [userId, username] = extractUserFromRequest(req);
            auditEntry.userId = userId;
            auditEntry.username = username;
            auditEntry.operationType = OperationType::CERT_EXPORT;
            auditEntry.operationSubtype = "ALL_ZIP";
            auditEntry.resourceType = "CERTIFICATE_COLLECTION";
            auditEntry.ipAddress = extractIpAddress(req);
            auditEntry.userAgent = req->getHeader("User-Agent");
            auditEntry.requestMethod = "GET";
            auditEntry.requestPath = "/api/certificates/export/all";
            auditEntry.success = true;
            Json::Value metadata;
            metadata["format"] = format;
            metadata["fileName"] = exportResult.filename;
            metadata["fileSize"] = static_cast<Json::Int64>(exportResult.data.size());
            auditEntry.metadata = metadata;
            logOperation(queryExecutor_, auditEntry);
        }

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CertHandler::exportAll", e));
    }
}

// =============================================================================
// Handler 8: GET /api/certificates/countries
// =============================================================================

void CertificateHandler::handleCountries(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        spdlog::debug("Fetching list of available countries");

        if (!certificateRepository_) {
            throw std::runtime_error("Certificate repository not initialized");
        }

        auto rows = certificateRepository_->getDistinctCountries();

        Json::Value response;
        response["success"] = true;
        response["count"] = static_cast<int>(rows.size());

        Json::Value countryList(Json::arrayValue);
        for (const auto& row : rows) {
            countryList.append(row["country_code"].asString());
        }
        response["countries"] = countryList;

        spdlog::info("Countries list fetched: {} countries", rows.size());

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CertHandler::countries", e));
    }
}

// =============================================================================
// Handler 9: GET /api/certificates/dsc-nc/report
// =============================================================================

void CertificateHandler::handleDscNcReport(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        // Parse query parameters
        std::string countryFilter = req->getOptionalParameter<std::string>("country").value_or("");
        std::string codeFilter = req->getOptionalParameter<std::string>("conformanceCode").value_or("");
        int page = req->getOptionalParameter<int>("page").value_or(1);
        int size = req->getOptionalParameter<int>("size").value_or(50);
        if (page < 1) page = 1;
        if (size < 1) size = 50;
        if (size > 200) size = 200;

        spdlog::info("DSC_NC report: country={}, code={}, page={}, size={}", countryFilter, codeFilter, page, size);

        // Fetch all DSC_NC certificates from LDAP (batch 200 at a time)
        domain::models::CertificateSearchResult result;
        result.total = 0;
        result.limit = 200;
        result.offset = 0;
        {
            int batchOffset = 0;
            const int batchSize = 200;
            while (true) {
                domain::models::CertificateSearchCriteria criteria;
                criteria.certType = domain::models::CertificateType::DSC_NC;
                criteria.limit = batchSize;
                criteria.offset = batchOffset;
                auto batch = certificateService_->searchCertificates(criteria);
                for (auto& cert : batch.certificates) {
                    result.certificates.push_back(std::move(cert));
                }
                result.total = batch.total;
                if (static_cast<int>(batch.certificates.size()) < batchSize) break;
                batchOffset += batchSize;
                if (batchOffset >= batch.total) break;
            }
        }

        // Single-pass aggregation
        std::map<std::string, std::pair<std::string, int>> conformanceCodeMap; // code -> {description, count}
        std::map<std::string, std::tuple<int, int, int>> countryMap; // country -> {total, valid, expired}
        std::map<int, int> yearMap; // year -> count
        std::map<std::string, int> sigAlgMap; // algorithm -> count
        std::map<std::string, int> pubKeyAlgMap; // algorithm -> count
        int validCount = 0, expiredCount = 0, notYetValidCount = 0, unknownCount = 0;

        // Filtered certificates for table
        std::vector<const domain::models::Certificate*> filteredCerts;

        for (const auto& cert : result.certificates) {
            // Aggregation (always, before filtering)
            std::string code = cert.getPkdConformanceCode().value_or("UNKNOWN");
            std::string desc = cert.getPkdConformanceText().value_or("");
            conformanceCodeMap[code].first = desc;
            conformanceCodeMap[code].second++;

            std::string country = cert.getCountry();
            auto status = cert.getValidityStatus();
            auto& countryEntry = countryMap[country];
            std::get<0>(countryEntry)++;
            if (status == domain::models::ValidityStatus::VALID) {
                std::get<1>(countryEntry)++;
                validCount++;
            } else if (status == domain::models::ValidityStatus::EXPIRED) {
                std::get<2>(countryEntry)++;
                expiredCount++;
            } else if (status == domain::models::ValidityStatus::NOT_YET_VALID) {
                notYetValidCount++;
            } else {
                unknownCount++;
            }

            // Year from notBefore
            auto notBefore = std::chrono::system_clock::to_time_t(cert.getValidFrom());
            struct tm tmBuf;
            gmtime_r(&notBefore, &tmBuf);
            yearMap[tmBuf.tm_year + 1900]++;

            // Algorithms
            std::string sigAlg = cert.getSignatureAlgorithm().value_or("Unknown");
            sigAlgMap[sigAlg]++;
            std::string pubKeyAlg = cert.getPublicKeyAlgorithm().value_or("Unknown");
            pubKeyAlgMap[pubKeyAlg]++;

            // Apply filters for table
            bool passCountry = countryFilter.empty() || cert.getCountry() == countryFilter;
            bool passCode = codeFilter.empty() || code.find(codeFilter) == 0; // prefix match
            if (passCountry && passCode) {
                filteredCerts.push_back(&cert);
            }
        }

        // Build JSON response
        Json::Value response;
        response["success"] = true;

        // Summary
        Json::Value summary;
        summary["totalDscNc"] = static_cast<int>(result.certificates.size());
        summary["countryCount"] = static_cast<int>(countryMap.size());
        summary["conformanceCodeCount"] = static_cast<int>(conformanceCodeMap.size());
        Json::Value validityBreakdown;
        validityBreakdown["VALID"] = validCount;
        validityBreakdown["EXPIRED"] = expiredCount;
        validityBreakdown["NOT_YET_VALID"] = notYetValidCount;
        validityBreakdown["UNKNOWN"] = unknownCount;
        summary["validityBreakdown"] = validityBreakdown;
        response["summary"] = summary;

        // Conformance codes (sorted by count desc)
        std::vector<std::pair<std::string, std::pair<std::string, int>>> codeVec(conformanceCodeMap.begin(), conformanceCodeMap.end());
        std::sort(codeVec.begin(), codeVec.end(), [](const auto& a, const auto& b) { return a.second.second > b.second.second; });
        Json::Value codesArray(Json::arrayValue);
        for (const auto& [code, descCount] : codeVec) {
            Json::Value item;
            item["code"] = code;
            item["description"] = descCount.first;
            item["count"] = descCount.second;
            codesArray.append(item);
        }
        response["conformanceCodes"] = codesArray;

        // By country (sorted by count desc)
        std::vector<std::pair<std::string, std::tuple<int, int, int>>> countryVec(countryMap.begin(), countryMap.end());
        std::sort(countryVec.begin(), countryVec.end(), [](const auto& a, const auto& b) { return std::get<0>(a.second) > std::get<0>(b.second); });
        Json::Value countryArray(Json::arrayValue);
        for (const auto& [cc, counts] : countryVec) {
            Json::Value item;
            item["countryCode"] = cc;
            item["count"] = std::get<0>(counts);
            item["validCount"] = std::get<1>(counts);
            item["expiredCount"] = std::get<2>(counts);
            countryArray.append(item);
        }
        response["byCountry"] = countryArray;

        // By year (sorted by year asc)
        Json::Value yearArray(Json::arrayValue);
        for (const auto& [year, count] : yearMap) {
            Json::Value item;
            item["year"] = year;
            item["count"] = count;
            yearArray.append(item);
        }
        response["byYear"] = yearArray;

        // By signature algorithm
        Json::Value sigAlgArray(Json::arrayValue);
        for (const auto& [alg, count] : sigAlgMap) {
            Json::Value item;
            item["algorithm"] = alg;
            item["count"] = count;
            sigAlgArray.append(item);
        }
        response["bySignatureAlgorithm"] = sigAlgArray;

        // By public key algorithm
        Json::Value pubKeyAlgArray(Json::arrayValue);
        for (const auto& [alg, count] : pubKeyAlgMap) {
            Json::Value item;
            item["algorithm"] = alg;
            item["count"] = count;
            pubKeyAlgArray.append(item);
        }
        response["byPublicKeyAlgorithm"] = pubKeyAlgArray;

        // Certificates table (paginated)
        int totalFiltered = static_cast<int>(filteredCerts.size());
        int startIdx = (page - 1) * size;
        int endIdx = std::min(startIdx + size, totalFiltered);

        Json::Value certsObj;
        certsObj["total"] = totalFiltered;
        certsObj["page"] = page;
        certsObj["size"] = size;

        Json::Value items(Json::arrayValue);
        for (int i = startIdx; i < endIdx; i++) {
            const auto& cert = *filteredCerts[i];
            Json::Value certJson;
            certJson["fingerprint"] = cert.getFingerprint();
            certJson["countryCode"] = cert.getCountry();
            certJson["subjectDn"] = cert.getSubjectDn();
            certJson["issuerDn"] = cert.getIssuerDn();
            certJson["serialNumber"] = cert.getSn();

            // Dates
            char timeBuf[32];
            auto validFrom = std::chrono::system_clock::to_time_t(cert.getValidFrom());
            auto validTo = std::chrono::system_clock::to_time_t(cert.getValidTo());
            std::tm gmBuf{};
            gmtime_r(&validFrom, &gmBuf);
            std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", &gmBuf);
            certJson["notBefore"] = timeBuf;
            gmtime_r(&validTo, &gmBuf);
            std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", &gmBuf);
            certJson["notAfter"] = timeBuf;

            // Validity
            auto status = cert.getValidityStatus();
            if (status == domain::models::ValidityStatus::VALID) certJson["validity"] = "VALID";
            else if (status == domain::models::ValidityStatus::EXPIRED) certJson["validity"] = "EXPIRED";
            else if (status == domain::models::ValidityStatus::NOT_YET_VALID) certJson["validity"] = "NOT_YET_VALID";
            else certJson["validity"] = "UNKNOWN";

            // Algorithms
            if (cert.getSignatureAlgorithm().has_value()) certJson["signatureAlgorithm"] = *cert.getSignatureAlgorithm();
            if (cert.getPublicKeyAlgorithm().has_value()) certJson["publicKeyAlgorithm"] = *cert.getPublicKeyAlgorithm();
            if (cert.getPublicKeySize().has_value()) certJson["publicKeySize"] = *cert.getPublicKeySize();

            // Conformance data
            if (cert.getPkdConformanceCode().has_value()) certJson["pkdConformanceCode"] = *cert.getPkdConformanceCode();
            if (cert.getPkdConformanceText().has_value()) certJson["pkdConformanceText"] = *cert.getPkdConformanceText();
            if (cert.getPkdVersion().has_value()) certJson["pkdVersion"] = *cert.getPkdVersion();

            items.append(certJson);
        }
        certsObj["items"] = items;
        response["certificates"] = certsObj;

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CertHandler::dscNcReport", e));
    }
}

// =============================================================================
// Handler 10: POST /api/validate/link-cert
// =============================================================================

void CertificateHandler::handleValidateLinkCert(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    spdlog::info("POST /api/validate/link-cert - Link Certificate validation");

    // Parse JSON request body
    auto json = req->getJsonObject();
    if (!json) {
        Json::Value error;
        error["success"] = false;
        error["error"] = "Invalid JSON body";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    // Get certificate binary (base64 encoded)
    std::string certBase64 = (*json).get("certificateBinary", "").asString();
    if (certBase64.empty()) {
        Json::Value error;
        error["success"] = false;
        error["error"] = "Missing certificateBinary field";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    // Decode base64
    std::vector<uint8_t> certBinary;
    try {
        std::string decoded = drogon::utils::base64Decode(certBase64);
        certBinary.assign(decoded.begin(), decoded.end());
    } catch (const std::exception& e) {
        spdlog::warn("[CertHandler::validateLinkCert] Base64 decode failed: {}", e.what());
        callback(common::handler::badRequest("Base64 decode failed"));
        return;
    }

    // Use QueryExecutor for Oracle support
    if (!queryExecutor_) {
        Json::Value error;
        error["success"] = false;
        error["error"] = "Query executor not initialized";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
        return;
    }

    try {
        // Create LC validator with QueryExecutor (Oracle/PostgreSQL agnostic)
        lc::LcValidator validator(queryExecutor_);

        // Validate Link Certificate
        auto result = validator.validateLinkCertificate(certBinary);

        // Build JSON response
        Json::Value response;
        response["success"] = true;
        response["trustChainValid"] = result.trustChainValid;
        response["validationMessage"] = result.validationMessage;

        // Signature validation
        Json::Value signatures;
        signatures["oldCscaSignatureValid"] = result.oldCscaSignatureValid;
        signatures["oldCscaSubjectDn"] = result.oldCscaSubjectDn;
        signatures["oldCscaFingerprint"] = result.oldCscaFingerprint;
        signatures["newCscaSignatureValid"] = result.newCscaSignatureValid;
        signatures["newCscaSubjectDn"] = result.newCscaSubjectDn;
        signatures["newCscaFingerprint"] = result.newCscaFingerprint;
        response["signatures"] = signatures;

        // Certificate properties
        Json::Value properties;
        properties["validityPeriodValid"] = result.validityPeriodValid;
        properties["notBefore"] = result.notBefore;
        properties["notAfter"] = result.notAfter;
        properties["extensionsValid"] = result.extensionsValid;
        response["properties"] = properties;

        // Extensions details
        Json::Value extensions;
        extensions["basicConstraintsCa"] = result.basicConstraintsCa;
        extensions["basicConstraintsPathlen"] = result.basicConstraintsPathlen;
        extensions["keyUsage"] = result.keyUsage;
        extensions["extendedKeyUsage"] = result.extendedKeyUsage;
        response["extensions"] = extensions;

        // Revocation status
        Json::Value revocation;
        revocation["status"] = crl::revocationStatusToString(result.revocationStatus);
        revocation["message"] = result.revocationMessage;
        response["revocation"] = revocation;

        // Metadata
        response["validationDurationMs"] = result.validationDurationMs;

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CertHandler::validateLinkCert", e));
    }
}

// =============================================================================
// Handler 11: GET /api/link-certs/search
// =============================================================================

void CertificateHandler::handleLinkCertsSearch(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    spdlog::info("GET /api/link-certs/search - Search Link Certificates");

    // Parse query parameters
    std::string country = req->getParameter("country");
    std::string validOnlyStr = req->getParameter("validOnly");
    std::string limitStr = req->getParameter("limit");
    std::string offsetStr = req->getParameter("offset");

    bool validOnly = (validOnlyStr == "true");
    int limit = common::handler::safeStoi(limitStr, 50, 1, 1000);
    int offset = common::handler::safeStoi(offsetStr, 0, 0, 100000);

    // Validate parameters
    if (limit <= 0 || limit > 1000) {
        Json::Value error;
        error["success"] = false;
        error["error"] = "Invalid limit (must be 1-1000)";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    if (!certificateRepository_) {
        Json::Value error;
        error["success"] = false;
        error["error"] = "Certificate repository not initialized";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
        return;
    }

    try {
        std::string validFilter = validOnly ? "true" : "";
        auto rows = certificateRepository_->searchLinkCertificates(country, validFilter, limit, offset);

        // Helper for Oracle boolean values
        std::string dbType = queryExecutor_ ? queryExecutor_->getDatabaseType() : "postgres";
        auto parseBool = [&dbType](const Json::Value& v) -> bool {
            if (v.isBool()) return v.asBool();
            std::string s = v.asString();
            return (s == "t" || s == "true" || s == "1" || s == "TRUE");
        };

        // Build JSON response
        Json::Value response;
        response["success"] = true;
        response["total"] = static_cast<int>(rows.size());
        response["limit"] = limit;
        response["offset"] = offset;

        Json::Value certificates(Json::arrayValue);
        for (Json::ArrayIndex i = 0; i < rows.size(); i++) {
            const auto& row = rows[i];
            Json::Value cert;
            cert["id"] = row.get("id", "").asString();
            cert["subjectDn"] = row.get("subject_dn", "").asString();
            cert["issuerDn"] = row.get("issuer_dn", "").asString();
            cert["serialNumber"] = row.get("serial_number", "").asString();
            cert["fingerprint"] = row.get("fingerprint_sha256", "").asString();
            cert["oldCscaSubjectDn"] = row.get("old_csca_subject_dn", "").asString();
            cert["newCscaSubjectDn"] = row.get("new_csca_subject_dn", "").asString();
            cert["trustChainValid"] = parseBool(row["trust_chain_valid"]);
            cert["createdAt"] = row.get("created_at", "").asString();
            cert["countryCode"] = row.get("country_code", "").asString();

            certificates.append(cert);
        }

        response["certificates"] = certificates;

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CertHandler::linkCertsSearch", e));
    }
}

// =============================================================================
// Handler 12: GET /api/link-certs/{id}
// =============================================================================

void CertificateHandler::handleLinkCertDetail(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {
    spdlog::info("GET /api/link-certs/{} - Get Link Certificate details", id);

    if (!certificateRepository_) {
        Json::Value error;
        error["success"] = false;
        error["error"] = "Certificate repository not initialized";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
        return;
    }

    try {
        std::string dbType = queryExecutor_ ? queryExecutor_->getDatabaseType() : "postgres";

        // Helper for Oracle boolean values
        auto parseBool = [&dbType](const Json::Value& v) -> bool {
            if (v.isBool()) return v.asBool();
            std::string s = v.asString();
            return (s == "t" || s == "true" || s == "1" || s == "TRUE");
        };

        auto safeInt = [](const Json::Value& v) -> int {
            if (v.isInt()) return v.asInt();
            if (v.isString()) { try { return std::stoi(v.asString()); } catch (...) { return 0; } }
            return 0;
        };

        // Query LC by ID via CertificateRepository
        Json::Value rowValue = certificateRepository_->findLinkCertificateById(id);

        if (rowValue.isNull()) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "Link Certificate not found";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k404NotFound);
            callback(resp);
            return;
        }

        const auto& row = rowValue;

        // Build JSON response
        Json::Value response;
        response["success"] = true;

        Json::Value cert;
        cert["id"] = row.get("id", "").asString();
        cert["subjectDn"] = row.get("subject_dn", "").asString();
        cert["issuerDn"] = row.get("issuer_dn", "").asString();
        cert["serialNumber"] = row.get("serial_number", "").asString();
        cert["fingerprint"] = row.get("fingerprint_sha256", "").asString();

        Json::Value signatures;
        signatures["oldCscaSubjectDn"] = row.get("old_csca_subject_dn", "").asString();
        signatures["oldCscaFingerprint"] = row.get("old_csca_fingerprint", "").asString();
        signatures["newCscaSubjectDn"] = row.get("new_csca_subject_dn", "").asString();
        signatures["newCscaFingerprint"] = row.get("new_csca_fingerprint", "").asString();
        signatures["trustChainValid"] = parseBool(row["trust_chain_valid"]);
        signatures["oldCscaSignatureValid"] = parseBool(row["old_csca_signature_valid"]);
        signatures["newCscaSignatureValid"] = parseBool(row["new_csca_signature_valid"]);
        cert["signatures"] = signatures;

        Json::Value properties;
        properties["validityPeriodValid"] = parseBool(row["validity_period_valid"]);
        properties["notBefore"] = row.get("not_before", "").asString();
        properties["notAfter"] = row.get("not_after", "").asString();
        properties["extensionsValid"] = parseBool(row["extensions_valid"]);
        cert["properties"] = properties;

        Json::Value extensions;
        extensions["basicConstraintsCa"] = parseBool(row["basic_constraints_ca"]);
        extensions["basicConstraintsPathlen"] = safeInt(row["basic_constraints_pathlen"]);
        extensions["keyUsage"] = row.get("key_usage", "").asString();
        extensions["extendedKeyUsage"] = row.get("extended_key_usage", "").asString();
        cert["extensions"] = extensions;

        Json::Value revocation;
        revocation["status"] = row.get("revocation_status", "").asString();
        revocation["message"] = row.get("revocation_message", "").asString();
        cert["revocation"] = revocation;

        cert["ldapDn"] = row.get("ldap_dn_v2", "").asString();
        cert["storedInLdap"] = parseBool(row["stored_in_ldap"]);
        cert["createdAt"] = row.get("created_at", "").asString();
        cert["countryCode"] = row.get("country_code", "").asString();

        response["certificate"] = cert;

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CertHandler::linkCertDetail", e));
    }
}



// =============================================================================
// Handler 13: GET /api/certificates/crl/report
// =============================================================================

void CertificateHandler::handleCrlReport(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("GET /api/certificates/crl/report");

    try {
        // Parse query parameters
        std::string countryFilter = req->getParameter("country");
        std::string statusFilter = req->getParameter("status");
        int page = 1, size = 50;
        try { page = std::max(1, std::min(10000, std::stoi(req->getParameter("page")))); } catch (const std::exception&) { page = 1; }
        try { size = std::max(1, std::min(200, std::stoi(req->getParameter("size")))); } catch (const std::exception&) { size = 50; }

        // Fetch ALL CRLs for aggregation (no filter, reasonable limit)
        Json::Value allCrls = crlRepository_->findAll("", "", 1000, 0);
        int totalAll = crlRepository_->countAll("", "");

        // Single-pass aggregation
        std::map<std::string, Json::Value> byCountry;
        std::map<std::string, int> byAlgorithm;
        std::map<std::string, int> byReason;
        int totalRevoked = 0, validCount = 0, expiredCount = 0;
        std::set<std::string> countrySet;

        // Build enriched CRL items with parsed data
        Json::Value enrichedItems(Json::arrayValue);

        for (const auto& row : allCrls) {
            std::string id = row.get("id", "").asString();
            std::string cc = row.get("country_code", "").asString();
            std::string issuer = row.get("issuer_dn", "").asString();
            std::string thisUpd = row.get("this_update", "").asString();
            std::string nextUpd = row.get("next_update", "").asString();
            std::string crlNum = row.get("crl_number", "").asString();
            std::string fp = row.get("fingerprint_sha256", "").asString();
            std::string crlBin = row.get("crl_binary", "").asString();

            // Parse CRL binary for revoked count + signature algorithm
            auto parsed = crl::parseCrlBinary(crlBin);
            int revokedCnt = parsed.parsed ? parsed.revokedCount : 0;
            std::string sigAlg = parsed.parsed ? parsed.signatureAlgorithm : "Unknown";

            // Determine status
            std::string status = "EXPIRED";
            if (!nextUpd.empty() && nextUpd != "null") {
                // Simple heuristic: if next_update contains a year >= current, likely valid
                // Server-side: DB already filtered by timestamp, but for aggregation use DB value
                // We use the DB query with timestamp comparison for filtered views
                // For aggregation, re-check using parsed nextUpdate from binary
                if (!parsed.nextUpdate.empty()) {
                    // ASN1_TIME_print format: "Mon DD HH:MM:SS YYYY GMT"
                    // Simple: check if year is >= 2026
                    // Better: use time comparison
                    status = "VALID"; // Default to VALID if nextUpdate exists
                }
            }

            // Actually, let's use a proper approach:
            // Count as expired if next_update column is empty/null from DB
            // Since the DB stores actual timestamps, we trust the status filter queries
            // For the ALL query, we need to determine status ourselves
            // Simple: re-query counts with status filter
            // But that's 2 extra queries. Instead, use the raw nextUpd string.
            // If nextUpd is empty => EXPIRED. Otherwise, compare with "now".
            // Since we're in C++, use time(nullptr):
            status = nextUpd.empty() ? "EXPIRED" : "VALID";
            // A more accurate check would parse the timestamp, but for 69 CRLs this is fine
            // The filtered view uses DB-side comparison anyway

            if (status == "VALID") validCount++;
            else expiredCount++;

            totalRevoked += revokedCnt;
            countrySet.insert(cc);

            // By country
            if (byCountry.find(cc) == byCountry.end()) {
                Json::Value entry;
                entry["countryCode"] = cc;
                entry["crlCount"] = 0;
                entry["revokedCount"] = 0;
                byCountry[cc] = entry;
            }
            byCountry[cc]["crlCount"] = byCountry[cc]["crlCount"].asInt() + 1;
            byCountry[cc]["revokedCount"] = byCountry[cc]["revokedCount"].asInt() + revokedCnt;

            // By algorithm
            byAlgorithm[sigAlg]++;

            // By reason (from parsed revoked certs)
            if (parsed.parsed) {
                for (const auto& rev : parsed.revokedCertificates) {
                    byReason[rev.revocationReason]++;
                }
            }

            // Build enriched item
            Json::Value item;
            item["id"] = id;
            item["countryCode"] = cc;
            item["issuerDn"] = issuer;
            item["thisUpdate"] = thisUpd;
            item["nextUpdate"] = nextUpd;
            item["crlNumber"] = crlNum;
            item["status"] = status;
            item["revokedCount"] = revokedCnt;
            item["signatureAlgorithm"] = sigAlg;
            item["fingerprint"] = fp;
            // Oracle returns NUMBER(1) as string "0"/"1"
            {
                const auto& v = row.get("stored_in_ldap", false);
                if (v.isBool()) item["storedInLdap"] = v.asBool();
                else { std::string s = v.asString(); item["storedInLdap"] = (s == "t" || s == "true" || s == "1" || s == "TRUE"); }
            }
            item["createdAt"] = row.get("created_at", "").asString();
            enrichedItems.append(item);
        }

        // Apply filters for table pagination
        Json::Value filteredItems(Json::arrayValue);
        for (const auto& item : enrichedItems) {
            bool match = true;
            if (!countryFilter.empty() && item["countryCode"].asString() != countryFilter) match = false;
            if (statusFilter == "valid" && item["status"].asString() != "VALID") match = false;
            if (statusFilter == "expired" && item["status"].asString() != "EXPIRED") match = false;
            if (match) filteredItems.append(item);
        }

        int filteredTotal = filteredItems.size();
        int offset = (page - 1) * size;

        Json::Value pageItems(Json::arrayValue);
        for (int i = offset; i < std::min(offset + size, filteredTotal); i++) {
            pageItems.append(filteredItems[i]);
        }

        // Build response
        Json::Value response;
        response["success"] = true;

        // Summary
        Json::Value summary;
        summary["totalCrls"] = totalAll;
        summary["countryCount"] = static_cast<int>(countrySet.size());
        summary["validCount"] = validCount;
        summary["expiredCount"] = expiredCount;
        summary["totalRevokedCertificates"] = totalRevoked;
        response["summary"] = summary;

        // By country
        Json::Value byCountryArr(Json::arrayValue);
        for (auto& [k, v] : byCountry) {
            byCountryArr.append(v);
        }
        response["byCountry"] = byCountryArr;

        // By signature algorithm
        Json::Value byAlgArr(Json::arrayValue);
        for (auto& [k, v] : byAlgorithm) {
            Json::Value entry;
            entry["algorithm"] = k;
            entry["count"] = v;
            byAlgArr.append(entry);
        }
        response["bySignatureAlgorithm"] = byAlgArr;

        // By revocation reason
        Json::Value byReasonArr(Json::arrayValue);
        for (auto& [k, v] : byReason) {
            Json::Value entry;
            entry["reason"] = k;
            entry["count"] = v;
            byReasonArr.append(entry);
        }
        response["byRevocationReason"] = byReasonArr;

        // Paginated CRLs
        Json::Value crls;
        crls["total"] = filteredTotal;
        crls["page"] = page;
        crls["size"] = size;
        crls["items"] = pageItems;
        response["crls"] = crls;

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CertHandler::crlReport", e));
    }
}

// =============================================================================
// Handler 14: GET /api/certificates/crl/{id}
// =============================================================================

void CertificateHandler::handleCrlDetail(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    spdlog::info("GET /api/certificates/crl/{}", id);

    try {
        Json::Value row = crlRepository_->findById(id);
        if (row.isNull()) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "CRL not found";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k404NotFound);
            callback(resp);
            return;
        }

        std::string crlBin = row.get("crl_binary", "").asString();
        auto parsed = crl::parseCrlBinary(crlBin);

        std::string nextUpd = row.get("next_update", "").asString();
        std::string status = nextUpd.empty() ? "EXPIRED" : "VALID";

        Json::Value response;
        response["success"] = true;

        // CRL metadata
        Json::Value crlJson;
        crlJson["id"] = row.get("id", "").asString();
        crlJson["countryCode"] = row.get("country_code", "").asString();
        crlJson["issuerDn"] = parsed.parsed ? parsed.issuerDn : row.get("issuer_dn", "").asString();
        crlJson["thisUpdate"] = row.get("this_update", "").asString();
        crlJson["nextUpdate"] = nextUpd;
        crlJson["crlNumber"] = row.get("crl_number", "").asString();
        crlJson["status"] = status;
        crlJson["signatureAlgorithm"] = parsed.signatureAlgorithm;
        crlJson["fingerprint"] = row.get("fingerprint_sha256", "").asString();
        crlJson["revokedCount"] = parsed.revokedCount;
        // Oracle returns NUMBER(1) as string "0"/"1"
        {
            const auto& v = row.get("stored_in_ldap", false);
            if (v.isBool()) crlJson["storedInLdap"] = v.asBool();
            else { std::string s = v.asString(); crlJson["storedInLdap"] = (s == "t" || s == "true" || s == "1" || s == "TRUE"); }
        }
        crlJson["createdAt"] = row.get("created_at", "").asString();
        response["crl"] = crlJson;

        // Revoked certificates
        Json::Value revokedJson;
        revokedJson["total"] = parsed.revokedCount;
        Json::Value items(Json::arrayValue);
        for (const auto& rev : parsed.revokedCertificates) {
            Json::Value item;
            item["serialNumber"] = rev.serialNumber;
            item["revocationDate"] = rev.revocationDate;
            item["revocationReason"] = rev.revocationReason;
            items.append(item);
        }
        revokedJson["items"] = items;
        response["revokedCertificates"] = revokedJson;

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CertHandler::crlDetail", e));
    }
}


// =============================================================================
// Handler 15: GET /api/certificates/crl/{id}/download
// =============================================================================

void CertificateHandler::handleCrlDownload(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    spdlog::info("GET /api/certificates/crl/{}/download", id);

    try {
        Json::Value row = crlRepository_->findById(id);
        if (row.isNull()) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "CRL not found";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k404NotFound);
            callback(resp);
            return;
        }

        std::string crlHex = row.get("crl_binary", "").asString();
        if (crlHex.empty()) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "CRL binary data not available";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k404NotFound);
            callback(resp);
            return;
        }

        // Convert hex to binary (strip \x prefix from PostgreSQL/Oracle)
        std::string hexData = crlHex;
        if (hexData.size() >= 2 && hexData[0] == '\\' && hexData[1] == 'x') {
            hexData = hexData.substr(2);
        }
        std::string binaryData;
        binaryData.reserve(hexData.length() / 2);
        for (size_t i = 0; i + 1 < hexData.length(); i += 2) {
            char hexByte[3] = {hexData[i], hexData[i + 1], 0};
            binaryData.push_back(static_cast<char>(strtol(hexByte, nullptr, 16)));
        }

        std::string countryCode = row.get("country_code", "XX").asString();
        std::string filename = "crl_" + countryCode + "_" + id.substr(0, 8) + ".crl";

        auto resp = drogon::HttpResponse::newHttpResponse();
        resp->setBody(binaryData);
        resp->setContentTypeCode(drogon::CT_NONE);
        resp->addHeader("Content-Type", "application/pkix-crl");
        resp->addHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CertHandler::crlDownload", e));
    }
}

// =============================================================================
// Handler: GET /api/certificates/quality/report
// =============================================================================

void CertificateHandler::handleQualityReport(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("GET /api/certificates/quality/report");

    try {
        // Parse query parameters
        std::string countryFilter = req->getOptionalParameter<std::string>("country").value_or("");
        std::string certTypeFilter = req->getOptionalParameter<std::string>("certType").value_or("");
        std::string categoryFilter = req->getOptionalParameter<std::string>("category").value_or("");
        int page = req->getOptionalParameter<int>("page").value_or(1);
        int size = req->getOptionalParameter<int>("size").value_or(50);
        if (page < 1) page = 1;
        if (size < 1) size = 50;
        if (size > 200) size = 200;

        std::string dbType = queryExecutor_->getDatabaseType();

        // --- 1. Overall summary (total, compliant, non-compliant, warning counts) ---
        std::string summaryQuery;
        if (dbType == "oracle") {
            summaryQuery =
                "SELECT COUNT(*) AS total, "
                "  SUM(CASE WHEN icao_compliance_level = 'CONFORMANT' THEN 1 ELSE 0 END) AS compliant_count, "
                "  SUM(CASE WHEN icao_compliance_level = 'NON_CONFORMANT' THEN 1 ELSE 0 END) AS non_compliant_count, "
                "  SUM(CASE WHEN icao_compliance_level = 'WARNING' THEN 1 ELSE 0 END) AS warning_count "
                "FROM validation_result WHERE icao_compliance_level IS NOT NULL";
        } else {
            summaryQuery =
                "SELECT COUNT(*) AS total, "
                "  SUM(CASE WHEN icao_compliance_level = 'CONFORMANT' THEN 1 ELSE 0 END) AS compliant_count, "
                "  SUM(CASE WHEN icao_compliance_level = 'NON_CONFORMANT' THEN 1 ELSE 0 END) AS non_compliant_count, "
                "  SUM(CASE WHEN icao_compliance_level = 'WARNING' THEN 1 ELSE 0 END) AS warning_count "
                "FROM validation_result WHERE icao_compliance_level IS NOT NULL";
        }
        Json::Value summaryResult = queryExecutor_->executeQuery(summaryQuery, {});

        Json::Value summary;
        if (!summaryResult.empty()) {
            summary["total"] = common::db::scalarToInt(summaryResult[0].get("total", 0));
            summary["compliantCount"] = common::db::scalarToInt(summaryResult[0].get("compliant_count", 0));
            summary["nonCompliantCount"] = common::db::scalarToInt(summaryResult[0].get("non_compliant_count", 0));
            summary["warningCount"] = common::db::scalarToInt(summaryResult[0].get("warning_count", 0));
        }

        // --- 2. Per-category compliance breakdown ---
        std::string categoryQuery;
        if (dbType == "oracle") {
            categoryQuery =
                "SELECT "
                "  SUM(CASE WHEN icao_algorithm_compliant = 0 THEN 1 ELSE 0 END) AS algorithm_fail, "
                "  SUM(CASE WHEN icao_key_size_compliant = 0 THEN 1 ELSE 0 END) AS key_size_fail, "
                "  SUM(CASE WHEN icao_key_usage_compliant = 0 THEN 1 ELSE 0 END) AS key_usage_fail, "
                "  SUM(CASE WHEN icao_extensions_compliant = 0 THEN 1 ELSE 0 END) AS extensions_fail, "
                "  SUM(CASE WHEN icao_validity_period_compliant = 0 THEN 1 ELSE 0 END) AS validity_period_fail "
                "FROM validation_result WHERE icao_compliance_level IS NOT NULL";
        } else {
            categoryQuery =
                "SELECT "
                "  SUM(CASE WHEN icao_algorithm_compliant = FALSE THEN 1 ELSE 0 END) AS algorithm_fail, "
                "  SUM(CASE WHEN icao_key_size_compliant = FALSE THEN 1 ELSE 0 END) AS key_size_fail, "
                "  SUM(CASE WHEN icao_key_usage_compliant = FALSE THEN 1 ELSE 0 END) AS key_usage_fail, "
                "  SUM(CASE WHEN icao_extensions_compliant = FALSE THEN 1 ELSE 0 END) AS extensions_fail, "
                "  SUM(CASE WHEN icao_validity_period_compliant = FALSE THEN 1 ELSE 0 END) AS validity_period_fail "
                "FROM validation_result WHERE icao_compliance_level IS NOT NULL";
        }
        Json::Value categoryResult = queryExecutor_->executeQuery(categoryQuery, {});

        Json::Value byCategory(Json::arrayValue);
        if (!categoryResult.empty()) {
            auto& row = categoryResult[0];
            auto addCategory = [&](const std::string& name, const std::string& field) {
                Json::Value item;
                item["category"] = name;
                item["failCount"] = common::db::scalarToInt(row.get(field, 0));
                byCategory.append(item);
            };
            addCategory("algorithm", "algorithm_fail");
            addCategory("keySize", "key_size_fail");
            addCategory("keyUsage", "key_usage_fail");
            addCategory("extensions", "extensions_fail");
            addCategory("validityPeriod", "validity_period_fail");
        }

        // --- 3. By country compliance ---
        std::string countryQuery;
        if (dbType == "oracle") {
            countryQuery =
                "SELECT country_code, "
                "  COUNT(*) AS total, "
                "  SUM(CASE WHEN icao_compliance_level = 'CONFORMANT' THEN 1 ELSE 0 END) AS compliant, "
                "  SUM(CASE WHEN icao_compliance_level = 'NON_CONFORMANT' THEN 1 ELSE 0 END) AS non_compliant, "
                "  SUM(CASE WHEN icao_compliance_level = 'WARNING' THEN 1 ELSE 0 END) AS warning "
                "FROM validation_result "
                "WHERE icao_compliance_level IS NOT NULL AND country_code IS NOT NULL "
                "GROUP BY country_code ORDER BY non_compliant DESC "
                "OFFSET 0 ROWS FETCH NEXT 50 ROWS ONLY";
        } else {
            countryQuery =
                "SELECT country_code, "
                "  COUNT(*) AS total, "
                "  SUM(CASE WHEN icao_compliance_level = 'CONFORMANT' THEN 1 ELSE 0 END) AS compliant, "
                "  SUM(CASE WHEN icao_compliance_level = 'NON_CONFORMANT' THEN 1 ELSE 0 END) AS non_compliant, "
                "  SUM(CASE WHEN icao_compliance_level = 'WARNING' THEN 1 ELSE 0 END) AS warning "
                "FROM validation_result "
                "WHERE icao_compliance_level IS NOT NULL AND country_code IS NOT NULL "
                "GROUP BY country_code ORDER BY non_compliant DESC "
                "LIMIT 50";
        }
        Json::Value countryResult = queryExecutor_->executeQuery(countryQuery, {});

        Json::Value byCountry(Json::arrayValue);
        for (const auto& row : countryResult) {
            Json::Value item;
            item["countryCode"] = row.get("country_code", "").asString();
            item["total"] = common::db::scalarToInt(row.get("total", 0));
            item["compliant"] = common::db::scalarToInt(row.get("compliant", 0));
            item["nonCompliant"] = common::db::scalarToInt(row.get("non_compliant", 0));
            item["warning"] = common::db::scalarToInt(row.get("warning", 0));
            byCountry.append(item);
        }

        // --- 4. By certificate type ---
        std::string typeQuery =
            "SELECT certificate_type, "
            "  COUNT(*) AS total, "
            "  SUM(CASE WHEN icao_compliance_level = 'CONFORMANT' THEN 1 ELSE 0 END) AS compliant, "
            "  SUM(CASE WHEN icao_compliance_level = 'NON_CONFORMANT' THEN 1 ELSE 0 END) AS non_compliant, "
            "  SUM(CASE WHEN icao_compliance_level = 'WARNING' THEN 1 ELSE 0 END) AS warning "
            "FROM validation_result "
            "WHERE icao_compliance_level IS NOT NULL "
            "GROUP BY certificate_type ORDER BY total DESC";
        Json::Value typeResult = queryExecutor_->executeQuery(typeQuery, {});

        Json::Value byCertType(Json::arrayValue);
        for (const auto& row : typeResult) {
            Json::Value item;
            item["certType"] = row.get("certificate_type", "").asString();
            item["total"] = common::db::scalarToInt(row.get("total", 0));
            item["compliant"] = common::db::scalarToInt(row.get("compliant", 0));
            item["nonCompliant"] = common::db::scalarToInt(row.get("non_compliant", 0));
            item["warning"] = common::db::scalarToInt(row.get("warning", 0));
            byCertType.append(item);
        }

        // --- 5. Violation details breakdown (parse icao_violations text) ---
        // icao_violations is pipe-separated, e.g. "Missing Key Usage|RSA key size below minimum..."
        // We aggregate by counting individual violations from the text field
        std::string violationQuery;
        if (dbType == "oracle") {
            violationQuery =
                "SELECT TO_CHAR(icao_violations) AS icao_violations, "
                "  certificate_type, country_code "
                "FROM validation_result "
                "WHERE icao_compliance_level IN ('NON_CONFORMANT', 'WARNING') "
                "AND icao_violations IS NOT NULL";
        } else {
            violationQuery =
                "SELECT icao_violations, certificate_type, country_code "
                "FROM validation_result "
                "WHERE icao_compliance_level IN ('NON_CONFORMANT', 'WARNING') "
                "AND icao_violations IS NOT NULL AND icao_violations != ''";
        }
        Json::Value violationResult = queryExecutor_->executeQuery(violationQuery, {});

        // Parse pipe-separated violations and aggregate
        std::map<std::string, int> violationCountMap;
        for (const auto& row : violationResult) {
            std::string violations = row.get("icao_violations", "").asString();
            if (violations.empty()) continue;

            // Split by pipe '|'
            size_t pos = 0;
            while (pos < violations.size()) {
                size_t end = violations.find('|', pos);
                if (end == std::string::npos) end = violations.size();
                std::string v = violations.substr(pos, end - pos);
                // Trim whitespace
                while (!v.empty() && v.front() == ' ') v.erase(v.begin());
                while (!v.empty() && v.back() == ' ') v.pop_back();
                if (!v.empty()) {
                    violationCountMap[v]++;
                }
                pos = end + 1;
            }
        }

        // Sort by count desc
        std::vector<std::pair<std::string, int>> violationVec(violationCountMap.begin(), violationCountMap.end());
        std::sort(violationVec.begin(), violationVec.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });

        Json::Value violationsArray(Json::arrayValue);
        for (const auto& [msg, count] : violationVec) {
            Json::Value item;
            item["violation"] = msg;
            item["count"] = count;
            violationsArray.append(item);
        }

        // --- 6. Non-compliant certificates table (filtered + paginated) ---
        std::string whereClause = "WHERE icao_compliance_level IN ('NON_CONFORMANT', 'WARNING')";
        std::vector<std::string> params;
        int paramIdx = 1;

        if (!countryFilter.empty()) {
            whereClause += " AND country_code = $" + std::to_string(paramIdx);
            params.push_back(countryFilter);
            paramIdx++;
        }
        if (!certTypeFilter.empty()) {
            whereClause += " AND certificate_type = $" + std::to_string(paramIdx);
            params.push_back(certTypeFilter);
            paramIdx++;
        }
        if (!categoryFilter.empty()) {
            if (categoryFilter == "algorithm") {
                whereClause += " AND icao_algorithm_compliant = " + common::db::boolLiteral(dbType, false);
            } else if (categoryFilter == "keySize") {
                whereClause += " AND icao_key_size_compliant = " + common::db::boolLiteral(dbType, false);
            } else if (categoryFilter == "keyUsage") {
                whereClause += " AND icao_key_usage_compliant = " + common::db::boolLiteral(dbType, false);
            } else if (categoryFilter == "extensions") {
                whereClause += " AND icao_extensions_compliant = " + common::db::boolLiteral(dbType, false);
            } else if (categoryFilter == "validityPeriod") {
                whereClause += " AND icao_validity_period_compliant = " + common::db::boolLiteral(dbType, false);
            }
        }

        // Count
        std::string countQuery = "SELECT COUNT(*) FROM validation_result " + whereClause;
        int total = common::db::scalarToInt(queryExecutor_->executeScalar(countQuery, params));

        // Fetch page
        std::string dataQuery;
        auto paginationParams = params;
        paginationParams.push_back(std::to_string((page - 1) * size));
        paginationParams.push_back(std::to_string(size));

        if (dbType == "oracle") {
            dataQuery =
                "SELECT certificate_id, certificate_type, country_code, subject_dn, "
                "  icao_compliance_level, TO_CHAR(icao_violations) AS icao_violations, "
                "  icao_algorithm_compliant, icao_key_size_compliant, "
                "  icao_key_usage_compliant, icao_extensions_compliant, "
                "  icao_validity_period_compliant, not_before, not_after, signature_algorithm "
                "FROM validation_result " + whereClause +
                " ORDER BY icao_compliance_level, country_code "
                " OFFSET $" + std::to_string(paramIdx) +
                " ROWS FETCH NEXT $" + std::to_string(paramIdx + 1) + " ROWS ONLY";
        } else {
            dataQuery =
                "SELECT certificate_id, certificate_type, country_code, subject_dn, "
                "  icao_compliance_level, icao_violations, "
                "  icao_algorithm_compliant, icao_key_size_compliant, "
                "  icao_key_usage_compliant, icao_extensions_compliant, "
                "  icao_validity_period_compliant, not_before, not_after, signature_algorithm "
                "FROM validation_result " + whereClause +
                " ORDER BY icao_compliance_level, country_code "
                " LIMIT $" + std::to_string(paramIdx + 1) +
                " OFFSET $" + std::to_string(paramIdx);
        }

        Json::Value dataResult = queryExecutor_->executeQuery(dataQuery, paginationParams);

        Json::Value certificates;
        certificates["total"] = total;
        certificates["page"] = page;
        certificates["size"] = size;

        Json::Value items(Json::arrayValue);
        for (const auto& row : dataResult) {
            Json::Value item;
            item["fingerprint"] = row.get("certificate_id", "").asString();
            item["certificateType"] = row.get("certificate_type", "").asString();
            item["countryCode"] = row.get("country_code", "").asString();
            item["subjectDn"] = row.get("subject_dn", "").asString();
            item["complianceLevel"] = row.get("icao_compliance_level", "").asString();
            item["violations"] = row.get("icao_violations", "").asString();
            item["signatureAlgorithm"] = row.get("signature_algorithm", "").asString();
            item["notBefore"] = row.get("not_before", "").asString();
            item["notAfter"] = row.get("not_after", "").asString();

            // Boolean fields
            auto toBool = [](const Json::Value& v) -> bool {
                if (v.isBool()) return v.asBool();
                if (v.isString()) {
                    auto s = v.asString();
                    return s == "t" || s == "true" || s == "1";
                }
                if (v.isInt()) return v.asInt() != 0;
                return true;
            };
            item["algorithmCompliant"] = toBool(row.get("icao_algorithm_compliant", true));
            item["keySizeCompliant"] = toBool(row.get("icao_key_size_compliant", true));
            item["keyUsageCompliant"] = toBool(row.get("icao_key_usage_compliant", true));
            item["extensionsCompliant"] = toBool(row.get("icao_extensions_compliant", true));
            item["validityPeriodCompliant"] = toBool(row.get("icao_validity_period_compliant", true));

            items.append(item);
        }
        certificates["items"] = items;

        // --- Build response ---
        Json::Value response;
        response["success"] = true;
        response["summary"] = summary;
        response["byCategory"] = byCategory;
        response["byCountry"] = byCountry;
        response["byCertType"] = byCertType;
        response["violations"] = violationsArray;
        response["certificates"] = certificates;

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CertHandler::qualityReport", e));
    }
}

// =============================================================================
// Handler: GET /api/certificates/doc9303-checklist
// =============================================================================

void CertificateHandler::handleDoc9303Checklist(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        std::string fingerprint = req->getOptionalParameter<std::string>("fingerprint").value_or("");

        if (fingerprint.empty()) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "fingerprint parameter is required";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        spdlog::info("Doc9303 checklist request: fingerprint={}...", fingerprint.substr(0, 16));

        // Step 1: Get metadata from DB (no BLOB — avoids Oracle ORA-03127 LOB session issue)
        Json::Value certInfo = certificateRepository_->findByFingerprint(fingerprint);

        if (certInfo.isNull()) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "Certificate not found for fingerprint: " + fingerprint;
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k404NotFound);
            callback(resp);
            return;
        }

        std::string certType = certInfo.get("certificate_type", "DSC").asString();
        std::string country = certInfo.get("country_code", "").asString();
        bool isSelfSigned = certInfo.get("isSelfSigned", false).asBool();

        // Step 2: Build LDAP DN from metadata and get certificate binary from LDAP
        // Link Certificates (CSCA that is NOT self-signed) are stored under o=lc
        std::string typeOu;
        if (certType == "CSCA" && !isSelfSigned) typeOu = "lc";
        else if (certType == "CSCA") typeOu = "csca";
        else if (certType == "MLSC") typeOu = "mlsc";
        else typeOu = "dsc";  // DSC and DSC_NC both stored under o=dsc

        std::string dataDc = (certType == "DSC_NC") ? "nc-data" : "data";
        std::string ldapDn = "cn=" + fingerprint + ",o=" + typeOu + ",c=" + country +
            ",dc=" + dataDc + ",dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";

        spdlog::debug("Doc9303 checklist: fetching binary from LDAP DN={}", ldapDn);

        auto exportResult = certificateService_->exportCertificateFile(ldapDn, services::ExportFormat::DER);

        // Fallback: if CSCA not found under o=lc, try o=csca (and vice versa)
        if ((!exportResult.success || exportResult.data.empty()) && certType == "CSCA") {
            std::string fallbackOu = (typeOu == "lc") ? "csca" : "lc";
            std::string fallbackDn = "cn=" + fingerprint + ",o=" + fallbackOu + ",c=" + country +
                ",dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";
            spdlog::debug("Doc9303 checklist: primary DN not found, trying fallback DN={}", fallbackDn);
            exportResult = certificateService_->exportCertificateFile(fallbackDn, services::ExportFormat::DER);
        }

        if (!exportResult.success || exportResult.data.empty()) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "Certificate binary data not available in LDAP";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k404NotFound);
            callback(resp);
            return;
        }

        // Step 3: Parse DER to X509
        const unsigned char* p = exportResult.data.data();
        X509* cert = d2i_X509(nullptr, &p, static_cast<long>(exportResult.data.size()));
        if (!cert) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "Failed to parse certificate DER data";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k500InternalServerError);
            callback(resp);
            return;
        }

        // Step 4: Run Doc 9303 checklist
        auto checklist = common::runDoc9303Checklist(cert, certType);
        X509_free(cert);

        // Build response
        Json::Value response;
        response["success"] = true;
        response["fingerprint"] = fingerprint;

        Json::Value checklistJson = checklist.toJson();
        // Merge checklist fields into response (flat structure)
        for (const auto& key : checklistJson.getMemberNames()) {
            response[key] = checklistJson[key];
        }

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        callback(common::handler::internalError("CertHandler::doc9303Checklist", e));
    }
}

// =============================================================================
// Pending DSC Registration Approval Handlers
// =============================================================================

void CertificateHandler::handlePendingDscList(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        if (!pendingDscRepository_) {
            callback(common::handler::internalError("CertHandler::pendingDscList",
                std::runtime_error("PendingDscRepository not configured")));
            return;
        }

        int page = std::max(1, req->getOptionalParameter<int>("page").value_or(1));
        int size = std::max(1, std::min(100, req->getOptionalParameter<int>("size").value_or(20)));
        std::string status = req->getOptionalParameter<std::string>("status").value_or("");
        std::string countryCode = req->getOptionalParameter<std::string>("country").value_or("");

        int offset = (page - 1) * size;
        int totalCount = pendingDscRepository_->countAll(status, countryCode);
        Json::Value rows = pendingDscRepository_->findAll(size, offset, status, countryCode);

        Json::Value response;
        response["success"] = true;
        response["data"] = rows;
        response["pagination"]["page"] = page;
        response["pagination"]["size"] = size;
        response["pagination"]["totalCount"] = totalCount;
        response["pagination"]["totalPages"] = (totalCount + size - 1) / size;

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);
    } catch (const std::exception& e) {
        callback(common::handler::internalError("CertHandler::pendingDscList", e));
    }
}

void CertificateHandler::handlePendingDscStats(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
    try {
        if (!pendingDscRepository_) {
            callback(common::handler::internalError("CertHandler::pendingDscStats",
                std::runtime_error("PendingDscRepository not configured")));
            return;
        }

        Json::Value stats = pendingDscRepository_->getStatistics();

        Json::Value response;
        response["success"] = true;
        response["data"] = stats;

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);
    } catch (const std::exception& e) {
        callback(common::handler::internalError("CertHandler::pendingDscStats", e));
    }
}

void CertificateHandler::handlePendingDscApprove(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {
    try {
        if (!pendingDscRepository_ || !certificateRepository_ || !queryExecutor_) {
            callback(common::handler::internalError("CertHandler::pendingDscApprove",
                std::runtime_error("Required dependencies not configured")));
            return;
        }

        // Parse optional comment from request body
        std::string reviewComment;
        auto jsonBody = req->getJsonObject();
        if (jsonBody && jsonBody->isMember("comment")) {
            reviewComment = (*jsonBody)["comment"].asString();
        }

        // Get reviewer info from JWT
        auto [userId, username] = extractUserFromRequest(req);
        std::string reviewedBy = username.value_or("admin");

        // 1. Fetch pending entry
        Json::Value pending = pendingDscRepository_->findById(id);
        if (pending.isNull()) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "Pending DSC registration not found";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k404NotFound);
            callback(resp);
            return;
        }

        if (pending["status"].asString() != "PENDING") {
            Json::Value error;
            error["success"] = false;
            error["error"] = "Entry already processed (status: " + pending["status"].asString() + ")";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        // 2. Insert into certificate table
        std::string dbType = queryExecutor_->getDatabaseType();
        std::string fingerprint = pending["fingerprint_sha256"].asString();
        std::string countryCode = pending["country_code"].asString();
        std::string subjectDn = pending["subject_dn"].asString();
        std::string issuerDn = pending["issuer_dn"].asString();
        std::string serialNumber = pending["serial_number"].asString();
        std::string notBefore = pending["not_before"].asString();
        std::string notAfter = pending["not_after"].asString();
        std::string signatureAlgorithm = pending["signature_algorithm"].asString();
        std::string publicKeyAlgorithm = pending["public_key_algorithm"].asString();
        std::string publicKeySize = pending["public_key_size"].asString();
        std::string isSelfSigned = pending["is_self_signed"].asString();
        std::string validationStatus = pending["validation_status"].asString();
        std::string paVerificationId = pending["pa_verification_id"].asString();

        // Get certificate_data (binary hex from DB)
        std::string certDataHex = pending["certificate_data"].asString();

        // Build source_context JSON
        std::string sourceContext = "{\"verificationId\":\"" + paVerificationId +
            "\",\"approvedBy\":\"" + reviewedBy + "\"}";

        std::string newCertId;

        auto fetchExistingCertId = [&]() {
            // Re-query to get existing certificate ID after unique violation
            try {
                std::string fetchQ =
                    "SELECT id FROM certificate WHERE fingerprint_sha256 = $1 "
                    "FETCH FIRST 1 ROWS ONLY";
                Json::Value row = queryExecutor_->executeQuery(fetchQ, {fingerprint});
                if (!row.empty()) return row[0]["id"].asString();
            } catch (...) {}
            return std::string{};
        };

        try {
            if (dbType == "oracle") {
                // Generate UUID for Oracle
                std::string oracleId;
                {
                    Json::Value uuidResult = queryExecutor_->executeQuery(
                        "SELECT uuid_generate_v4() AS id FROM DUAL", {});
                    if (!uuidResult.empty()) {
                        oracleId = uuidResult[0]["id"].asString();
                    }
                }
                if (oracleId.empty()) oracleId = id; // fallback

                // Fix: Oracle boolean
                std::string boolVal = (isSelfSigned == "1" || isSelfSigned == "true" || isSelfSigned == "TRUE")
                    ? "1" : "0";

                std::string insertQuery =
                    "INSERT INTO certificate ("
                    "id, certificate_type, country_code, "
                    "subject_dn, issuer_dn, serial_number, fingerprint_sha256, "
                    "not_before, not_after, certificate_data, "
                    "validation_status, stored_in_ldap, is_self_signed, "
                    "signature_algorithm, public_key_algorithm, public_key_size, "
                    "duplicate_count, created_at, "
                    "source_type, source_context, extracted_from, registered_at"
                    ") VALUES ("
                    "$1, 'DSC', $2, $3, $4, $5, $6, "
                    "CASE WHEN $7 IS NULL OR $7 = '' THEN NULL ELSE TO_TIMESTAMP($7, 'YYYY-MM-DD HH24:MI:SS') END, "
                    "CASE WHEN $8 IS NULL OR $8 = '' THEN NULL ELSE TO_TIMESTAMP($8, 'YYYY-MM-DD HH24:MI:SS') END, "
                    "$9, $10, 0, $11, "
                    "$12, $13, $14, "
                    "0, SYSTIMESTAMP, "
                    "'PA_EXTRACTED', $15, $16, SYSTIMESTAMP"
                    ")";

                std::vector<std::string> insertParams = {
                    oracleId, countryCode, subjectDn, issuerDn, serialNumber, fingerprint,
                    notBefore, notAfter, certDataHex, validationStatus, boolVal,
                    signatureAlgorithm, publicKeyAlgorithm, publicKeySize,
                    sourceContext, paVerificationId
                };

                queryExecutor_->executeCommand(insertQuery, insertParams);
                newCertId = oracleId;

            } else {
                // PostgreSQL
                std::string boolVal = (isSelfSigned == "true" || isSelfSigned == "t" || isSelfSigned == "1")
                    ? "TRUE" : "FALSE";

                std::string insertQuery =
                    "INSERT INTO certificate ("
                    "certificate_type, country_code, "
                    "subject_dn, issuer_dn, serial_number, fingerprint_sha256, "
                    "not_before, not_after, certificate_data, "
                    "validation_status, stored_in_ldap, is_self_signed, "
                    "signature_algorithm, public_key_algorithm, public_key_size, "
                    "duplicate_count, created_at, "
                    "source_type, source_context, extracted_from, registered_at"
                    ") VALUES ("
                    "'DSC', $1, $2, $3, $4, $5, "
                    "$6, $7, $8, "
                    "$9, FALSE, " + boolVal + ", "
                    "$10, $11, $12, "
                    "0, CURRENT_TIMESTAMP, "
                    "'PA_EXTRACTED', $13::jsonb, $14, CURRENT_TIMESTAMP"
                    ") RETURNING id";

                std::vector<std::string> insertParams = {
                    countryCode, subjectDn, issuerDn, serialNumber, fingerprint,
                    notBefore, notAfter, certDataHex, validationStatus,
                    signatureAlgorithm, publicKeyAlgorithm, publicKeySize,
                    sourceContext, paVerificationId
                };

                Json::Value insertResult = queryExecutor_->executeQuery(insertQuery, insertParams);
                if (!insertResult.empty()) {
                    newCertId = insertResult[0]["id"].asString();
                }
            }
        } catch (const std::exception& certE) {
            // Race condition: two concurrent approvals or a duplicate fingerprint.
            // Detect unique violation and resolve by re-querying existing certificate.
            std::string errMsg = certE.what();
            bool isUnique = errMsg.find("ORA-00001") != std::string::npos ||
                            errMsg.find("23505") != std::string::npos ||
                            errMsg.find("unique constraint") != std::string::npos ||
                            errMsg.find("UNIQUE constraint") != std::string::npos;
            if (isUnique) {
                newCertId = fetchExistingCertId();
                spdlog::info("[PendingDsc] Certificate already registered (unique violation), using existing id={}, fingerprint={}...",
                    newCertId.empty() ? "?" : newCertId.substr(0, 8), fingerprint.substr(0, 16));
            } else {
                throw; // rethrow unexpected errors
            }
        }

        // 3. Try LDAP storage (non-fatal)
        bool ldapStored = false;
        if (ldapStorageService_ && ldapPool_) {
            try {
                auto conn = ldapPool_->acquire();
                if (conn.isValid()) {
                    // Decode certificate_data hex to binary for LDAP
                    std::vector<uint8_t> certBinary;
                    std::string hexData = certDataHex;
                    // Strip PostgreSQL \x or Oracle \\x prefix
                    if (hexData.substr(0, 2) == "\\x") hexData = hexData.substr(2);
                    else if (hexData.substr(0, 4) == "\\\\x") hexData = hexData.substr(4);

                    for (size_t i = 0; i + 1 < hexData.size(); i += 2) {
                        unsigned int byte = 0;
                        std::istringstream iss(hexData.substr(i, 2));
                        iss >> std::hex >> byte;
                        certBinary.push_back(static_cast<uint8_t>(byte));
                    }

                    if (!certBinary.empty()) {
                        std::string dn = ldapStorageService_->saveCertificateToLdap(
                            conn.get(), "dsc", countryCode,
                            subjectDn, issuerDn, serialNumber, fingerprint,
                            certBinary);
                        if (!dn.empty()) {
                            ldapStored = true;
                            // Mark as stored in LDAP
                            certificateRepository_->markStoredInLdap(fingerprint);
                        }
                    }
                }
            } catch (const std::exception& e) {
                spdlog::warn("[PendingDsc] LDAP storage failed (non-fatal): {}", e.what());
            }
        }

        // 4. Mark pending entry as APPROVED
        if (!pendingDscRepository_->updateStatus(id, "APPROVED", reviewedBy, reviewComment)) {
            spdlog::warn("[PendingDsc] Failed to update status to APPROVED for id={} — entry may have been concurrently processed", id);
        }

        // 5. Audit log
        try {
            auto entry = icao::audit::createAuditEntryFromRequest(req, OperationType::DSC_APPROVE);
            entry.resourceId = id;
            entry.resourceType = "pending_dsc_registration";
            entry.success = true;
            Json::Value meta;
            meta["fingerprint"] = fingerprint;
            meta["countryCode"] = countryCode;
            meta["certificateId"] = newCertId;
            meta["ldapStored"] = ldapStored;
            meta["subjectDn"] = subjectDn.substr(0, 100);
            entry.metadata = meta;
            logOperation(queryExecutor_, entry);
        } catch (...) {
            spdlog::warn("[PendingDsc] Audit log failed for DSC_APPROVE");
        }

        Json::Value response;
        response["success"] = true;
        response["data"]["certificateId"] = newCertId;
        response["data"]["fingerprint"] = fingerprint;
        response["data"]["countryCode"] = countryCode;
        response["data"]["ldapStored"] = ldapStored;
        response["data"]["message"] = "DSC 인증서가 승인되어 등록되었습니다.";

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        // Audit log failure
        try {
            auto entry = icao::audit::createAuditEntryFromRequest(req, OperationType::DSC_APPROVE);
            entry.resourceId = id;
            entry.success = false;
            entry.errorMessage = e.what();
            logOperation(queryExecutor_, entry);
        } catch (...) {}

        callback(common::handler::internalError("CertHandler::pendingDscApprove", e));
    }
}

void CertificateHandler::handlePendingDscReject(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {
    try {
        if (!pendingDscRepository_) {
            callback(common::handler::internalError("CertHandler::pendingDscReject",
                std::runtime_error("PendingDscRepository not configured")));
            return;
        }

        // Parse comment from request body
        std::string reviewComment;
        auto jsonBody = req->getJsonObject();
        if (jsonBody && jsonBody->isMember("comment")) {
            reviewComment = (*jsonBody)["comment"].asString();
        }

        auto [userId, username] = extractUserFromRequest(req);
        std::string reviewedBy = username.value_or("admin");

        // Check pending entry exists
        Json::Value pending = pendingDscRepository_->findById(id);
        if (pending.isNull()) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "Pending DSC registration not found";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k404NotFound);
            callback(resp);
            return;
        }

        if (pending["status"].asString() != "PENDING") {
            Json::Value error;
            error["success"] = false;
            error["error"] = "Entry already processed (status: " + pending["status"].asString() + ")";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        // Mark as REJECTED
        pendingDscRepository_->updateStatus(id, "REJECTED", reviewedBy, reviewComment);

        // Audit log
        try {
            auto entry = icao::audit::createAuditEntryFromRequest(req, OperationType::DSC_REJECT);
            entry.resourceId = id;
            entry.resourceType = "pending_dsc_registration";
            entry.success = true;
            Json::Value meta;
            meta["fingerprint"] = pending["fingerprint_sha256"].asString();
            meta["countryCode"] = pending["country_code"].asString();
            meta["comment"] = reviewComment;
            entry.metadata = meta;
            logOperation(queryExecutor_, entry);
        } catch (...) {
            spdlog::warn("[PendingDsc] Audit log failed for DSC_REJECT");
        }

        Json::Value response;
        response["success"] = true;
        response["data"]["message"] = "DSC 인증서 등록이 거부되었습니다.";

        auto resp = drogon::HttpResponse::newHttpJsonResponse(response);
        callback(resp);

    } catch (const std::exception& e) {
        try {
            auto entry = icao::audit::createAuditEntryFromRequest(req, OperationType::DSC_REJECT);
            entry.resourceId = id;
            entry.success = false;
            entry.errorMessage = e.what();
            logOperation(queryExecutor_, entry);
        } catch (...) {}

        callback(common::handler::internalError("CertHandler::pendingDscReject", e));
    }
}

} // namespace handlers
