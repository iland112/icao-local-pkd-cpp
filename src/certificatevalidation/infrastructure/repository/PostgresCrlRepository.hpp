/**
 * @file PostgresCrlRepository.hpp
 * @brief PostgreSQL implementation of ICrlRepository
 */

#pragma once

#include "certificatevalidation/domain/repository/ICrlRepository.hpp"
#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

namespace certificatevalidation::infrastructure::repository {

using namespace certificatevalidation::domain::model;
using namespace certificatevalidation::domain::repository;

/**
 * @brief PostgreSQL implementation of ICrlRepository
 */
class PostgresCrlRepository : public ICrlRepository {
private:
    /**
     * @brief Convert database row to CertificateRevocationList
     */
    CertificateRevocationList rowToCrl(const drogon::orm::Row& row) {
        std::string id = row["id"].as<std::string>();
        std::string uploadId = row["upload_id"].as<std::string>();
        std::string issuerName = row["issuer_name"].as<std::string>();
        std::string countryCode = row["country_code"].as<std::string>();
        bool isValidCrl = row["is_valid"].as<bool>();

        // Parse CRL number
        std::optional<std::string> crlNumber;
        if (!row["crl_number"].isNull()) {
            crlNumber = row["crl_number"].as<std::string>();
        }

        // Parse binary data
        std::vector<uint8_t> crlBinary;
        auto binaryField = row["crl_binary"];
        if (!binaryField.isNull()) {
            auto data = binaryField.as<std::string>();
            crlBinary.assign(data.begin(), data.end());
        }

        // Parse timestamps (simplified)
        auto thisUpdate = std::chrono::system_clock::now() - std::chrono::hours(24);
        auto nextUpdate = std::chrono::system_clock::now() + std::chrono::hours(24 * 30);
        auto createdAt = std::chrono::system_clock::now();

        // Parse revoked certificates
        std::unordered_set<std::string> revokedSerials;
        // In real implementation, query revoked_certificate table
        // For now, return empty set

        return CertificateRevocationList::reconstitute(
            CrlId::of(id),
            uploadId,
            IssuerName::of(issuerName),
            CountryCode::of(countryCode),
            crlNumber,
            ValidityPeriod::of(thisUpdate, nextUpdate),
            X509CrlData::of(std::move(crlBinary)),
            RevokedCertificates::of(std::move(revokedSerials)),
            isValidCrl,
            createdAt
        );
    }

public:
    void save(const CertificateRevocationList& crl) override {
        spdlog::debug("Saving CRL: {}", crl.getId().getValue());

        auto clientPtr = drogon::app().getDbClient();

        // Check if exists
        auto existsResult = clientPtr->execSqlSync(
            "SELECT COUNT(*) FROM crl WHERE id = $1",
            crl.getId().getValue()
        );

        bool exists = existsResult[0]["count"].as<int>() > 0;

        if (exists) {
            // Update
            clientPtr->execSqlSync(
                R"(UPDATE crl SET
                    is_valid = $2,
                    updated_at = NOW()
                WHERE id = $1)",
                crl.getId().getValue(),
                crl.getIsValidCrl()
            );
        } else {
            // Insert
            clientPtr->execSqlSync(
                R"(INSERT INTO crl (
                    id, upload_id, issuer_name, country_code,
                    crl_number, this_update, next_update,
                    crl_binary, is_valid, created_at, updated_at
                ) VALUES (
                    $1, $2, $3, $4, $5, $6, $7, $8, $9, NOW(), NOW()
                ))",
                crl.getId().getValue(),
                crl.getUploadId(),
                crl.getIssuerName().getValue(),
                crl.getCountryCode().getValue(),
                crl.getCrlNumber().value_or(""),
                std::chrono::system_clock::to_time_t(crl.getValidityPeriod().getNotBefore()),
                std::chrono::system_clock::to_time_t(crl.getValidityPeriod().getNotAfter()),
                std::string(crl.getCrlBinary().begin(), crl.getCrlBinary().end()),
                crl.getIsValidCrl()
            );

            // Insert revoked certificates
            for (const auto& serial : crl.getRevokedCertificates().getSerialNumbers()) {
                clientPtr->execSqlSync(
                    R"(INSERT INTO revoked_certificate (
                        crl_id, serial_number, revocation_date
                    ) VALUES ($1, $2, NOW()))",
                    crl.getId().getValue(),
                    serial
                );
            }
        }
    }

    std::optional<CertificateRevocationList> findById(const CrlId& id) override {
        spdlog::debug("Finding CRL by id: {}", id.getValue());

        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM crl WHERE id = $1",
            id.getValue()
        );

        if (result.empty()) {
            return std::nullopt;
        }

        return rowToCrl(result[0]);
    }

    std::optional<CertificateRevocationList> findByIssuerNameAndCountry(
        const std::string& issuerName,
        const std::string& countryCode
    ) override {
        spdlog::debug("Finding CRL by issuer={}, country={}", issuerName, countryCode);

        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM crl WHERE issuer_name = $1 AND country_code = $2 AND is_valid = TRUE ORDER BY this_update DESC LIMIT 1",
            issuerName, countryCode
        );

        if (result.empty()) {
            return std::nullopt;
        }

        return rowToCrl(result[0]);
    }

    std::optional<CertificateRevocationList> findLatestByIssuerName(
        const IssuerName& issuerName
    ) override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM crl WHERE issuer_name = $1 AND is_valid = TRUE ORDER BY this_update DESC LIMIT 1",
            issuerName.getValue()
        );

        if (result.empty()) {
            return std::nullopt;
        }

        return rowToCrl(result[0]);
    }

    std::optional<CertificateRevocationList> findLatestByCountry(
        const CountryCode& countryCode
    ) override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM crl WHERE country_code = $1 AND is_valid = TRUE ORDER BY this_update DESC LIMIT 1",
            countryCode.getValue()
        );

        if (result.empty()) {
            return std::nullopt;
        }

        return rowToCrl(result[0]);
    }

    std::vector<CertificateRevocationList> findByUploadId(const std::string& uploadId) override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM crl WHERE upload_id = $1 ORDER BY created_at DESC",
            uploadId
        );

        std::vector<CertificateRevocationList> crls;
        for (const auto& row : result) {
            crls.push_back(rowToCrl(row));
        }
        return crls;
    }

    std::vector<CertificateRevocationList> findByCountry(const std::string& countryCode) override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM crl WHERE country_code = $1 ORDER BY this_update DESC",
            countryCode
        );

        std::vector<CertificateRevocationList> crls;
        for (const auto& row : result) {
            crls.push_back(rowToCrl(row));
        }
        return crls;
    }

    std::vector<CertificateRevocationList> findAllValid() override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM crl WHERE is_valid = TRUE AND next_update > NOW() ORDER BY this_update DESC"
        );

        std::vector<CertificateRevocationList> crls;
        for (const auto& row : result) {
            crls.push_back(rowToCrl(row));
        }
        return crls;
    }

    std::vector<CertificateRevocationList> findExpired() override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM crl WHERE next_update < NOW() ORDER BY next_update DESC"
        );

        std::vector<CertificateRevocationList> crls;
        for (const auto& row : result) {
            crls.push_back(rowToCrl(row));
        }
        return crls;
    }

    size_t countByCountry(const std::string& countryCode) override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT COUNT(*) FROM crl WHERE country_code = $1",
            countryCode
        );
        return result[0]["count"].as<size_t>();
    }

    void deleteById(const CrlId& id) override {
        auto clientPtr = drogon::app().getDbClient();

        // Delete revoked certificates first
        clientPtr->execSqlSync(
            "DELETE FROM revoked_certificate WHERE crl_id = $1",
            id.getValue()
        );

        // Delete CRL
        clientPtr->execSqlSync(
            "DELETE FROM crl WHERE id = $1",
            id.getValue()
        );
    }

    void invalidateByIssuer(const IssuerName& issuerName) override {
        spdlog::info("Invalidating CRLs for issuer: {}", issuerName.getValue());

        auto clientPtr = drogon::app().getDbClient();
        clientPtr->execSqlSync(
            "UPDATE crl SET is_valid = FALSE, updated_at = NOW() WHERE issuer_name = $1",
            issuerName.getValue()
        );
    }
};

} // namespace certificatevalidation::infrastructure::repository
