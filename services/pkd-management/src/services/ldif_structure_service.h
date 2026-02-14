/**
 * @file ldif_structure_service.h
 * @brief LDIF Structure Service - Business Logic Layer
 *
 * Handles LDIF file structure visualization business logic.
 * Provides validation, error handling, and response formatting.
 *
 * @author SmartCore Inc.
 * @date 2026-01-31
 */

#pragma once

#include <string>
#include <json/json.h>
#include "../repositories/ldif_structure_repository.h"

namespace services {

/**
 * @brief LDIF Structure Service
 *
 * Responsibilities:
 * - Validate input parameters (maxEntries range)
 * - Call LdifStructureRepository
 * - Format JSON responses
 * - Handle exceptions and return user-friendly error messages
 */
class LdifStructureService {
public:
    /**
     * @brief Constructor with Dependency Injection
     * @param ldifStructureRepo LDIF Structure Repository (non-owning pointer)
     */
    explicit LdifStructureService(repositories::LdifStructureRepository* ldifStructureRepo);

    ~LdifStructureService() = default;

    /**
     * @brief Get LDIF file structure
     *
     * @param uploadId Upload UUID
     * @param maxEntries Maximum number of entries to return (1-10000, default: 100)
     * @return JSON response with structure data
     *
     * Response format (success):
     * {
     *   "success": true,
     *   "data": {
     *     "entries": [
     *       {
     *         "dn": "cn=...,o=csca,c=FR,...",
     *         "objectClass": "pkdCertificate",
     *         "lineNumber": 15,
     *         "attributes": [
     *           {
     *             "name": "cn",
     *             "value": "CSCA-FRANCE",
     *             "isBinary": false
     *           },
     *           {
     *             "name": "userCertificate;binary",
     *             "value": "[Binary Certificate: 1234 bytes]",
     *             "isBinary": true,
     *             "binarySize": 1234
     *           }
     *         ]
     *       }
     *     ],
     *     "totalEntries": 5017,
     *     "displayedEntries": 100,
     *     "totalAttributes": 15051,
     *     "objectClassCounts": {
     *       "pkdCertificate": 4991,
     *       "pkdMasterList": 26
     *     },
     *     "truncated": true
     *   }
     * }
     *
     * Response format (error):
     * {
     *   "success": false,
     *   "error": "Error message"
     * }
     */
    Json::Value getLdifStructure(const std::string& uploadId, int maxEntries = 100);

private:
    repositories::LdifStructureRepository* ldifStructureRepository_;

    /**
     * @brief Validate maxEntries parameter
     * @param maxEntries Requested max entries
     * @return Validated max entries (clamped to valid range)
     */
    int validateMaxEntries(int maxEntries);

    /**
     * @brief Create success response
     * @param data LDIF structure data
     * @return JSON response
     */
    Json::Value createSuccessResponse(const repositories::LdifStructureData& data);

    /**
     * @brief Create error response
     * @param errorMessage Error message
     * @return JSON response
     */
    Json::Value createErrorResponse(const std::string& errorMessage);
};

}  // namespace services
