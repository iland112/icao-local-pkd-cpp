#include "processing_strategy.h"
#include "ldif_processor.h"
#include "common.h"
#include "common/masterlist_processor.h"
#include <drogon/HttpTypes.h>
#include <json/json.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <libpq-fe.h>
#include <ldap.h>

// This file will be implemented in phases
// For now, we provide the factory implementation

std::unique_ptr<ProcessingStrategy> ProcessingStrategyFactory::create(const std::string& mode) {
    if (mode == "AUTO") {
        return std::make_unique<AutoProcessingStrategy>();
    } else if (mode == "MANUAL") {
        return std::make_unique<ManualProcessingStrategy>();
    } else {
        throw std::runtime_error("Unknown processing mode: " + mode);
    }
}

// ============================================================================
// AutoProcessingStrategy - Process in one go
// ============================================================================

// Forward declarations for functions still in main.cpp
extern void updateUploadStatistics(PGconn* conn, const std::string& uploadId,
                                   const std::string& status, int cscaCount,
                                   int dscCount, int dscNcCount, int crlCount,
                                   int totalEntries, int processedEntries,
                                   const std::string& errorMessage);
extern void updateValidationStatistics(PGconn* conn, const std::string& uploadId,
                                      int validCount, int invalidCount, int pendingCount,
                                      int errorCount, int trustChainValidCount,
                                      int trustChainInvalidCount, int cscaNotFoundCount,
                                      int expiredCount, int revokedCount);
extern void sendCompletionProgress(const std::string& uploadId, int totalItems, const std::string& message);

// processMasterListContentCore is now declared in common.h

void AutoProcessingStrategy::processLdifEntries(
    const std::string& uploadId,
    const std::vector<LdifEntry>& entries,
    PGconn* conn,
    LDAP* ld
) {
    spdlog::info("AUTO mode: Processing {} LDIF entries for upload {}", entries.size(), uploadId);

    ValidationStats stats;

    // v1.5.9: Pre-scan entries to calculate total counts for "X/Total" progress display
    LdifProcessor::TotalCounts totalCounts;
    for (const auto& entry : entries) {
        // Count certificates (userCertificate or cACertificate)
        if (entry.hasAttribute("userCertificate;binary") || entry.hasAttribute("cACertificate;binary")) {
            totalCounts.totalCerts++;
        }
        // Count CRLs
        if (entry.hasAttribute("certificateRevocationList;binary")) {
            totalCounts.totalCrl++;
        }
        // Count Master Lists
        if (entry.hasAttribute("pkdMasterListContent;binary") || entry.hasAttribute("pkdMasterListContent")) {
            totalCounts.totalMl++;
        }
    }
    spdlog::info("AUTO mode: Pre-scan complete - {} certs, {} CRLs, {} MLs",
                totalCounts.totalCerts, totalCounts.totalCrl, totalCounts.totalMl);

    // Process all entries (save to DB, validate, upload to LDAP) with total counts for progress display
    auto counts = LdifProcessor::processEntries(uploadId, entries, conn, ld, stats, &totalCounts);

    // Update database statistics
    int totalItems = counts.cscaCount + counts.dscCount + counts.dscNcCount + counts.crlCount + counts.mlCount;
    updateUploadStatistics(conn, uploadId, "COMPLETED",
                          counts.cscaCount, counts.dscCount, counts.dscNcCount, counts.crlCount,
                          entries.size(), entries.size(), "");

    // Update validation statistics
    updateValidationStatistics(conn, uploadId,
                              stats.validCount, stats.invalidCount, stats.pendingCount,
                              stats.errorCount, stats.trustChainValidCount,
                              stats.trustChainInvalidCount, stats.cscaNotFoundCount,
                              stats.expiredCount, stats.revokedCount);

    // Update ML count if any (parameterized query)
    if (counts.mlCount > 0) {
        const char* mlUpdateQuery = "UPDATE uploaded_file SET ml_count = $1 WHERE id = $2";
        std::string mlCountStr = std::to_string(counts.mlCount);
        const char* paramValues[2] = {mlCountStr.c_str(), uploadId.c_str()};
        PGresult* res = PQexecParams(conn, mlUpdateQuery, 2, nullptr, paramValues,
                                     nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            spdlog::error("Failed to update ML count: {}", PQerrorMessage(conn));
        }
        PQclear(res);
    }

    // Update MLSC count if any (v2.1.1)
    if (counts.mlscCount > 0) {
        const char* mlscUpdateQuery = "UPDATE uploaded_file SET mlsc_count = $1 WHERE id = $2";
        std::string mlscCountStr = std::to_string(counts.mlscCount);
        const char* paramValues[2] = {mlscCountStr.c_str(), uploadId.c_str()};
        PGresult* res = PQexecParams(conn, mlscUpdateQuery, 2, nullptr, paramValues,
                                     nullptr, nullptr, 0);
        if (PQresultStatus(res) != PGRES_COMMAND_OK) {
            spdlog::error("Failed to update MLSC count: {}", PQerrorMessage(conn));
        }
        PQclear(res);
    }

    spdlog::info("AUTO mode: Completed - CSCA: {}, DSC: {}, DSC_NC: {}, CRL: {}, ML: {}, MLSC: {}, LDAP: {} certs, {} CRLs, {} MLs",
                counts.cscaCount, counts.dscCount, counts.dscNcCount, counts.crlCount, counts.mlCount, counts.mlscCount,
                counts.ldapCertStoredCount, counts.ldapCrlStoredCount, counts.ldapMlStoredCount);
    spdlog::info("AUTO mode: Validation - {} valid, {} invalid, {} pending, {} CSCA not found, {} expired",
                stats.validCount, stats.invalidCount, stats.pendingCount, stats.cscaNotFoundCount, stats.expiredCount);

    // Send completion progress to frontend
    std::string completionMsg = "처리 완료: ";
    std::vector<std::string> parts;
    if (counts.cscaCount > 0) parts.push_back("CSCA " + std::to_string(counts.cscaCount) + "개");
    if (counts.dscCount > 0) parts.push_back("DSC " + std::to_string(counts.dscCount) + "개");
    if (counts.dscNcCount > 0) parts.push_back("DSC_NC " + std::to_string(counts.dscNcCount) + "개");
    if (counts.crlCount > 0) parts.push_back("CRL " + std::to_string(counts.crlCount) + "개");
    if (counts.mlCount > 0) parts.push_back("ML " + std::to_string(counts.mlCount) + "개");

    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) completionMsg += ", ";
        completionMsg += parts[i];
    }

    completionMsg += " (검증: " + std::to_string(stats.validCount) + " 성공, " +
                    std::to_string(stats.invalidCount) + " 실패, " +
                    std::to_string(stats.pendingCount) + " 보류)";

    sendCompletionProgress(uploadId, totalItems, completionMsg);
}

void AutoProcessingStrategy::processMasterListContent(
    const std::string& uploadId,
    const std::vector<uint8_t>& content,
    PGconn* conn,
    LDAP* ld
) {
    spdlog::info("AUTO mode: Processing Master List ({} bytes) for upload {}", content.size(), uploadId);

    // v2.1.1: Use new masterlist_processor for correct MLSC/CSCA/LC extraction
    MasterListStats stats;
    bool success = processMasterListFile(conn, ld, uploadId, content, stats);

    if (!success) {
        throw std::runtime_error("Failed to process Master List file");
    }

    spdlog::info("AUTO mode: Master List processing completed - {} MLSC, {} CSCA/LC extracted ({} new, {} duplicate)",
                stats.mlCount, stats.cscaExtractedCount, stats.cscaNewCount, stats.cscaDuplicateCount);

    // Update uploaded_file table with final statistics
    // Note: ml_count represents Master List files from LDIF entries, not the .ml file itself
    // MLSC and CSCA certificates are counted separately
    updateUploadStatistics(conn, uploadId, "COMPLETED",
                          stats.cscaExtractedCount,  // csca_count (includes both MLSC and CSCA/LC)
                          0,                          // dsc_count (Master Lists don't contain DSC)
                          0,                          // dsc_nc_count
                          0,                          // crl_count
                          stats.mlCount,              // ml_count (stored Master List entries)
                          stats.cscaExtractedCount,   // processed_entries
                          "");                        // error_message

    // Update MLSC-specific count (v2.1.1)
    const char* mlscQuery = "UPDATE uploaded_file SET mlsc_count = $1 WHERE id = $2";
    std::string mlscCountStr = std::to_string(stats.mlCount);
    const char* mlscParams[2] = {mlscCountStr.c_str(), uploadId.c_str()};
    PGresult* mlscRes = PQexecParams(conn, mlscQuery, 2, nullptr, mlscParams,
                                     nullptr, nullptr, 0);
    if (PQresultStatus(mlscRes) != PGRES_COMMAND_OK) {
        spdlog::error("Failed to update mlsc_count: {}", PQerrorMessage(conn));
    }
    PQclear(mlscRes);

    spdlog::info("AUTO mode: Statistics updated - status=COMPLETED, mlsc_count={}, csca_count={}",
                stats.mlCount, stats.cscaExtractedCount);
}

// ============================================================================
// ManualProcessingStrategy - Stage 1: Parse and save to temp
// ============================================================================

std::string ManualProcessingStrategy::getTempFilePath(const std::string& uploadId, const std::string& type) const {
    return "/app/temp/" + uploadId + "_" + type + ".json";
}

void ManualProcessingStrategy::saveLdifEntriesToTempFile(
    const std::string& uploadId,
    const std::vector<LdifEntry>& entries
) {
    std::string tempDir = "/app/temp";
    std::string tempFile = getTempFilePath(uploadId, "ldif");

    // Create temp directory if not exists
    std::filesystem::create_directories(tempDir);

    // Pre-calculate total counts for each type (for progress display in Stage 2)
    int totalCerts = 0, totalCrl = 0, totalMl = 0;
    try {
        for (const auto& entry : entries) {
            if (entry.hasAttribute("userCertificate;binary") || entry.hasAttribute("cACertificate;binary")) {
                totalCerts++;
            }
            if (entry.hasAttribute("certificateRevocationList;binary")) {
                totalCrl++;
            }
            if (entry.hasAttribute("pkdMasterListContent;binary") || entry.hasAttribute("pkdMasterListContent")) {
                totalMl++;
            }
        }
        spdlog::info("MANUAL mode Stage 1: Counted {} certs, {} CRLs, {} MLs", totalCerts, totalCrl, totalMl);
    } catch (const std::exception& e) {
        spdlog::error("Error counting entry types: {}", e.what());
        throw;
    }

    // Create root JSON with metadata
    Json::Value root;
    root["metadata"]["totalEntries"] = static_cast<int>(entries.size());
    root["metadata"]["totalCerts"] = totalCerts;
    root["metadata"]["totalCrl"] = totalCrl;
    root["metadata"]["totalMl"] = totalMl;

    // Serialize LDIF entries to JSON
    Json::Value jsonEntries(Json::arrayValue);
    for (const auto& entry : entries) {
        Json::Value jsonEntry;
        jsonEntry["dn"] = entry.dn;

        // Serialize attributes
        Json::Value jsonAttrs(Json::objectValue);
        for (const auto& attr : entry.attributes) {
            Json::Value jsonValues(Json::arrayValue);
            for (const auto& val : attr.second) {
                jsonValues.append(val);
            }
            jsonAttrs[attr.first] = jsonValues;
        }
        jsonEntry["attributes"] = jsonAttrs;
        jsonEntries.append(jsonEntry);
    }
    root["entries"] = jsonEntries;

    // Write to file
    std::ofstream outFile(tempFile);
    if (!outFile.is_open()) {
        throw std::runtime_error("Failed to create temp file: " + tempFile);
    }

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";  // Compact JSON
    std::unique_ptr<Json::StreamWriter> jsonWriter(writer.newStreamWriter());
    jsonWriter->write(root, &outFile);
    outFile.close();

    spdlog::info("MANUAL mode: Saved {} LDIF entries to {} (Certs: {}, CRL: {}, ML: {})",
                entries.size(), tempFile, totalCerts, totalCrl, totalMl);
}

std::vector<LdifEntry> ManualProcessingStrategy::loadLdifEntriesFromTempFile(const std::string& uploadId) {
    std::string tempFile = getTempFilePath(uploadId, "ldif");

    std::ifstream inFile(tempFile);
    if (!inFile.is_open()) {
        throw std::runtime_error("Failed to open temp file: " + tempFile);
    }

    Json::Value root;
    Json::CharReaderBuilder reader;
    std::string errs;
    if (!Json::parseFromStream(reader, inFile, &root, &errs)) {
        throw std::runtime_error("Failed to parse JSON from temp file: " + errs);
    }
    inFile.close();

    // Check if this is new format (with metadata) or old format (array only)
    const Json::Value& jsonEntries = root.isMember("entries") ? root["entries"] : root;

    // Deserialize JSON to LdifEntry vector
    std::vector<LdifEntry> entries;
    for (const auto& jsonEntry : jsonEntries) {
        LdifEntry entry;
        entry.dn = jsonEntry["dn"].asString();

        const Json::Value& jsonAttrs = jsonEntry["attributes"];
        for (const auto& attrName : jsonAttrs.getMemberNames()) {
            std::vector<std::string> values;
            for (const auto& val : jsonAttrs[attrName]) {
                values.push_back(val.asString());
            }
            entry.attributes[attrName] = values;
        }
        entries.push_back(entry);
    }

    spdlog::info("MANUAL mode: Loaded {} LDIF entries from {}", entries.size(), tempFile);
    return entries;
}

void ManualProcessingStrategy::saveMasterListToTempFile(
    const std::string& uploadId,
    const std::vector<uint8_t>& content
) {
    std::string tempDir = "/app/temp";
    std::string tempFile = getTempFilePath(uploadId, "ml");

    // Create temp directory if not exists
    std::filesystem::create_directories(tempDir);

    // Save binary content to file
    std::ofstream outFile(tempFile, std::ios::binary);
    if (!outFile.is_open()) {
        throw std::runtime_error("Failed to create temp file: " + tempFile);
    }

    outFile.write(reinterpret_cast<const char*>(content.data()), content.size());
    outFile.close();

    spdlog::info("MANUAL mode: Saved Master List ({} bytes) to {}", content.size(), tempFile);
}

std::vector<uint8_t> ManualProcessingStrategy::loadMasterListFromTempFile(const std::string& uploadId) {
    std::string tempFile = getTempFilePath(uploadId, "ml");

    std::ifstream inFile(tempFile, std::ios::binary | std::ios::ate);
    if (!inFile.is_open()) {
        throw std::runtime_error("Failed to open temp file: " + tempFile);
    }

    std::streamsize size = inFile.tellg();
    inFile.seekg(0, std::ios::beg);

    std::vector<uint8_t> content(size);
    if (!inFile.read(reinterpret_cast<char*>(content.data()), size)) {
        throw std::runtime_error("Failed to read temp file: " + tempFile);
    }
    inFile.close();

    spdlog::info("MANUAL mode: Loaded Master List ({} bytes) from {}", content.size(), tempFile);
    return content;
}

void ManualProcessingStrategy::processLdifEntries(
    const std::string& uploadId,
    const std::vector<LdifEntry>& entries,
    PGconn* conn,
    LDAP* ld
) {
    spdlog::info("MANUAL mode Stage 1: Parsing {} LDIF entries for upload {}", entries.size(), uploadId);

    // Save to temp file
    saveLdifEntriesToTempFile(uploadId, entries);

    // Update upload status (parameterized query - Phase 2)
    const char* updateQuery = "UPDATE uploaded_file SET status = 'PENDING', total_entries = $1 WHERE id = $2";
    std::string totalEntriesStr = std::to_string(entries.size());
    const char* paramValues[2] = {totalEntriesStr.c_str(), uploadId.c_str()};
    PGresult* res = PQexecParams(conn, updateQuery, 2, nullptr, paramValues,
                                 nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("Failed to update upload status: {}", PQerrorMessage(conn));
    }
    PQclear(res);

    spdlog::info("MANUAL mode Stage 1: Completed, waiting for user to trigger validation");
}

void ManualProcessingStrategy::processMasterListContent(
    const std::string& uploadId,
    const std::vector<uint8_t>& content,
    PGconn* conn,
    LDAP* ld
) {
    spdlog::info("MANUAL mode Stage 1: Parsing Master List ({} bytes) for upload {}", content.size(), uploadId);

    // Save to temp file
    saveMasterListToTempFile(uploadId, content);

    // Update upload status (parameterized query)
    const char* updateQuery = "UPDATE uploaded_file SET status = 'PENDING' WHERE id = $1";
    const char* paramValues[1] = {uploadId.c_str()};
    PGresult* res = PQexecParams(conn, updateQuery, 1, nullptr, paramValues,
                                 nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("Failed to update upload status: {}", PQerrorMessage(conn));
    }
    PQclear(res);

    spdlog::info("MANUAL mode Stage 1: Completed, waiting for user to trigger validation");
}

void ManualProcessingStrategy::validateAndSaveToDb(
    const std::string& uploadId,
    PGconn* conn
) {
    spdlog::info("MANUAL mode Stage 2: Validating and saving to DB + LDAP for upload {}", uploadId);

    // Check upload status and file format (parameterized query - Phase 2)
    const char* checkQuery = "SELECT file_format, status FROM uploaded_file WHERE id = $1";
    const char* paramValues[1] = {uploadId.c_str()};
    PGresult* res = PQexecParams(conn, checkQuery, 1, nullptr, paramValues,
                                 nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_TUPLES_OK || PQntuples(res) == 0) {
        PQclear(res);
        throw std::runtime_error("Upload not found: " + uploadId);
    }

    std::string fileFormat = PQgetvalue(res, 0, 0);
    std::string status = PQgetvalue(res, 0, 1);
    PQclear(res);

    // Verify Stage 1 is completed (status should be PENDING after parsing)
    if (status != "PENDING") {
        throw std::runtime_error("Stage 1 parsing not completed. Current status: " + status);
    }

    // Connect to LDAP for write operations
    LDAP* ld = getLdapWriteConnection();
    if (!ld) {
        throw std::runtime_error("LDAP write connection failed");
    }

    if (fileFormat == "LDIF") {
        // Load LDIF entries from temp file
        auto entries = loadLdifEntriesFromTempFile(uploadId);

        // Load metadata for progress display (X/Total format)
        LdifProcessor::TotalCounts totalCounts;
        try {
            std::string tempFile = getTempFilePath(uploadId, "ldif");
            std::ifstream metaFile(tempFile);
            if (metaFile.is_open()) {
                Json::Value root;
                Json::CharReaderBuilder reader;
                std::string errs;
                if (Json::parseFromStream(reader, metaFile, &root, &errs) && root.isMember("metadata")) {
                    totalCounts.totalCerts = root["metadata"].get("totalCerts", 0).asInt();
                    totalCounts.totalCrl = root["metadata"].get("totalCrl", 0).asInt();
                    totalCounts.totalMl = root["metadata"].get("totalMl", 0).asInt();
                    spdlog::info("MANUAL mode Stage 2: Loaded metadata - Certs: {}, CRL: {}, ML: {}",
                                totalCounts.totalCerts, totalCounts.totalCrl, totalCounts.totalMl);
                }
                metaFile.close();
            }
        } catch (const std::exception& e) {
            spdlog::warn("Failed to load metadata for progress display: {}. Using simple format.", e.what());
        }

        ValidationStats stats;

        // Process entries (save to BOTH DB and LDAP simultaneously)
        auto counts = LdifProcessor::processEntries(uploadId, entries, conn, ld, stats, &totalCounts);

        // Update database statistics
        updateUploadStatistics(conn, uploadId, "COMPLETED",
                              counts.cscaCount, counts.dscCount, counts.dscNcCount, counts.crlCount,
                              entries.size(), entries.size(), "");

        // Update validation statistics
        updateValidationStatistics(conn, uploadId,
                                  stats.validCount, stats.invalidCount, stats.pendingCount,
                                  stats.errorCount, stats.trustChainValidCount,
                                  stats.trustChainInvalidCount, stats.cscaNotFoundCount,
                                  stats.expiredCount, stats.revokedCount);

        // Update ML count if any (parameterized query)
        if (counts.mlCount > 0) {
            const char* mlUpdateQuery = "UPDATE uploaded_file SET ml_count = $1 WHERE id = $2";
            std::string mlCountStr = std::to_string(counts.mlCount);
            const char* paramValues[2] = {mlCountStr.c_str(), uploadId.c_str()};
            PGresult* mlRes = PQexecParams(conn, mlUpdateQuery, 2, nullptr, paramValues,
                                           nullptr, nullptr, 0);
            if (PQresultStatus(mlRes) != PGRES_COMMAND_OK) {
                spdlog::error("Failed to update ML count: {}", PQerrorMessage(conn));
            }
            PQclear(mlRes);
        }

        // Update MLSC count if any (v2.1.1)
        if (counts.mlscCount > 0) {
            const char* mlscUpdateQuery = "UPDATE uploaded_file SET mlsc_count = $1 WHERE id = $2";
            std::string mlscCountStr = std::to_string(counts.mlscCount);
            const char* paramValues[2] = {mlscCountStr.c_str(), uploadId.c_str()};
            PGresult* mlscRes = PQexecParams(conn, mlscUpdateQuery, 2, nullptr, paramValues,
                                             nullptr, nullptr, 0);
            if (PQresultStatus(mlscRes) != PGRES_COMMAND_OK) {
                spdlog::error("Failed to update MLSC count: {}", PQerrorMessage(conn));
            }
            PQclear(mlscRes);
        }

        spdlog::info("MANUAL mode Stage 2: Processed {} LDIF entries - CSCA: {}, DSC: {}, DSC_NC: {}, CRL: {}, ML: {}, MLSC: {}",
                    entries.size(), counts.cscaCount, counts.dscCount, counts.dscNcCount, counts.crlCount, counts.mlCount, counts.mlscCount);
        spdlog::info("MANUAL mode Stage 2: Validation - {} valid, {} invalid, {} pending",
                    stats.validCount, stats.invalidCount, stats.pendingCount);

        // Send completion progress to frontend
        std::string completionMsg = "처리 완료: ";
        std::vector<std::string> completionParts;
        if (counts.cscaCount > 0) completionParts.push_back("CSCA " + std::to_string(counts.cscaCount) + "개");
        if (counts.dscCount > 0) completionParts.push_back("DSC " + std::to_string(counts.dscCount) + "개");
        if (counts.dscNcCount > 0) completionParts.push_back("DSC_NC " + std::to_string(counts.dscNcCount) + "개");
        if (counts.crlCount > 0) completionParts.push_back("CRL " + std::to_string(counts.crlCount) + "개");
        if (counts.mlCount > 0) completionParts.push_back("ML " + std::to_string(counts.mlCount) + "개");

        for (size_t i = 0; i < completionParts.size(); ++i) {
            if (i > 0) completionMsg += ", ";
            completionMsg += completionParts[i];
        }

        int totalItems = counts.cscaCount + counts.dscCount + counts.dscNcCount + counts.crlCount + counts.mlCount;
        // Call helper function from main.cpp to send completion progress
        sendCompletionProgress(uploadId, totalItems, completionMsg);

    } else if (fileFormat == "ML") {
        // Load Master List from temp file
        auto content = loadMasterListFromTempFile(uploadId);

        // Process Master List (save to BOTH DB and LDAP simultaneously)
        spdlog::info("MANUAL mode Stage 2: Processing Master List ({} bytes)", content.size());
        processMasterListToDbAndLdap(uploadId, content, conn, ld);

        // Update upload status to COMPLETED
        std::string mlUpdateQuery = "UPDATE uploaded_file SET status = 'COMPLETED', completed_timestamp = NOW() "
                                   " WHERE id = '" + uploadId + "'";
        PGresult* mlRes = PQexec(conn, mlUpdateQuery.c_str());
        PQclear(mlRes);

        spdlog::info("MANUAL mode Stage 2: Master List processing completed");

    } else {
        ldap_unbind_ext_s(ld, nullptr, nullptr);
        throw std::runtime_error("Unknown file format: " + fileFormat);
    }

    // Cleanup LDAP connection
    ldap_unbind_ext_s(ld, nullptr, nullptr);

    spdlog::info("MANUAL mode Stage 2: Completed, DB and LDAP save done");
}


void ManualProcessingStrategy::cleanupFailedUpload(
    const std::string& uploadId,
    PGconn* conn
) {
    spdlog::info("Cleaning up failed upload: {}", uploadId);

    // Prepare parameter (used for all DELETE queries)
    const char* paramValues[1] = {uploadId.c_str()};

    // Delete certificates (parameterized query)
    const char* deleteCerts = "DELETE FROM certificate WHERE upload_id = $1";
    PGresult* res = PQexecParams(conn, deleteCerts, 1, nullptr, paramValues,
                                 nullptr, nullptr, 0);
    int certsDeleted = 0;
    if (PQresultStatus(res) == PGRES_COMMAND_OK) {
        certsDeleted = atoi(PQcmdTuples(res));
    } else {
        spdlog::error("Failed to delete certificates: {}", PQerrorMessage(conn));
    }
    PQclear(res);

    // Delete CRLs (parameterized query)
    const char* deleteCrls = "DELETE FROM crl WHERE upload_id = $1";
    res = PQexecParams(conn, deleteCrls, 1, nullptr, paramValues,
                       nullptr, nullptr, 0);
    int crlsDeleted = 0;
    if (PQresultStatus(res) == PGRES_COMMAND_OK) {
        crlsDeleted = atoi(PQcmdTuples(res));
    } else {
        spdlog::error("Failed to delete CRLs: {}", PQerrorMessage(conn));
    }
    PQclear(res);

    // Delete master lists (parameterized query)
    const char* deleteMls = "DELETE FROM master_list WHERE upload_id = $1";
    res = PQexecParams(conn, deleteMls, 1, nullptr, paramValues,
                       nullptr, nullptr, 0);
    int mlsDeleted = 0;
    if (PQresultStatus(res) == PGRES_COMMAND_OK) {
        mlsDeleted = atoi(PQcmdTuples(res));
    } else {
        spdlog::error("Failed to delete master lists: {}", PQerrorMessage(conn));
    }
    PQclear(res);

    // Delete upload record (parameterized query)
    const char* deleteUpload = "DELETE FROM uploaded_file WHERE id = $1";
    res = PQexecParams(conn, deleteUpload, 1, nullptr, paramValues,
                       nullptr, nullptr, 0);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        spdlog::error("Failed to delete upload record: {}", PQerrorMessage(conn));
    }
    PQclear(res);

    // Delete temp files
    ManualProcessingStrategy strategy;
    std::string ldifTemp = strategy.getTempFilePath(uploadId, "ldif");
    std::string mlTemp = strategy.getTempFilePath(uploadId, "ml");

    try {
        if (std::filesystem::exists(ldifTemp)) {
            std::filesystem::remove(ldifTemp);
            spdlog::info("Deleted temp file: {}", ldifTemp);
        }
        if (std::filesystem::exists(mlTemp)) {
            std::filesystem::remove(mlTemp);
            spdlog::info("Deleted temp file: {}", mlTemp);
        }
    } catch (const std::exception& e) {
        spdlog::warn("Failed to delete temp files: {}", e.what());
    }

    spdlog::info("Cleanup completed: {} certs, {} CRLs, {} MLs deleted",
                 certsDeleted, crlsDeleted, mlsDeleted);
}

// ============================================================================
// ManualProcessingStrategy - Helper: Process Master List to DB (Stage 2)
// ============================================================================

void ManualProcessingStrategy::processMasterListToDbAndLdap(
    const std::string& uploadId,
    const std::vector<uint8_t>& content,
    PGconn* conn,
    LDAP* ld
) {
    spdlog::info("MANUAL mode Stage 2: Processing Master List to DB + LDAP ({} bytes)", content.size());

    // v2.1.1: Use new masterlist_processor for correct MLSC/CSCA/LC extraction
    MasterListStats stats;
    bool success = processMasterListFile(conn, ld, uploadId, content, stats);

    if (!success) {
        throw std::runtime_error("Failed to process Master List file");
    }

    spdlog::info("MANUAL mode Stage 2: Master List saved to DB and LDAP - {} MLSC, {} CSCA/LC extracted",
                stats.mlCount, stats.cscaExtractedCount);

    // Update uploaded_file table with final statistics
    updateUploadStatistics(conn, uploadId, "COMPLETED",
                          stats.cscaExtractedCount,  // csca_count
                          0,                          // dsc_count
                          0,                          // dsc_nc_count
                          0,                          // crl_count
                          stats.mlCount,              // ml_count
                          stats.cscaExtractedCount,   // processed_entries
                          "");                        // error_message

    // Update MLSC-specific count (v2.1.1)
    const char* mlscQuery = "UPDATE uploaded_file SET mlsc_count = $1 WHERE id = $2";
    std::string mlscCountStr = std::to_string(stats.mlCount);
    const char* mlscParams[2] = {mlscCountStr.c_str(), uploadId.c_str()};
    PGresult* mlscRes = PQexecParams(conn, mlscQuery, 2, nullptr, mlscParams,
                                     nullptr, nullptr, 0);
    if (PQresultStatus(mlscRes) != PGRES_COMMAND_OK) {
        spdlog::error("Failed to update mlsc_count: {}", PQerrorMessage(conn));
    }
    PQclear(mlscRes);

    spdlog::info("MANUAL mode Stage 2: Statistics updated - mlsc_count={}, csca_count={}",
                stats.mlCount, stats.cscaExtractedCount);
}
