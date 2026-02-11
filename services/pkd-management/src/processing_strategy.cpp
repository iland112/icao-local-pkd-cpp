#include "processing_strategy.h"
#include "ldif_processor.h"
#include "common.h"
#include "common/masterlist_processor.h"
#include "common/progress_manager.h"  // Phase 4.4: Enhanced progress tracking
#include "repositories/upload_repository.h"  // Phase 6.1: Repository Pattern
#include "repositories/validation_repository.h"  // Phase 6.4: ValidationRepository for updateStatistics
#include "domain/models/validation_statistics.h"
#include "i_query_executor.h"  // Phase 6.3: Database-agnostic query execution
#include <drogon/HttpTypes.h>
#include <json/json.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <filesystem>
#include <stdexcept>
#include <memory>
#include <ldap.h>

// Phase 4.4: Using declarations for enhanced progress tracking
using common::ValidationStatistics;
using common::CertificateMetadata;
using common::IcaoComplianceStatus;
using common::ProcessingStage;

// Phase 6.1: Global repository declarations (defined in main.cpp)
// Forward declare the class first
namespace repositories {
    class UploadRepository;
    class ValidationRepository;
}
extern std::shared_ptr<repositories::UploadRepository> uploadRepository;
extern std::shared_ptr<repositories::ValidationRepository> validationRepository;

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
extern void updateUploadStatistics(const std::string& uploadId,
                                   const std::string& status, int cscaCount,
                                   int dscCount, int dscNcCount, int crlCount,
                                   int totalEntries, int processedEntries,
                                   const std::string& errorMessage);
extern void sendCompletionProgress(const std::string& uploadId, int totalItems, const std::string& message);

// processMasterListContentCore is now declared in common.h

void AutoProcessingStrategy::processLdifEntries(
    const std::string& uploadId,
    const std::vector<LdifEntry>& entries,
    LDAP* ld
) {
    spdlog::info("AUTO mode: Processing {} LDIF entries for upload {}", entries.size(), uploadId);

    ValidationStats stats;  // Existing validation statistics (legacy)
    common::ValidationStatistics enhancedStats{};  // Phase 4.4: Enhanced statistics with metadata tracking

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
    auto counts = LdifProcessor::processEntries(uploadId, entries, ld, stats, enhancedStats, &totalCounts);

    // Update database statistics
    int totalItems = counts.cscaCount + counts.dscCount + counts.dscNcCount + counts.crlCount + counts.mlCount;
    updateUploadStatistics(uploadId, "COMPLETED",
                          counts.cscaCount, counts.dscCount, counts.dscNcCount, counts.crlCount,
                          entries.size(), entries.size(), "");

    // Update validation statistics via ValidationRepository
    if (::validationRepository) {
        domain::models::ValidationStatistics valStats;
        valStats.validCount = stats.validCount;
        valStats.invalidCount = stats.invalidCount;
        valStats.pendingCount = stats.pendingCount;
        valStats.errorCount = stats.errorCount;
        valStats.trustChainValidCount = stats.trustChainValidCount;
        valStats.trustChainInvalidCount = stats.trustChainInvalidCount;
        valStats.cscaNotFoundCount = stats.cscaNotFoundCount;
        valStats.expiredCount = stats.expiredCount;
        valStats.revokedCount = stats.revokedCount;
        ::validationRepository->updateStatistics(uploadId, valStats);
    }

    // Update ML and MLSC counts via repository (v2.6.2 fix)
    if ((counts.mlCount > 0 || counts.mlscCount > 0) && ::uploadRepository) {
        ::uploadRepository->updateStatistics(uploadId,
            counts.cscaCount, counts.dscCount, counts.dscNcCount, counts.crlCount,
            counts.mlscCount, counts.mlCount);
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
    LDAP* ld
) {
    spdlog::info("AUTO mode: Processing Master List ({} bytes) for upload {}", content.size(), uploadId);

    // v2.1.1: Use new masterlist_processor for correct MLSC/CSCA/LC extraction
    // TODO Phase 6.1: processMasterListFile needs conn parameter removed (masterlist_processor.h/cpp)
    // For now, pass nullptr as conn since we're transitioning to Repository Pattern
    MasterListStats stats;
    bool success = processMasterListFile(ld, uploadId, content, stats);

    if (!success) {
        throw std::runtime_error("Failed to process Master List file");
    }

    spdlog::info("AUTO mode: Master List processing completed - {} MLSC, {} CSCA/LC extracted ({} new, {} duplicate)",
                stats.mlCount, stats.cscaExtractedCount, stats.cscaNewCount, stats.cscaDuplicateCount);

    // Update uploaded_file table with final statistics
    // csca_count = newly inserted CSCA count (not total in file)
    // total_entries = total CSCA certificates in ML file
    // processed_entries = newly inserted CSCA count
    updateUploadStatistics(uploadId, "COMPLETED",
                          stats.cscaNewCount,          // csca_count (newly inserted only)
                          0,                            // dsc_count (Master Lists don't contain DSC)
                          0,                            // dsc_nc_count
                          0,                            // crl_count
                          0,                            // ml_count (unused in this call)
                          0,                            // processed_entries (unused - set via updateProgress below)
                          "");                          // error_message

    // Update all statistics via repository: csca_count, mlsc_count, ml_count, total_entries, processed_entries
    if (::uploadRepository) {
        ::uploadRepository->updateStatistics(uploadId,
            stats.cscaNewCount, 0, 0, 0,
            stats.mlscCount, stats.mlCount);
        ::uploadRepository->updateProgress(uploadId,
            stats.cscaExtractedCount, stats.cscaNewCount);
    }

    spdlog::info("AUTO mode: Statistics updated - status=COMPLETED, csca_count={}, mlsc_count={}, total_entries={}, processed_entries={}",
                stats.cscaNewCount, stats.mlscCount, stats.cscaExtractedCount, stats.cscaNewCount);
}

void AutoProcessingStrategy::validateAndSaveToDb(
    const std::string& uploadId
) {
    // AUTO mode doesn't use Stage 2 validation - all processing happens in processLdifEntries/processMasterListContent
    throw std::runtime_error("validateAndSaveToDb() is not supported in AUTO mode");
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
    LDAP* ld
) {
    spdlog::info("MANUAL mode Stage 1: Parsing {} LDIF entries for upload {}", entries.size(), uploadId);

    // Save to temp file
    saveLdifEntriesToTempFile(uploadId, entries);

    // Update upload status using repository
    // TODO Phase 6.1: Need uploadRepository->updateStatusAndTotalEntries() method
    // For now, using updateStatus() and noting that total_entries update is missing
    if (uploadRepository) {
        uploadRepository->updateStatus(uploadId, "PENDING", "");
        spdlog::info("Updated upload status to PENDING (total_entries update pending)");
    } else {
        spdlog::error("uploadRepository is null");
    }

    spdlog::info("MANUAL mode Stage 1: Completed, waiting for user to trigger validation");
}

void ManualProcessingStrategy::processMasterListContent(
    const std::string& uploadId,
    const std::vector<uint8_t>& content,
    LDAP* ld
) {
    spdlog::info("MANUAL mode Stage 1: Parsing Master List ({} bytes) for upload {}", content.size(), uploadId);

    // Save to temp file
    saveMasterListToTempFile(uploadId, content);

    // Update upload status using repository
    if (uploadRepository) {
        uploadRepository->updateStatus(uploadId, "PENDING", "");
        spdlog::info("Updated upload status to PENDING");
    } else {
        spdlog::error("uploadRepository is null");
    }

    spdlog::info("MANUAL mode Stage 1: Completed, waiting for user to trigger validation");
}

void ManualProcessingStrategy::validateAndSaveToDb(
    const std::string& uploadId
) {
    spdlog::info("MANUAL mode Stage 2: Validating and saving to DB + LDAP for upload {}", uploadId);

    // Check upload status and file format using repository
    auto uploadOpt = uploadRepository->findById(uploadId);
    if (!uploadOpt.has_value()) {
        throw std::runtime_error("Upload not found: " + uploadId);
    }

    const auto& upload = uploadOpt.value();
    std::string fileFormat = upload.fileFormat;
    std::string status = upload.status;

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
        common::ValidationStatistics enhancedStats{};  // Phase 4.4: Enhanced statistics with metadata tracking

        // Process entries (save to BOTH DB and LDAP simultaneously)
        auto counts = LdifProcessor::processEntries(uploadId, entries, ld, stats, enhancedStats, &totalCounts);

        // Update database statistics
        updateUploadStatistics(uploadId, "COMPLETED",
                              counts.cscaCount, counts.dscCount, counts.dscNcCount, counts.crlCount,
                              entries.size(), entries.size(), "");

        // Update validation statistics via ValidationRepository
        if (::validationRepository) {
            domain::models::ValidationStatistics valStats;
            valStats.validCount = stats.validCount;
            valStats.invalidCount = stats.invalidCount;
            valStats.pendingCount = stats.pendingCount;
            valStats.errorCount = stats.errorCount;
            valStats.trustChainValidCount = stats.trustChainValidCount;
            valStats.trustChainInvalidCount = stats.trustChainInvalidCount;
            valStats.cscaNotFoundCount = stats.cscaNotFoundCount;
            valStats.expiredCount = stats.expiredCount;
            valStats.revokedCount = stats.revokedCount;
            ::validationRepository->updateStatistics(uploadId, valStats);
        }

        // Update ML and MLSC counts via repository (v2.6.2 fix - Oracle compatible)
        if ((counts.mlCount > 0 || counts.mlscCount > 0) && ::uploadRepository) {
            ::uploadRepository->updateStatistics(uploadId,
                counts.cscaCount, counts.dscCount, counts.dscNcCount, counts.crlCount,
                counts.mlscCount, counts.mlCount);
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
        processMasterListToDbAndLdap(uploadId, content, ld);

        // Update upload status to COMPLETED via repository (Oracle compatible)
        if (::uploadRepository) {
            ::uploadRepository->updateStatus(uploadId, "COMPLETED", "");
        }

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
    const std::string& uploadId
) {
    spdlog::info("Cleaning up failed upload: {}", uploadId);

    // Use QueryExecutor for cascading deletes (Oracle + PostgreSQL compatible)
    // Note: FK ON DELETE CASCADE on uploaded_file would handle child tables,
    // but we delete explicitly for logging counts
    extern std::unique_ptr<common::IQueryExecutor> queryExecutor;

    int certsDeleted = 0;
    int crlsDeleted = 0;
    int mlsDeleted = 0;

    try {
        if (queryExecutor) {
            // Delete certificates
            certsDeleted = queryExecutor->executeCommand(
                "DELETE FROM certificate WHERE upload_id = $1", {uploadId});

            // Delete CRLs
            crlsDeleted = queryExecutor->executeCommand(
                "DELETE FROM crl WHERE upload_id = $1", {uploadId});

            // Delete master lists
            mlsDeleted = queryExecutor->executeCommand(
                "DELETE FROM master_list WHERE upload_id = $1", {uploadId});

            // Delete upload record
            queryExecutor->executeCommand(
                "DELETE FROM uploaded_file WHERE id = $1", {uploadId});
        } else {
            spdlog::error("queryExecutor is null, cannot cleanup upload");
        }
    } catch (const std::exception& e) {
        spdlog::error("Failed to cleanup upload {}: {}", uploadId, e.what());
    }

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
    LDAP* ld
) {
    spdlog::info("MANUAL mode Stage 2: Processing Master List to DB + LDAP ({} bytes)", content.size());

    // v2.1.1: Use new masterlist_processor for correct MLSC/CSCA/LC extraction
    MasterListStats stats;
    bool success = processMasterListFile(ld, uploadId, content, stats);

    if (!success) {
        throw std::runtime_error("Failed to process Master List file");
    }

    spdlog::info("MANUAL mode Stage 2: Master List saved to DB and LDAP - {} MLSC, {} CSCA/LC extracted",
                stats.mlCount, stats.cscaExtractedCount);

    // Update uploaded_file table with final statistics
    updateUploadStatistics(uploadId, "COMPLETED",
                          stats.cscaExtractedCount,  // csca_count
                          0,                          // dsc_count
                          0,                          // dsc_nc_count
                          0,                          // crl_count
                          stats.mlCount,              // ml_count
                          stats.cscaExtractedCount,   // processed_entries
                          "");                        // error_message

    // Update MLSC and ML counts directly via repository (v2.6.2 fix)
    if (::uploadRepository) {
        ::uploadRepository->updateStatistics(uploadId,
            stats.cscaExtractedCount, 0, 0, 0,
            stats.mlscCount, stats.mlCount);
    }

    spdlog::info("MANUAL mode Stage 2: Statistics updated - mlsc_count={}, csca_count={}",
                stats.mlscCount, stats.cscaExtractedCount);
}
