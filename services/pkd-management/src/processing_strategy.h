#pragma once

#include <string>
#include <vector>
#include <memory>
#include <libpq-fe.h>
#include <ldap.h>
#include "common.h"

/**
 * @brief Abstract base class for file processing strategies
 *
 * Defines the interface for different processing modes (AUTO, MANUAL)
 * following the Strategy design pattern.
 */
class ProcessingStrategy {
public:
    virtual ~ProcessingStrategy() = default;

    /**
     * @brief Process LDIF file according to the strategy
     * @param uploadId Upload record UUID
     * @param entries Parsed LDIF entries
     * @param conn PostgreSQL connection
     * @param ld LDAP connection (can be nullptr)
     */
    virtual void processLdifEntries(
        const std::string& uploadId,
        const std::vector<LdifEntry>& entries,
        PGconn* conn,
        LDAP* ld
    ) = 0;

    /**
     * @brief Process Master List file according to the strategy
     * @param uploadId Upload record UUID
     * @param content Raw file content
     * @param conn PostgreSQL connection
     * @param ld LDAP connection (can be nullptr)
     */
    virtual void processMasterListContent(
        const std::string& uploadId,
        const std::vector<uint8_t>& content,
        PGconn* conn,
        LDAP* ld
    ) = 0;

    /**
     * @brief Validate and save to database (MANUAL mode Stage 2)
     * @param uploadId Upload record UUID
     * @param conn PostgreSQL connection
     * @note Only implemented for ManualProcessingStrategy
     * @throws std::runtime_error if called on AutoProcessingStrategy
     */
    virtual void validateAndSaveToDb(
        const std::string& uploadId,
        PGconn* conn
    ) = 0;
};

/**
 * @brief AUTO mode processing strategy
 *
 * Processes files in one go:
 * 1. Parse
 * 2. Save to DB with validation
 * 3. Upload to LDAP (if connection available)
 */
class AutoProcessingStrategy : public ProcessingStrategy {
public:
    void processLdifEntries(
        const std::string& uploadId,
        const std::vector<LdifEntry>& entries,
        PGconn* conn,
        LDAP* ld
    ) override;

    void processMasterListContent(
        const std::string& uploadId,
        const std::vector<uint8_t>& content,
        PGconn* conn,
        LDAP* ld
    ) override;

    void validateAndSaveToDb(
        const std::string& uploadId,
        PGconn* conn
    ) override;
};

/**
 * @brief MANUAL mode processing strategy
 *
 * Processes files in 2 stages:
 * Stage 1 (parse):     Parse and save to temp file
 * Stage 2 (validate):  Load from temp, save to DB + LDAP with validation
 */
class ManualProcessingStrategy : public ProcessingStrategy {
public:
    // Stage 1: Parse only
    void processLdifEntries(
        const std::string& uploadId,
        const std::vector<LdifEntry>& entries,
        PGconn* conn,
        LDAP* ld
    ) override;

    void processMasterListContent(
        const std::string& uploadId,
        const std::vector<uint8_t>& content,
        PGconn* conn,
        LDAP* ld
    ) override;

    // Stage 2: Validate and save to DB + LDAP
    void validateAndSaveToDb(
        const std::string& uploadId,
        PGconn* conn
    );

    // Cleanup failed upload
    static void cleanupFailedUpload(
        const std::string& uploadId,
        PGconn* conn
    );

private:
    std::string getTempFilePath(const std::string& uploadId, const std::string& type) const;
    void saveLdifEntriesToTempFile(const std::string& uploadId, const std::vector<LdifEntry>& entries);
    std::vector<LdifEntry> loadLdifEntriesFromTempFile(const std::string& uploadId);
    void saveMasterListToTempFile(const std::string& uploadId, const std::vector<uint8_t>& content);
    std::vector<uint8_t> loadMasterListFromTempFile(const std::string& uploadId);
    void processMasterListToDbAndLdap(const std::string& uploadId, const std::vector<uint8_t>& content, PGconn* conn, LDAP* ld);
};

/**
 * @brief Factory for creating processing strategies
 *
 * Factory pattern implementation to create appropriate strategy
 * based on processing mode.
 */
class ProcessingStrategyFactory {
public:
    /**
     * @brief Create processing strategy based on mode
     * @param mode "AUTO" or "MANUAL"
     * @return Unique pointer to strategy instance
     * @throws std::runtime_error if mode is unknown
     */
    static std::unique_ptr<ProcessingStrategy> create(const std::string& mode);
};
