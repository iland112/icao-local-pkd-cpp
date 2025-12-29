#pragma once

#include <drogon/HttpController.h>
#include <drogon/HttpRequest.h>
#include <drogon/HttpResponse.h>
#include "passiveauthentication/application/usecase/PerformPassiveAuthenticationUseCase.hpp"
#include "passiveauthentication/application/command/PerformPassiveAuthenticationCommand.hpp"
#include "passiveauthentication/domain/model/DataGroupNumber.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <memory>

namespace pa::infrastructure::controller {

using namespace drogon;
using json = nlohmann::json;

/**
 * REST API Controller for Passive Authentication.
 *
 * Endpoints:
 * - POST /api/pa/verify - Perform PA verification
 * - GET /api/pa/history - Get PA verification history
 * - GET /api/pa/{id} - Get PA result details
 */
class PassiveAuthenticationController : public HttpController<PassiveAuthenticationController> {
private:
    std::shared_ptr<application::usecase::PerformPassiveAuthenticationUseCase> paUseCase_;

    // Helper to decode base64
    static std::vector<uint8_t> base64Decode(const std::string& encoded) {
        static const std::string chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

        std::vector<uint8_t> result;
        std::vector<int> T(256, -1);
        for (int i = 0; i < 64; i++) T[chars[i]] = i;

        int val = 0, valb = -8;
        for (unsigned char c : encoded) {
            if (c == '=' || c == '\n' || c == '\r') continue;
            if (T[c] == -1) continue;
            val = (val << 6) + T[c];
            valb += 6;
            if (valb >= 0) {
                result.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
                valb -= 8;
            }
        }
        return result;
    }

    // Convert time_point to ISO8601 string
    static std::string timePointToIso8601(const std::chrono::system_clock::time_point& tp) {
        auto time = std::chrono::system_clock::to_time_t(tp);
        std::tm tm = *std::localtime(&time);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
        return std::string(buf);
    }

public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(PassiveAuthenticationController::verify, "/api/pa/verify", Post);
    ADD_METHOD_TO(PassiveAuthenticationController::getHistory, "/api/pa/history", Get);
    ADD_METHOD_TO(PassiveAuthenticationController::getDetail, "/api/pa/{id}", Get);
    ADD_METHOD_TO(PassiveAuthenticationController::parseDg1, "/api/pa/parse-dg1", Post);
    ADD_METHOD_TO(PassiveAuthenticationController::parseDg2, "/api/pa/parse-dg2", Post);
    METHOD_LIST_END

    void setUseCase(std::shared_ptr<application::usecase::PerformPassiveAuthenticationUseCase> useCase) {
        paUseCase_ = std::move(useCase);
    }

    /**
     * POST /api/pa/verify
     *
     * Request body:
     * {
     *   "sod": "<base64 encoded SOD>",
     *   "dataGroups": {
     *     "DG1": "<base64>",
     *     "DG2": "<base64>"
     *   },
     *   "issuingCountry": "KR",
     *   "documentNumber": "M12345678"
     * }
     */
    void verify(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        spdlog::info("POST /api/pa/verify - Passive Authentication request received");

        try {
            auto jsonBody = req->getJsonObject();
            if (!jsonBody) {
                auto resp = HttpResponse::newHttpJsonResponse(json{
                    {"error", "Invalid JSON body"}
                });
                resp->setStatusCode(k400BadRequest);
                callback(resp);
                return;
            }

            // Parse SOD
            if (!jsonBody->isMember("sod")) {
                auto resp = HttpResponse::newHttpJsonResponse(json{
                    {"error", "Missing 'sod' field"}
                });
                resp->setStatusCode(k400BadRequest);
                callback(resp);
                return;
            }
            std::string sodBase64 = (*jsonBody)["sod"].asString();
            std::vector<uint8_t> sodBytes = base64Decode(sodBase64);

            // Parse Data Groups
            if (!jsonBody->isMember("dataGroups")) {
                auto resp = HttpResponse::newHttpJsonResponse(json{
                    {"error", "Missing 'dataGroups' field"}
                });
                resp->setStatusCode(k400BadRequest);
                callback(resp);
                return;
            }

            std::map<domain::model::DataGroupNumber, std::vector<uint8_t>> dataGroups;
            const auto& dgJson = (*jsonBody)["dataGroups"];
            for (auto it = dgJson.begin(); it != dgJson.end(); ++it) {
                std::string dgName = it.key().asString();
                std::string dgBase64 = it->asString();

                try {
                    auto dgNumber = domain::model::dataGroupNumberFromString(dgName);
                    dataGroups[dgNumber] = base64Decode(dgBase64);
                } catch (const std::exception& e) {
                    spdlog::warn("Invalid data group name: {}", dgName);
                }
            }

            if (dataGroups.empty()) {
                auto resp = HttpResponse::newHttpJsonResponse(json{
                    {"error", "At least one valid data group is required"}
                });
                resp->setStatusCode(k400BadRequest);
                callback(resp);
                return;
            }

            // Parse metadata
            std::string issuingCountry = jsonBody->get("issuingCountry", "").asString();
            std::string documentNumber = jsonBody->get("documentNumber", "").asString();

            // Get request metadata
            std::string ipAddress = req->getPeerAddr().toIp();
            std::string userAgent = req->getHeader("User-Agent");

            // Create command
            application::command::PerformPassiveAuthenticationCommand command(
                sodBytes,
                dataGroups,
                issuingCountry,
                documentNumber
            );
            command.withRequestMetadata(ipAddress, userAgent, "");

            // Execute use case
            if (!paUseCase_) {
                auto resp = HttpResponse::newHttpJsonResponse(json{
                    {"error", "PA Use Case not configured"}
                });
                resp->setStatusCode(k500InternalServerError);
                callback(resp);
                return;
            }

            auto response = paUseCase_->execute(command);

            // Build JSON response
            json result;
            result["status"] = domain::model::toString(response.getStatus());
            result["verificationId"] = response.getVerificationId();
            result["verificationTimestamp"] = timePointToIso8601(response.getVerificationTimestamp());
            result["issuingCountry"] = response.getIssuingCountry();
            result["documentNumber"] = response.getDocumentNumber();
            result["processingDurationMs"] = response.getProcessingDurationMs();

            // Certificate chain validation
            if (response.getCertificateChainValidation().has_value()) {
                const auto& cv = response.getCertificateChainValidation().value();
                result["certificateChainValidation"] = {
                    {"valid", cv.valid},
                    {"dscSubjectDn", cv.dscSubjectDn},
                    {"dscSerialNumber", cv.dscSerialNumber},
                    {"cscaSubjectDn", cv.cscaSubjectDn},
                    {"cscaSerialNumber", cv.cscaSerialNumber},
                    {"crlChecked", cv.crlChecked},
                    {"revoked", cv.revoked},
                    {"crlStatus", cv.crlStatus},
                    {"crlStatusDescription", cv.crlStatusDescription},
                    {"crlMessage", cv.crlMessage}
                };
            }

            // SOD signature validation
            if (response.getSodSignatureValidation().has_value()) {
                const auto& sv = response.getSodSignatureValidation().value();
                result["sodSignatureValidation"] = {
                    {"valid", sv.valid},
                    {"signatureAlgorithm", sv.signatureAlgorithm.value_or("")},
                    {"hashAlgorithm", sv.hashAlgorithm.value_or("")}
                };
            }

            // Data group validation
            if (response.getDataGroupValidation().has_value()) {
                const auto& dv = response.getDataGroupValidation().value();
                result["dataGroupValidation"] = {
                    {"totalGroups", dv.totalGroups},
                    {"validGroups", dv.validGroups},
                    {"invalidGroups", dv.invalidGroups}
                };

                json details = json::object();
                for (const auto& [dgNum, detail] : dv.details) {
                    details[domain::model::toString(dgNum)] = {
                        {"valid", detail.valid},
                        {"expectedHash", detail.expectedHash},
                        {"actualHash", detail.actualHash}
                    };
                }
                result["dataGroupValidation"]["details"] = details;
            }

            // Errors
            if (!response.getErrors().empty()) {
                json errorsJson = json::array();
                for (const auto& error : response.getErrors()) {
                    errorsJson.push_back({
                        {"code", error.getCode()},
                        {"message", error.getMessage()},
                        {"severity", error.getSeverityString()}
                    });
                }
                result["errors"] = errorsJson;
            }

            auto resp = HttpResponse::newHttpJsonResponse(result);
            if (response.isValid()) {
                resp->setStatusCode(k200OK);
            } else {
                resp->setStatusCode(k200OK);  // Still 200, status is in body
            }
            callback(resp);

        } catch (const std::exception& e) {
            spdlog::error("PA verification error: {}", e.what());
            auto resp = HttpResponse::newHttpJsonResponse(json{
                {"status", "ERROR"},
                {"error", e.what()}
            });
            resp->setStatusCode(k500InternalServerError);
            callback(resp);
        }
    }

    /**
     * GET /api/pa/history
     */
    void getHistory(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        spdlog::info("GET /api/pa/history");

        // TODO: Implement history retrieval from repository
        json result = {
            {"items", json::array()},
            {"total", 0},
            {"page", 1},
            {"pageSize", 20}
        };

        auto resp = HttpResponse::newHttpJsonResponse(result);
        callback(resp);
    }

    /**
     * GET /api/pa/{id}
     */
    void getDetail(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback, const std::string& id) {
        spdlog::info("GET /api/pa/{}", id);

        // TODO: Implement detail retrieval from repository
        json result = {
            {"error", "Not implemented"},
            {"verificationId", id}
        };

        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k501NotImplemented);
        callback(resp);
    }

    /**
     * POST /api/pa/parse-dg1
     * Parse DG1 (MRZ) data
     */
    void parseDg1(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        spdlog::info("POST /api/pa/parse-dg1");

        // TODO: Implement DG1 MRZ parsing
        json result = {
            {"error", "Not implemented"}
        };

        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k501NotImplemented);
        callback(resp);
    }

    /**
     * POST /api/pa/parse-dg2
     * Parse DG2 (Face image) data
     */
    void parseDg2(const HttpRequestPtr& req, std::function<void(const HttpResponsePtr&)>&& callback) {
        spdlog::info("POST /api/pa/parse-dg2");

        // TODO: Implement DG2 face image parsing
        json result = {
            {"error", "Not implemented"}
        };

        auto resp = HttpResponse::newHttpJsonResponse(result);
        resp->setStatusCode(k501NotImplemented);
        callback(resp);
    }
};

} // namespace pa::infrastructure::controller
