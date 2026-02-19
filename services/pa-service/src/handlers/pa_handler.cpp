/** @file pa_handler.cpp
 *  @brief PaHandler implementation
 */

#include "pa_handler.h"

#include <spdlog/spdlog.h>
#include <json/json.h>

#include <openssl/bio.h>
#include <openssl/evp.h>

#include <algorithm>
#include <regex>
#include <sstream>

#include "../services/pa_verification_service.h"
#include "../repositories/data_group_repository.h"
#include "../common/country_code_utils.h"
#include <sod_parser.h>
#include <dg_parser.h>

namespace handlers {

// --- Static utility functions ---

std::vector<uint8_t> PaHandler::base64Decode(const std::string& encoded) {
    BIO* bio = BIO_new_mem_buf(encoded.data(), static_cast<int>(encoded.size()));
    if (!bio) return {};
    BIO* b64 = BIO_new(BIO_f_base64());
    if (!b64) { BIO_free(bio); return {}; }
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    bio = BIO_push(b64, bio);

    std::vector<uint8_t> decoded(encoded.size());
    int len = BIO_read(bio, decoded.data(), static_cast<int>(decoded.size()));
    BIO_free_all(bio);

    if (len > 0) {
        decoded.resize(len);
    } else {
        decoded.clear();
    }
    return decoded;
}

// --- Constructor ---

PaHandler::PaHandler(
    services::PaVerificationService* paVerificationService,
    repositories::DataGroupRepository* dataGroupRepository,
    icao::SodParser* sodParserService,
    icao::DgParser* dataGroupParserService)
    : paVerificationService_(paVerificationService),
      dataGroupRepository_(dataGroupRepository),
      sodParserService_(sodParserService),
      dataGroupParserService_(dataGroupParserService) {

    if (!paVerificationService_ || !dataGroupRepository_ ||
        !sodParserService_ || !dataGroupParserService_) {
        throw std::invalid_argument("PaHandler: service/repository pointers cannot be nullptr");
    }

    spdlog::info("[PaHandler] Initialized with Service Pattern");
}

// --- Route Registration ---

void PaHandler::registerRoutes(drogon::HttpAppFramework& app) {
    // POST /api/pa/verify
    app.registerHandler(
        "/api/pa/verify",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleVerify(req, std::move(callback));
        },
        {drogon::Post}
    );

    // GET /api/pa/history
    app.registerHandler(
        "/api/pa/history",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleHistory(req, std::move(callback));
        },
        {drogon::Get}
    );

    // GET /api/pa/{id}
    app.registerHandler(
        "/api/pa/{id}",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
            handleDetail(req, std::move(callback), id);
        },
        {drogon::Get}
    );

    // GET /api/pa/statistics
    app.registerHandler(
        "/api/pa/statistics",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleStatistics(req, std::move(callback));
        },
        {drogon::Get}
    );

    // POST /api/pa/parse-dg1
    app.registerHandler(
        "/api/pa/parse-dg1",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleParseDg1(req, std::move(callback));
        },
        {drogon::Post}
    );

    // POST /api/pa/parse-mrz-text
    app.registerHandler(
        "/api/pa/parse-mrz-text",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleParseMrzText(req, std::move(callback));
        },
        {drogon::Post}
    );

    // POST /api/pa/parse-dg2
    app.registerHandler(
        "/api/pa/parse-dg2",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleParseDg2(req, std::move(callback));
        },
        {drogon::Post}
    );

    // POST /api/pa/parse-sod
    app.registerHandler(
        "/api/pa/parse-sod",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
            handleParseSod(req, std::move(callback));
        },
        {drogon::Post}
    );

    // GET /api/pa/{id}/datagroups
    app.registerHandler(
        "/api/pa/{id}/datagroups",
        [this](const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback,
               const std::string& id) {
            handleDataGroups(req, std::move(callback), id);
        },
        {drogon::Get}
    );

    spdlog::info("[PaHandler] Routes registered");
}

// --- Handler Implementations ---

void PaHandler::handleVerify(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("POST /api/pa/verify - Passive Authentication verification (Service Layer)");

    // Log request details for debugging
    auto contentType = req->getHeader("Content-Type");
    auto contentLength = req->getHeader("Content-Length");
    auto bodyLength = req->body().length();
    spdlog::info("Request - Content-Type: {}, Content-Length: {}, Body Length: {}",
                contentType.empty() ? "(empty)" : contentType,
                contentLength.empty() ? "(empty)" : contentLength,
                bodyLength);

    try {
        // Parse request body
        auto jsonBody = req->getJsonObject();
        if (!jsonBody) {
            spdlog::error("Failed to parse JSON body");
            Json::Value error;
            error["success"] = false;
            error["error"] = "Invalid JSON body";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        // Get SOD data (Base64 encoded)
        std::string sodBase64 = (*jsonBody)["sod"].asString();
        if (sodBase64.empty()) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "SOD data is required";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        // Decode SOD
        std::vector<uint8_t> sodBytes = base64Decode(sodBase64);
        if (sodBytes.empty()) {
            Json::Value error;
            error["success"] = false;
            error["error"] = "Failed to decode SOD (invalid Base64)";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
            resp->setStatusCode(drogon::k400BadRequest);
            callback(resp);
            return;
        }

        // Parse Data Groups (convert to map with string keys)
        std::map<std::string, std::vector<uint8_t>> dataGroups;
        if (jsonBody->isMember("dataGroups")) {
            if ((*jsonBody)["dataGroups"].isArray()) {
                // Array format: [{number: "DG1", data: "base64..."}, ...]
                for (const auto& dg : (*jsonBody)["dataGroups"]) {
                    std::string dgNumStr = dg["number"].asString();
                    std::string dgData = dg["data"].asString();
                    // Extract number from "DG1" -> "1"
                    std::string dgKey = dgNumStr.length() > 2 ? dgNumStr.substr(2) : dgNumStr;
                    dataGroups[dgKey] = base64Decode(dgData);
                }
            } else if ((*jsonBody)["dataGroups"].isObject()) {
                // Object format: {"DG1": "base64...", "DG2": "base64..."} OR {"1": "base64...", "2": "base64..."}
                for (const auto& key : (*jsonBody)["dataGroups"].getMemberNames()) {
                    std::string dgKey;
                    // Support both "DG1" format and "1" format
                    if (key.length() > 2 && (key.substr(0, 2) == "DG" || key.substr(0, 2) == "dg")) {
                        dgKey = key.substr(2);  // "DG1" -> "1"
                    } else {
                        dgKey = key;  // "1" -> "1"
                    }
                    std::string dgData = (*jsonBody)["dataGroups"][key].asString();
                    dataGroups[dgKey] = base64Decode(dgData);
                }
            }
        }

        // Get optional fields
        std::string countryCode = (*jsonBody).get("issuingCountry", "").asString();
        // Normalize alpha-3 country codes (e.g., KOR->KR) for LDAP compatibility
        if (!countryCode.empty()) {
            std::string normalized = common::normalizeCountryCodeToAlpha2(countryCode);
            if (normalized != countryCode) {
                spdlog::info("Country code normalized: {} -> {}", countryCode, normalized);
            }
            countryCode = normalized;
        }
        std::string documentNumber = (*jsonBody).get("documentNumber", "").asString();

        // Extract documentNumber and countryCode from DG1 MRZ if not provided
        if ((documentNumber.empty() || countryCode.empty()) && dataGroups.count("1") > 0) {
            const auto& dg1Data = dataGroups["1"];
            // Simple extraction: find MRZ in DG1 and extract document number + country
            // This is a simplified version - full parsing is in icao::DgParser
            size_t pos = 0;
            while (pos + 3 < dg1Data.size()) {
                if (dg1Data[pos] == 0x5F && dg1Data[pos + 1] == 0x1F) {
                    // Found MRZ tag 5F1F
                    pos += 2;
                    size_t mrzLen = dg1Data[pos++];
                    if (mrzLen > 127) {
                        size_t numBytes = mrzLen & 0x7F;
                        mrzLen = 0;
                        for (size_t i = 0; i < numBytes && pos < dg1Data.size(); i++) {
                            mrzLen = (mrzLen << 8) | dg1Data[pos++];
                        }
                    }
                    if (pos + mrzLen <= dg1Data.size() && mrzLen >= 88) {
                        std::string mrzData(dg1Data.begin() + pos, dg1Data.begin() + pos + mrzLen);
                        // TD3 format (2 lines x 44 chars)
                        if (mrzData.length() >= 88) {
                            // Document number: line2[0:9]
                            if (documentNumber.empty()) {
                                std::string docNum = mrzData.substr(44, 9);
                                docNum.erase(std::remove(docNum.begin(), docNum.end(), '<'), docNum.end());
                                documentNumber = docNum;
                                spdlog::debug("Extracted document number from DG1: {}", documentNumber);
                            }
                            // Issuing country: line1[2:5] (3-letter alpha-3)
                            if (countryCode.empty()) {
                                std::string mrzCountry = mrzData.substr(2, 3);
                                mrzCountry.erase(std::remove(mrzCountry.begin(), mrzCountry.end(), '<'), mrzCountry.end());
                                if (!mrzCountry.empty()) {
                                    countryCode = common::normalizeCountryCodeToAlpha2(mrzCountry);
                                    spdlog::info("Extracted country code from DG1 MRZ: {} -> {}", mrzCountry, countryCode);
                                }
                            }
                        }
                    }
                    break;
                }
                pos++;
            }
        }

        spdlog::info("PA verification request: country={}, documentNumber={}, dataGroups={}",
                    countryCode.empty() ? "(unknown)" : countryCode,
                    documentNumber.empty() ? "(unknown)" : documentNumber,
                    dataGroups.size());

        // Extract client metadata for audit
        std::string clientIp = req->getPeerAddr().toIp();
        std::string userAgent = req->getHeader("User-Agent");
        std::string requestedBy = (*jsonBody).get("requestedBy", "").asString();

        // Call service layer - this replaces ~400 lines of complex logic
        Json::Value result = paVerificationService_->verifyPassiveAuthentication(
            sodBytes,
            dataGroups,
            documentNumber,
            countryCode,
            clientIp,
            userAgent,
            requestedBy
        );

        // Return response
        auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
        if (!result["success"].asBool()) {
            resp->setStatusCode(drogon::k400BadRequest);
        }
        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("Error in POST /api/pa/verify: {}", e.what());
        Json::Value error;
        error["success"] = false;
        error["error"] = "Internal Server Error";
        error["message"] = e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
    }
}

void PaHandler::handleHistory(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("GET /api/pa/history");

    try {
        // Parse query parameters
        int page = 0;
        int size = 20;
        std::string statusFilter;
        std::string countryFilter;

        if (auto p = req->getParameter("page"); !p.empty()) {
            page = std::stoi(p);
        }
        if (auto s = req->getParameter("size"); !s.empty()) {
            size = std::stoi(s);
        }
        if (auto st = req->getParameter("status"); !st.empty()) {
            statusFilter = st;
        }
        if (auto c = req->getParameter("issuingCountry"); !c.empty()) {
            countryFilter = c;
        }

        // Calculate limit and offset
        int limit = size;
        int offset = page * size;

        // Call service layer (100% parameterized SQL, secure)
        Json::Value result = paVerificationService_->getVerificationHistory(
            limit,
            offset,
            statusFilter,
            countryFilter
        );

        auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("Error in GET /api/pa/history: {}", e.what());
        Json::Value error;
        error["success"] = false;
        error["error"] = e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
    }
}

void PaHandler::handleDetail(
    const drogon::HttpRequestPtr& /* req */,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    spdlog::info("GET /api/pa/{}", id);

    try {
        // Call service layer
        Json::Value result = paVerificationService_->getVerificationById(id);

        if (result.isNull() || result.empty()) {
            // Not found
            Json::Value notFound;
            notFound["status"] = "NOT_FOUND";
            notFound["message"] = "PA verification record not found";
            auto resp = drogon::HttpResponse::newHttpJsonResponse(notFound);
            resp->setStatusCode(drogon::k404NotFound);
            callback(resp);
            return;
        }

        auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("Error in GET /api/pa/{}: {}", id, e.what());
        Json::Value error;
        error["success"] = false;
        error["error"] = e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
    }
}

void PaHandler::handleStatistics(
    const drogon::HttpRequestPtr& /* req */,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("GET /api/pa/statistics");

    try {
        // Call service layer
        Json::Value result = paVerificationService_->getStatistics();

        auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
        callback(resp);

    } catch (const std::exception& e) {
        spdlog::error("Error in GET /api/pa/statistics: {}", e.what());
        Json::Value error;
        error["success"] = false;
        error["error"] = e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
    }
}

void PaHandler::handleParseDg1(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("POST /api/pa/parse-dg1");

    // Parse request body
    auto jsonBody = req->getJsonObject();
    std::string dg1Base64;

    if (jsonBody) {
        dg1Base64 = (*jsonBody).get("dg1Base64", "").asString();
        if (dg1Base64.empty()) {
            dg1Base64 = (*jsonBody).get("dg1", "").asString();
        }
        if (dg1Base64.empty()) {
            dg1Base64 = (*jsonBody).get("data", "").asString();
        }
    }

    if (dg1Base64.empty()) {
        Json::Value error;
        error["error"] = "DG1 data is required (dg1Base64, dg1, or data field)";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    // Base64 decode
    std::vector<uint8_t> dg1Bytes = base64Decode(dg1Base64);
    if (dg1Bytes.empty()) {
        Json::Value error;
        error["error"] = "Invalid Base64 encoding";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    // Use icao::DgParser to parse DG1
    Json::Value result = dataGroupParserService_->parseDg1(dg1Bytes);

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

void PaHandler::handleParseMrzText(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("POST /api/pa/parse-mrz-text");

    // Parse request body
    auto jsonBody = req->getJsonObject();
    if (!jsonBody || (*jsonBody)["mrzText"].asString().empty()) {
        Json::Value error;
        error["error"] = "MRZ text is required";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    std::string mrzText = (*jsonBody)["mrzText"].asString();

    // Use icao::DgParser to parse MRZ text
    Json::Value result = dataGroupParserService_->parseMrzText(mrzText);

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

void PaHandler::handleParseDg2(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("POST /api/pa/parse-dg2");

    // Parse request body
    auto jsonBody = req->getJsonObject();
    std::string dg2Base64;

    if (jsonBody) {
        dg2Base64 = (*jsonBody).get("dg2Base64", "").asString();
        if (dg2Base64.empty()) {
            dg2Base64 = (*jsonBody).get("dg2", "").asString();
        }
        if (dg2Base64.empty()) {
            dg2Base64 = (*jsonBody).get("data", "").asString();
        }
    }

    if (dg2Base64.empty()) {
        Json::Value error;
        error["error"] = "DG2 data is required (dg2Base64, dg2, or data field)";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    // Base64 decode
    std::vector<uint8_t> dg2Bytes = base64Decode(dg2Base64);
    if (dg2Bytes.empty()) {
        Json::Value error;
        error["error"] = "Invalid Base64 encoding";
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    // Use icao::DgParser to parse DG2
    Json::Value result = dataGroupParserService_->parseDg2(dg2Bytes);

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

void PaHandler::handleParseSod(
    const drogon::HttpRequestPtr& req,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {

    spdlog::info("POST /api/pa/parse-sod");

    // Parse request body
    auto jsonBody = req->getJsonObject();
    std::string sodBase64;

    if (jsonBody) {
        sodBase64 = (*jsonBody).get("sodBase64", "").asString();
        if (sodBase64.empty()) {
            sodBase64 = (*jsonBody).get("sod", "").asString();
        }
        if (sodBase64.empty()) {
            sodBase64 = (*jsonBody).get("data", "").asString();
        }
    }

    if (sodBase64.empty()) {
        Json::Value error;
        error["error"] = "SOD data is required (sodBase64, sod, or data field)";
        error["success"] = false;
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    // Base64 decode
    std::vector<uint8_t> sodBytes = base64Decode(sodBase64);
    if (sodBytes.empty()) {
        Json::Value error;
        error["error"] = "Invalid Base64 encoding";
        error["success"] = false;
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k400BadRequest);
        callback(resp);
        return;
    }

    // Use icao::SodParser to parse SOD
    Json::Value result = sodParserService_->parseSodForApi(sodBytes);

    auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
    callback(resp);
}

void PaHandler::handleDataGroups(
    const drogon::HttpRequestPtr& /* req */,
    std::function<void(const drogon::HttpResponsePtr&)>&& callback,
    const std::string& id) {

    spdlog::info("GET /api/pa/{}/datagroups", id);

    try {
        // Use DataGroupRepository to fetch data groups (returns JSON array)
        Json::Value dataGroups = dataGroupRepository_->findByVerificationId(id);

        Json::Value result;
        result["verificationId"] = id;
        result["hasDg1"] = false;
        result["hasDg2"] = false;

        spdlog::debug("Found {} data groups for verification {}", dataGroups.size(), id);

        // Process each data group
        for (const auto& dg : dataGroups) {
            int dgNumber = dg["dgNumber"].asInt();

            // Convert hex string back to binary for parsing
            std::string dgBinaryHex = dg["dgBinary"].asString();
            std::vector<uint8_t> dgBytes;

            // Remove \x prefix if present
            size_t startPos = 0;
            if (dgBinaryHex.length() >= 2 && dgBinaryHex[0] == '\\' && dgBinaryHex[1] == 'x') {
                startPos = 2;
            }

            // Convert hex string to bytes
            for (size_t i = startPos; i < dgBinaryHex.length(); i += 2) {
                if (i + 1 < dgBinaryHex.length()) {
                    std::string byteStr = dgBinaryHex.substr(i, 2);
                    uint8_t byte = static_cast<uint8_t>(std::stoi(byteStr, nullptr, 16));
                    dgBytes.push_back(byte);
                }
            }

            if (dgNumber == 1) {
                result["hasDg1"] = true;
                spdlog::debug("Parsing DG1 ({} bytes)", dgBytes.size());

                // Use icao::DgParser to parse DG1
                Json::Value dg1Result = dataGroupParserService_->parseDg1(dgBytes);
                if (dg1Result["success"].asBool()) {
                    result["dg1"] = dg1Result;
                    spdlog::debug("DG1 parsed successfully");
                } else {
                    spdlog::warn("Failed to parse DG1: {}", dg1Result["error"].asString());
                }
            } else if (dgNumber == 2) {
                result["hasDg2"] = true;
                spdlog::debug("Parsing DG2 ({} bytes)", dgBytes.size());

                // Use icao::DgParser to parse DG2
                Json::Value dg2Result = dataGroupParserService_->parseDg2(dgBytes);
                if (dg2Result["success"].asBool()) {
                    result["dg2"] = dg2Result;
                    spdlog::debug("DG2 parsed successfully");
                } else {
                    spdlog::warn("Failed to parse DG2: {}", dg2Result["error"].asString());
                }
            }
        }

        auto resp = drogon::HttpResponse::newHttpJsonResponse(result);
        callback(resp);
    } catch (const std::exception& e) {
        spdlog::error("Error in /api/pa/{}/datagroups: {}", id, e.what());
        Json::Value error;
        error["success"] = false;
        error["error"] = e.what();
        auto resp = drogon::HttpResponse::newHttpJsonResponse(error);
        resp->setStatusCode(drogon::k500InternalServerError);
        callback(resp);
    }
}

} // namespace handlers
