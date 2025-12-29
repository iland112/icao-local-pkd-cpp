/**
 * @file PostgresCertificateRepository.hpp
 * @brief PostgreSQL implementation of ICertificateRepository
 */

#pragma once

#include "certificatevalidation/domain/repository/ICertificateRepository.hpp"
#include <drogon/drogon.h>
#include <spdlog/spdlog.h>

namespace certificatevalidation::infrastructure::repository {

using namespace certificatevalidation::domain::model;
using namespace certificatevalidation::domain::repository;

/**
 * @brief PostgreSQL implementation of ICertificateRepository
 *
 * Uses Drogon ORM for database operations.
 */
class PostgresCertificateRepository : public ICertificateRepository {
private:
    /**
     * @brief Convert database row to Certificate
     */
    Certificate rowToCertificate(const drogon::orm::Row& row) {
        // Extract values from row
        std::string id = row["id"].as<std::string>();
        std::string uploadId = row["upload_id"].as<std::string>();
        std::string serialNumber = row["serial_number"].as<std::string>();
        std::string subjectDn = row["subject_dn"].as<std::string>();
        std::string issuerDn = row["issuer_dn"].as<std::string>();
        std::string subjectCountryCode = row["subject_country_code"].as<std::string>();
        std::string typeStr = row["certificate_type"].as<std::string>();
        std::string statusStr = row["status"].as<std::string>();
        std::string signatureAlgorithm = row["signature_algorithm"].as<std::string>();
        std::string sourceTypeStr = row["source_type"].as<std::string>();
        std::string fingerprint = row["fingerprint_sha256"].as<std::string>();
        bool uploadedToLdap = row["uploaded_to_ldap"].as<bool>();

        // Parse binary data
        std::vector<uint8_t> certBinary;
        auto binaryField = row["certificate_binary"];
        if (!binaryField.isNull()) {
            auto data = binaryField.as<std::string>();
            certBinary.assign(data.begin(), data.end());
        }

        // Parse timestamps
        auto notBeforeStr = row["not_before"].as<std::string>();
        auto notAfterStr = row["not_after"].as<std::string>();

        // Convert to time_point (simplified - actual implementation needs proper parsing)
        auto notBefore = std::chrono::system_clock::now();
        auto notAfter = std::chrono::system_clock::now() + std::chrono::hours(24 * 365);

        // Build certificate
        auto x509Data = X509Data::of(std::move(certBinary), serialNumber, fingerprint);
        auto subjectInfo = SubjectInfo::fromDn(subjectDn);
        auto issuerInfo = IssuerInfo::fromDn(issuerDn, typeStr == "CSCA");
        auto validity = ValidityPeriod::of(notBefore, notAfter);

        CertificateType certType = parseCertificateType(typeStr);
        CertificateStatus status = parseCertificateStatus(statusStr);
        CertificateSourceType sourceType = CertificateSourceType::LDIF_DSC;
        if (sourceTypeStr == "LDIF_CSCA") sourceType = CertificateSourceType::LDIF_CSCA;
        else if (sourceTypeStr == "MASTER_LIST") sourceType = CertificateSourceType::MASTER_LIST;

        std::optional<std::string> masterListId;
        if (!row["master_list_id"].isNull()) {
            masterListId = row["master_list_id"].as<std::string>();
        }

        auto createdAtStr = row["created_at"].as<std::string>();
        auto createdAt = std::chrono::system_clock::now();

        return Certificate::reconstitute(
            CertificateId::of(id),
            uploadId,
            std::move(x509Data),
            std::move(subjectInfo),
            std::move(issuerInfo),
            std::move(validity),
            certType,
            status,
            signatureAlgorithm,
            sourceType,
            masterListId,
            uploadedToLdap,
            createdAt
        );
    }

public:
    void save(const Certificate& certificate) override {
        spdlog::debug("Saving certificate: {}", certificate.getId().getValue());

        auto clientPtr = drogon::app().getDbClient();

        // Check if exists
        auto existsResult = clientPtr->execSqlSync(
            "SELECT COUNT(*) FROM certificate WHERE id = $1",
            certificate.getId().getValue()
        );

        bool exists = existsResult[0]["count"].as<int>() > 0;

        if (exists) {
            // Update
            clientPtr->execSqlSync(
                R"(UPDATE certificate SET
                    status = $2,
                    validation_overall_status = $3,
                    validation_signature_valid = $4,
                    validation_chain_valid = $5,
                    validation_not_revoked = $6,
                    validation_validity_valid = $7,
                    uploaded_to_ldap = $8,
                    updated_at = NOW()
                WHERE id = $1)",
                certificate.getId().getValue(),
                toDbString(certificate.getStatus()),
                certificate.getValidationResult() ?
                    toDbString(certificate.getValidationResult()->getOverallStatus()) : "",
                certificate.getValidationResult() ?
                    certificate.getValidationResult()->isSignatureValid() : true,
                certificate.getValidationResult() ?
                    certificate.getValidationResult()->isChainValid() : false,
                certificate.getValidationResult() ?
                    certificate.getValidationResult()->isNotRevoked() : true,
                certificate.getValidationResult() ?
                    certificate.getValidationResult()->isValidityValid() : true,
                certificate.isUploadedToLdap()
            );
        } else {
            // Insert
            clientPtr->execSqlSync(
                R"(INSERT INTO certificate (
                    id, upload_id, certificate_binary, serial_number,
                    subject_dn, subject_country_code,
                    issuer_dn, issuer_country_code,
                    not_before, not_after,
                    certificate_type, status, signature_algorithm,
                    source_type, master_list_id,
                    fingerprint_sha256, uploaded_to_ldap,
                    created_at, updated_at
                ) VALUES (
                    $1, $2, $3, $4, $5, $6, $7, $8, $9, $10,
                    $11, $12, $13, $14, $15, $16, $17, NOW(), NOW()
                ))",
                certificate.getId().getValue(),
                certificate.getUploadId(),
                std::string(certificate.getX509Data().getCertificateBinary().begin(),
                           certificate.getX509Data().getCertificateBinary().end()),
                certificate.getX509Data().getSerialNumber(),
                certificate.getSubjectInfo().getDistinguishedName(),
                certificate.getSubjectInfo().getCountryCode(),
                certificate.getIssuerInfo().getDistinguishedName(),
                certificate.getIssuerInfo().getCountryCode(),
                std::chrono::system_clock::to_time_t(certificate.getValidity().getNotBefore()),
                std::chrono::system_clock::to_time_t(certificate.getValidity().getNotAfter()),
                toString(certificate.getCertificateType()),
                toDbString(certificate.getStatus()),
                certificate.getSignatureAlgorithm(),
                toString(certificate.getSourceType()),
                certificate.getMasterListId().value_or(""),
                certificate.getX509Data().getFingerprintSha256(),
                certificate.isUploadedToLdap()
            );
        }
    }

    std::optional<Certificate> findById(const CertificateId& id) override {
        spdlog::debug("Finding certificate by id: {}", id.getValue());

        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM certificate WHERE id = $1",
            id.getValue()
        );

        if (result.empty()) {
            return std::nullopt;
        }

        return rowToCertificate(result[0]);
    }

    std::optional<Certificate> findBySubjectDn(const std::string& subjectDn) override {
        spdlog::debug("Finding certificate by subject DN: {}", subjectDn);

        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM certificate WHERE subject_dn = $1 ORDER BY created_at DESC LIMIT 1",
            subjectDn
        );

        if (result.empty()) {
            return std::nullopt;
        }

        return rowToCertificate(result[0]);
    }

    std::optional<Certificate> findBySerialNumberAndIssuerDn(
        const std::string& serialNumber,
        const std::string& issuerDn
    ) override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM certificate WHERE serial_number = $1 AND issuer_dn = $2",
            serialNumber, issuerDn
        );

        if (result.empty()) {
            return std::nullopt;
        }

        return rowToCertificate(result[0]);
    }

    std::optional<Certificate> findByFingerprint(const std::string& fingerprintSha256) override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM certificate WHERE fingerprint_sha256 = $1",
            fingerprintSha256
        );

        if (result.empty()) {
            return std::nullopt;
        }

        return rowToCertificate(result[0]);
    }

    std::vector<Certificate> findByUploadId(const std::string& uploadId) override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM certificate WHERE upload_id = $1 ORDER BY created_at DESC",
            uploadId
        );

        std::vector<Certificate> certificates;
        for (const auto& row : result) {
            certificates.push_back(rowToCertificate(row));
        }
        return certificates;
    }

    std::vector<Certificate> findByType(CertificateType type) override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM certificate WHERE certificate_type = $1 ORDER BY created_at DESC",
            toString(type)
        );

        std::vector<Certificate> certificates;
        for (const auto& row : result) {
            certificates.push_back(rowToCertificate(row));
        }
        return certificates;
    }

    std::vector<Certificate> findByCountryCode(const std::string& countryCode) override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM certificate WHERE subject_country_code = $1 ORDER BY created_at DESC",
            countryCode
        );

        std::vector<Certificate> certificates;
        for (const auto& row : result) {
            certificates.push_back(rowToCertificate(row));
        }
        return certificates;
    }

    std::vector<Certificate> findByTypeAndCountry(
        CertificateType type,
        const std::string& countryCode
    ) override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM certificate WHERE certificate_type = $1 AND subject_country_code = $2 ORDER BY created_at DESC",
            toString(type), countryCode
        );

        std::vector<Certificate> certificates;
        for (const auto& row : result) {
            certificates.push_back(rowToCertificate(row));
        }
        return certificates;
    }

    std::vector<Certificate> findByStatus(CertificateStatus status) override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM certificate WHERE status = $1 ORDER BY created_at DESC",
            toDbString(status)
        );

        std::vector<Certificate> certificates;
        for (const auto& row : result) {
            certificates.push_back(rowToCertificate(row));
        }
        return certificates;
    }

    std::vector<Certificate> findAllCsca() override {
        return findByType(CertificateType::CSCA);
    }

    std::vector<Certificate> findDscByIssuerDn(const std::string& issuerDn) override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM certificate WHERE issuer_dn = $1 AND certificate_type IN ('DSC', 'DSC_NC') ORDER BY created_at DESC",
            issuerDn
        );

        std::vector<Certificate> certificates;
        for (const auto& row : result) {
            certificates.push_back(rowToCertificate(row));
        }
        return certificates;
    }

    std::vector<Certificate> findExpired() override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM certificate WHERE not_after < NOW() ORDER BY not_after DESC"
        );

        std::vector<Certificate> certificates;
        for (const auto& row : result) {
            certificates.push_back(rowToCertificate(row));
        }
        return certificates;
    }

    std::vector<Certificate> findExpiringSoon(int daysThreshold) override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM certificate WHERE not_after > NOW() AND not_after < NOW() + INTERVAL '" + std::to_string(daysThreshold) + " days' ORDER BY not_after ASC"
        );

        std::vector<Certificate> certificates;
        for (const auto& row : result) {
            certificates.push_back(rowToCertificate(row));
        }
        return certificates;
    }

    std::vector<Certificate> findNotUploadedToLdap() override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT * FROM certificate WHERE uploaded_to_ldap = FALSE ORDER BY created_at DESC"
        );

        std::vector<Certificate> certificates;
        for (const auto& row : result) {
            certificates.push_back(rowToCertificate(row));
        }
        return certificates;
    }

    size_t countByType(CertificateType type) override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT COUNT(*) FROM certificate WHERE certificate_type = $1",
            toString(type)
        );
        return result[0]["count"].as<size_t>();
    }

    size_t countByCountry(const std::string& countryCode) override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT COUNT(*) FROM certificate WHERE subject_country_code = $1",
            countryCode
        );
        return result[0]["count"].as<size_t>();
    }

    void deleteById(const CertificateId& id) override {
        auto clientPtr = drogon::app().getDbClient();
        clientPtr->execSqlSync(
            "DELETE FROM certificate WHERE id = $1",
            id.getValue()
        );
    }

    bool existsByFingerprint(const std::string& fingerprintSha256) override {
        auto clientPtr = drogon::app().getDbClient();
        auto result = clientPtr->execSqlSync(
            "SELECT COUNT(*) FROM certificate WHERE fingerprint_sha256 = $1",
            fingerprintSha256
        );
        return result[0]["count"].as<int>() > 0;
    }
};

} // namespace certificatevalidation::infrastructure::repository
