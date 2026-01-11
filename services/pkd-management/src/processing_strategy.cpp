#include "processing_strategy.h"
#include "ldif_processor.h"
#include "common.h"
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

// processMasterListContentCore is now declared in common.h

void AutoProcessingStrategy::processLdifEntries(
    const std::string& uploadId,
    const std::vector<LdifEntry>& entries,
    PGconn* conn,
    LDAP* ld
) {
    spdlog::info("AUTO mode: Processing {} LDIF entries for upload {}", entries.size(), uploadId);

    ValidationStats stats;

    // Process all entries (save to DB, validate, upload to LDAP)
    auto counts = LdifProcessor::processEntries(uploadId, entries, conn, ld, stats);

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

    // Update ML count if any
    if (counts.mlCount > 0) {
        std::string mlUpdateQuery = "UPDATE uploaded_file SET ml_count = " + std::to_string(counts.mlCount) +
                                   " WHERE id = '" + uploadId + "'";
        PGresult* res = PQexec(conn, mlUpdateQuery.c_str());
        PQclear(res);
    }

    spdlog::info("AUTO mode: Completed - CSCA: {}, DSC: {}, DSC_NC: {}, CRL: {}, ML: {}, LDAP: {} certs, {} CRLs, {} MLs",
                counts.cscaCount, counts.dscCount, counts.dscNcCount, counts.crlCount, counts.mlCount,
                counts.ldapCertStoredCount, counts.ldapCrlStoredCount, counts.ldapMlStoredCount);
    spdlog::info("AUTO mode: Validation - {} valid, {} invalid, {} pending, {} CSCA not found, {} expired",
                stats.validCount, stats.invalidCount, stats.pendingCount, stats.cscaNotFoundCount, stats.expiredCount);
}

void AutoProcessingStrategy::processMasterListContent(
    const std::string& uploadId,
    const std::vector<uint8_t>& content,
    PGconn* conn,
    LDAP* ld
) {
    spdlog::info("AUTO mode: Processing Master List ({} bytes) for upload {}", content.size(), uploadId);

    // Use core Master List processing function from main.cpp
    processMasterListContentCore(uploadId, content, conn, ld);

    spdlog::info("AUTO mode: Master List processing completed");
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

    // Write to file
    std::ofstream outFile(tempFile);
    if (!outFile.is_open()) {
        throw std::runtime_error("Failed to create temp file: " + tempFile);
    }

    Json::StreamWriterBuilder writer;
    writer["indentation"] = "";  // Compact JSON
    std::unique_ptr<Json::StreamWriter> jsonWriter(writer.newStreamWriter());
    jsonWriter->write(jsonEntries, &outFile);
    outFile.close();

    spdlog::info("MANUAL mode: Saved {} LDIF entries to {}", entries.size(), tempFile);
}

std::vector<LdifEntry> ManualProcessingStrategy::loadLdifEntriesFromTempFile(const std::string& uploadId) {
    std::string tempFile = getTempFilePath(uploadId, "ldif");

    std::ifstream inFile(tempFile);
    if (!inFile.is_open()) {
        throw std::runtime_error("Failed to open temp file: " + tempFile);
    }

    Json::Value jsonEntries;
    Json::CharReaderBuilder reader;
    std::string errs;
    if (!Json::parseFromStream(reader, inFile, &jsonEntries, &errs)) {
        throw std::runtime_error("Failed to parse JSON from temp file: " + errs);
    }
    inFile.close();

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

    // Update upload status
    std::string updateQuery = "UPDATE uploaded_file SET status = 'PENDING', "
                             "total_entries = " + std::to_string(entries.size()) + " "
                             "WHERE id = '" + uploadId + "'";
    PGresult* res = PQexec(conn, updateQuery.c_str());
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

    // Update upload status
    std::string updateQuery = "UPDATE uploaded_file SET status = 'PENDING' WHERE id = '" + uploadId + "'";
    PGresult* res = PQexec(conn, updateQuery.c_str());
    PQclear(res);

    spdlog::info("MANUAL mode Stage 1: Completed, waiting for user to trigger validation");
}

void ManualProcessingStrategy::validateAndSaveToDb(
    const std::string& uploadId,
    PGconn* conn
) {
    spdlog::info("MANUAL mode Stage 2: Validating and saving to DB for upload {}", uploadId);

    // Check upload status and file format
    std::string checkQuery = "SELECT file_format, status FROM uploaded_file WHERE id = '" + uploadId + "'";
    PGresult* res = PQexec(conn, checkQuery.c_str());
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

    if (fileFormat == "LDIF") {
        // Load LDIF entries from temp file
        auto entries = loadLdifEntriesFromTempFile(uploadId);

        ValidationStats stats;

        // Process entries (save to DB with validation, but skip LDAP)
        auto counts = LdifProcessor::processEntries(uploadId, entries, conn, nullptr, stats);

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

        // Update ML count if any
        if (counts.mlCount > 0) {
            std::string mlUpdateQuery = "UPDATE uploaded_file SET ml_count = " + std::to_string(counts.mlCount) +
                                       " WHERE id = '" + uploadId + "'";
            PGresult* mlRes = PQexec(conn, mlUpdateQuery.c_str());
            PQclear(mlRes);
        }

        spdlog::info("MANUAL mode Stage 2: Processed {} LDIF entries - CSCA: {}, DSC: {}, DSC_NC: {}, CRL: {}, ML: {}",
                    entries.size(), counts.cscaCount, counts.dscCount, counts.dscNcCount, counts.crlCount, counts.mlCount);
        spdlog::info("MANUAL mode Stage 2: Validation - {} valid, {} invalid, {} pending",
                    stats.validCount, stats.invalidCount, stats.pendingCount);

    } else if (fileFormat == "ML") {
        // Load Master List from temp file
        auto content = loadMasterListFromTempFile(uploadId);

        // Process Master List (save to DB with validation, LDAP=nullptr for MANUAL mode)
        spdlog::info("MANUAL mode Stage 2: Processing Master List ({} bytes)", content.size());
        processMasterListToDb(uploadId, content, conn);

        // Update upload status to COMPLETED
        std::string mlUpdateQuery = "UPDATE uploaded_file SET status = 'COMPLETED', completed_timestamp = NOW() "
                                   " WHERE id = '" + uploadId + "'";
        PGresult* mlRes = PQexec(conn, mlUpdateQuery.c_str());
        PQclear(mlRes);

        spdlog::info("MANUAL mode Stage 2: Master List processing completed");

    } else {
        throw std::runtime_error("Unknown file format: " + fileFormat);
    }

    spdlog::info("MANUAL mode Stage 2: Completed, DB save and validation done");
}

void ManualProcessingStrategy::uploadToLdap(
    const std::string& uploadId,
    PGconn* conn,
    LDAP* ld
) {
    if (!ld) {
        throw std::runtime_error("LDAP connection not available");
    }

    spdlog::info("MANUAL mode Stage 3: Uploading to LDAP for upload {}", uploadId);

    // Upload certificates from DB to LDAP
    int uploadedCount = LdifProcessor::uploadToLdap(uploadId, conn, ld);

    spdlog::info("MANUAL mode Stage 3: Completed, uploaded {} entries to LDAP", uploadedCount);
}

void ManualProcessingStrategy::cleanupFailedUpload(
    const std::string& uploadId,
    PGconn* conn
) {
    spdlog::info("Cleaning up failed upload: {}", uploadId);

    // Delete certificates
    std::string deleteCerts = "DELETE FROM certificate WHERE upload_id = '" + uploadId + "'";
    PGresult* res = PQexec(conn, deleteCerts.c_str());
    int certsDeleted = 0;
    if (PQresultStatus(res) == PGRES_COMMAND_OK) {
        certsDeleted = atoi(PQcmdTuples(res));
    }
    PQclear(res);

    // Delete CRLs
    std::string deleteCrls = "DELETE FROM crl WHERE upload_id = '" + uploadId + "'";
    res = PQexec(conn, deleteCrls.c_str());
    int crlsDeleted = 0;
    if (PQresultStatus(res) == PGRES_COMMAND_OK) {
        crlsDeleted = atoi(PQcmdTuples(res));
    }
    PQclear(res);

    // Delete master lists
    std::string deleteMls = "DELETE FROM master_list WHERE upload_id = '" + uploadId + "'";
    res = PQexec(conn, deleteMls.c_str());
    int mlsDeleted = 0;
    if (PQresultStatus(res) == PGRES_COMMAND_OK) {
        mlsDeleted = atoi(PQcmdTuples(res));
    }
    PQclear(res);

    // Delete upload record
    std::string deleteUpload = "DELETE FROM uploaded_file WHERE id = '" + uploadId + "'";
    res = PQexec(conn, deleteUpload.c_str());
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

void ManualProcessingStrategy::processMasterListToDb(
    const std::string& uploadId,
    const std::vector<uint8_t>& content,
    PGconn* conn
) {
    spdlog::info("MANUAL mode Stage 2: Processing Master List to DB ({} bytes)", content.size());

    // Use core Master List processing function with LDAP=nullptr
    // This will parse CMS, extract certificates, validate them, and save to DB
    // but will NOT upload to LDAP
    processMasterListContentCore(uploadId, content, conn, nullptr);

    spdlog::info("MANUAL mode Stage 2: Master List saved to DB successfully");
}
