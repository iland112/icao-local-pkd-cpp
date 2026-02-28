#pragma once

#include <drogon/HttpAppFramework.h>
#include <json/json.h>
#include <string>
#include <vector>
#include <map>

// Forward declarations
namespace common {
    class IQueryExecutor;
}
namespace services {
    class PaVerificationService;
}
namespace repositories {
    class DataGroupRepository;
}
namespace icao {
    class SodParser;
    class DgParser;
}

namespace handlers {

/**
 * @brief Passive Authentication endpoints handler
 *
 * Provides all PA-related API endpoints:
 * - POST /api/pa/verify - Full PA verification
 * - GET /api/pa/history - Verification history (paginated)
 * - GET /api/pa/{id} - Verification detail by ID
 * - GET /api/pa/statistics - Verification statistics
 * - POST /api/pa/parse-sod - Parse SOD (Security Object Document)
 * - POST /api/pa/parse-dg1 - Parse DG1 (MRZ data)
 * - POST /api/pa/parse-dg2 - Parse DG2 (Face image extraction)
 * - POST /api/pa/parse-mrz-text - Parse raw MRZ text
 * - GET /api/pa/{id}/datagroups - Data groups for a verification
 *
 * Uses Service Pattern for business logic delegation.
 */
class PaHandler {
public:
    /**
     * @brief Construct PaHandler
     *
     * @param paVerificationService PA verification service (non-owning pointer)
     * @param dataGroupRepository Data group repository (non-owning pointer)
     * @param sodParserService SOD parser service (non-owning pointer)
     * @param dataGroupParserService DG parser service (non-owning pointer)
     */
    PaHandler(
        services::PaVerificationService* paVerificationService,
        repositories::DataGroupRepository* dataGroupRepository,
        icao::SodParser* sodParserService,
        icao::DgParser* dataGroupParserService,
        common::IQueryExecutor* queryExecutor = nullptr);

    /**
     * @brief Register PA routes
     *
     * Registers all PA endpoints with Drogon application.
     *
     * @param app Drogon application instance
     */
    void registerRoutes(drogon::HttpAppFramework& app);

private:
    services::PaVerificationService* paVerificationService_;
    repositories::DataGroupRepository* dataGroupRepository_;
    icao::SodParser* sodParserService_;
    icao::DgParser* dataGroupParserService_;
    common::IQueryExecutor* queryExecutor_;

    /**
     * @brief POST /api/pa/verify
     *
     * Perform full ICAO 9303 Passive Authentication verification.
     *
     * Request body:
     * {
     *   "sod": "base64-encoded SOD",
     *   "dataGroups": {"DG1": "base64...", "DG2": "base64..."},
     *   "issuingCountry": "KR",
     *   "documentNumber": "M12345678"
     * }
     */
    void handleVerify(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/pa/history
     *
     * Get paginated PA verification history.
     *
     * Query params:
     *   - page: Page number (default: 0)
     *   - size: Page size (default: 20)
     *   - status: Filter by status (optional)
     *   - issuingCountry: Filter by country (optional)
     */
    void handleHistory(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/pa/{id}
     *
     * Get PA verification detail by ID.
     */
    void handleDetail(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& id);

    /**
     * @brief GET /api/pa/statistics
     *
     * Get PA verification statistics.
     */
    void handleStatistics(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief POST /api/pa/parse-sod
     *
     * Parse SOD (Security Object Document) and return metadata.
     *
     * Request body:
     * {
     *   "sodBase64": "base64..." (or "sod" or "data")
     * }
     */
    void handleParseSod(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief POST /api/pa/parse-dg1
     *
     * Parse DG1 data group (MRZ extraction).
     *
     * Request body:
     * {
     *   "dg1Base64": "base64..." (or "dg1" or "data")
     * }
     */
    void handleParseDg1(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief POST /api/pa/parse-dg2
     *
     * Parse DG2 data group (face image extraction).
     *
     * Request body:
     * {
     *   "dg2Base64": "base64..." (or "dg2" or "data")
     * }
     */
    void handleParseDg2(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief POST /api/pa/parse-mrz-text
     *
     * Parse raw MRZ text string.
     *
     * Request body:
     * {
     *   "mrzText": "P<UTOERIKSSON<<ANNA..."
     * }
     */
    void handleParseMrzText(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback);

    /**
     * @brief GET /api/pa/{id}/datagroups
     *
     * Get data groups for a PA verification with full DG1/DG2 parsing.
     */
    void handleDataGroups(
        const drogon::HttpRequestPtr& req,
        std::function<void(const drogon::HttpResponsePtr&)>&& callback,
        const std::string& id);

    /// @name Utility Functions
    /// @{

    /**
     * @brief Decode Base64 string to binary bytes
     * @param encoded Base64 encoded string
     * @return Decoded bytes or empty vector on error
     */
    static std::vector<uint8_t> base64Decode(const std::string& encoded);

    /// @}
};

} // namespace handlers
