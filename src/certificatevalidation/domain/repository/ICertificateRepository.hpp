/**
 * @file ICertificateRepository.hpp
 * @brief Repository interface for Certificate aggregate
 */

#pragma once

#include "certificatevalidation/domain/model/Certificate.hpp"
#include "certificatevalidation/domain/model/CertificateId.hpp"
#include "certificatevalidation/domain/model/CertificateType.hpp"
#include "certificatevalidation/domain/model/CertificateStatus.hpp"
#include <optional>
#include <vector>
#include <string>

namespace certificatevalidation::domain::repository {

using namespace certificatevalidation::domain::model;

/**
 * @brief Repository interface for Certificate aggregate
 *
 * Defines the contract for certificate persistence operations.
 * Implementations should be in the infrastructure layer.
 */
class ICertificateRepository {
public:
    virtual ~ICertificateRepository() = default;

    /**
     * @brief Save a certificate
     */
    virtual void save(const Certificate& certificate) = 0;

    /**
     * @brief Find certificate by ID
     */
    virtual std::optional<Certificate> findById(const CertificateId& id) = 0;

    /**
     * @brief Find certificate by Subject DN
     */
    virtual std::optional<Certificate> findBySubjectDn(const std::string& subjectDn) = 0;

    /**
     * @brief Find certificate by serial number and issuer DN
     */
    virtual std::optional<Certificate> findBySerialNumberAndIssuerDn(
        const std::string& serialNumber,
        const std::string& issuerDn
    ) = 0;

    /**
     * @brief Find certificate by fingerprint
     */
    virtual std::optional<Certificate> findByFingerprint(const std::string& fingerprintSha256) = 0;

    /**
     * @brief Find all certificates by upload ID
     */
    virtual std::vector<Certificate> findByUploadId(const std::string& uploadId) = 0;

    /**
     * @brief Find all certificates by type
     */
    virtual std::vector<Certificate> findByType(CertificateType type) = 0;

    /**
     * @brief Find all certificates by country code
     */
    virtual std::vector<Certificate> findByCountryCode(const std::string& countryCode) = 0;

    /**
     * @brief Find all certificates by type and country
     */
    virtual std::vector<Certificate> findByTypeAndCountry(
        CertificateType type,
        const std::string& countryCode
    ) = 0;

    /**
     * @brief Find all certificates by status
     */
    virtual std::vector<Certificate> findByStatus(CertificateStatus status) = 0;

    /**
     * @brief Find all CSCA certificates
     */
    virtual std::vector<Certificate> findAllCsca() = 0;

    /**
     * @brief Find all DSC certificates issued by a CSCA
     */
    virtual std::vector<Certificate> findDscByIssuerDn(const std::string& issuerDn) = 0;

    /**
     * @brief Find expired certificates
     */
    virtual std::vector<Certificate> findExpired() = 0;

    /**
     * @brief Find certificates expiring within given days
     */
    virtual std::vector<Certificate> findExpiringSoon(int daysThreshold) = 0;

    /**
     * @brief Find certificates not yet uploaded to LDAP
     */
    virtual std::vector<Certificate> findNotUploadedToLdap() = 0;

    /**
     * @brief Count certificates by type
     */
    virtual size_t countByType(CertificateType type) = 0;

    /**
     * @brief Count certificates by country
     */
    virtual size_t countByCountry(const std::string& countryCode) = 0;

    /**
     * @brief Delete certificate by ID
     */
    virtual void deleteById(const CertificateId& id) = 0;

    /**
     * @brief Check if certificate exists by fingerprint
     */
    virtual bool existsByFingerprint(const std::string& fingerprintSha256) = 0;
};

} // namespace certificatevalidation::domain::repository
