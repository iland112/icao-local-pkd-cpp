#pragma once

#include "passiveauthentication/domain/repository/PassportDataRepository.hpp"
#include "passiveauthentication/domain/model/PassportData.hpp"
#include <drogon/orm/DbClient.h>
#include <spdlog/spdlog.h>
#include <memory>

namespace pa::infrastructure::repository {

/**
 * PostgreSQL repository implementation for PassportData.
 */
class PostgresPassportDataRepository : public domain::repository::PassportDataRepository {
private:
    std::shared_ptr<drogon::orm::DbClient> dbClient_;

public:
    explicit PostgresPassportDataRepository(std::shared_ptr<drogon::orm::DbClient> dbClient)
        : dbClient_(std::move(dbClient)) {}

    void save(const domain::model::PassportData& passportData) override {
        spdlog::debug("Saving PassportData: {}", passportData.getId().getId());

        try {
            auto binder = *dbClient_ << R"(
                INSERT INTO passport_data (
                    id, sod_encoded, verification_status, issuing_country,
                    document_number, started_at, completed_at, processing_duration_ms,
                    raw_request_data
                ) VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)
                ON CONFLICT (id) DO UPDATE SET
                    verification_status = EXCLUDED.verification_status,
                    completed_at = EXCLUDED.completed_at,
                    processing_duration_ms = EXCLUDED.processing_duration_ms
            )";

            // Convert time points to strings
            auto startedAt = std::chrono::system_clock::to_time_t(passportData.getStartedAt());
            std::tm tm = *std::localtime(&startedAt);
            char startBuf[32];
            std::strftime(startBuf, sizeof(startBuf), "%Y-%m-%d %H:%M:%S", &tm);

            std::string completedAtStr = "";
            if (passportData.getCompletedAt().has_value()) {
                auto completedAt = std::chrono::system_clock::to_time_t(passportData.getCompletedAt().value());
                tm = *std::localtime(&completedAt);
                char completeBuf[32];
                std::strftime(completeBuf, sizeof(completeBuf), "%Y-%m-%d %H:%M:%S", &tm);
                completedAtStr = completeBuf;
            }

            binder << passportData.getId().getId()
                   << passportData.getSod().getEncodedData()
                   << domain::model::toString(passportData.getVerificationStatus())
                   << passportData.getIssuingCountry()
                   << passportData.getDocumentNumber()
                   << std::string(startBuf)
                   << completedAtStr
                   << passportData.getProcessingDurationMs().value_or(0)
                   << passportData.getRawRequestData();

            binder >> [](const drogon::orm::Result&) {
                spdlog::debug("PassportData saved successfully");
            };

            binder >> [](const drogon::orm::DrogonDbException& e) {
                spdlog::error("Failed to save PassportData: {}", e.base().what());
            };

        } catch (const std::exception& e) {
            spdlog::error("Error saving PassportData: {}", e.what());
            throw;
        }
    }

    std::optional<domain::model::PassportData> findById(const domain::model::PassportDataId& id) override {
        spdlog::debug("Finding PassportData by ID: {}", id.getId());

        // TODO: Implement full retrieval with data groups
        return std::nullopt;
    }

    std::optional<domain::model::PassportData> findByVerificationId(const std::string& verificationId) override {
        return findById(domain::model::PassportDataId::of(verificationId));
    }

    std::vector<domain::model::PassportData> findAll(int offset, int limit) override {
        spdlog::debug("Finding all PassportData, offset: {}, limit: {}", offset, limit);
        // TODO: Implement
        return {};
    }

    std::vector<domain::model::PassportData> findByStatus(
        domain::model::PassiveAuthenticationStatus status,
        int offset,
        int limit
    ) override {
        spdlog::debug("Finding PassportData by status: {}", domain::model::toString(status));
        // TODO: Implement
        return {};
    }

    std::vector<domain::model::PassportData> findByCountry(
        const std::string& countryCode,
        int offset,
        int limit
    ) override {
        spdlog::debug("Finding PassportData by country: {}", countryCode);
        // TODO: Implement
        return {};
    }

    long countAll() override {
        // TODO: Implement
        return 0;
    }

    long countByStatus(domain::model::PassiveAuthenticationStatus status) override {
        // TODO: Implement
        return 0;
    }

    bool deleteById(const domain::model::PassportDataId& id) override {
        spdlog::debug("Deleting PassportData: {}", id.getId());
        // TODO: Implement
        return false;
    }
};

} // namespace pa::infrastructure::repository
