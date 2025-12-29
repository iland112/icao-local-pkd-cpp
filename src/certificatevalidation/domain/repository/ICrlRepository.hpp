/**
 * @file ICrlRepository.hpp
 * @brief Repository interface for CertificateRevocationList aggregate
 */

#pragma once

#include "certificatevalidation/domain/model/CertificateRevocationList.hpp"
#include "certificatevalidation/domain/model/CrlId.hpp"
#include "certificatevalidation/domain/model/IssuerName.hpp"
#include "certificatevalidation/domain/model/CountryCode.hpp"
#include <optional>
#include <vector>
#include <string>

namespace certificatevalidation::domain::repository {

using namespace certificatevalidation::domain::model;

/**
 * @brief Repository interface for CertificateRevocationList aggregate
 *
 * Defines the contract for CRL persistence operations.
 */
class ICrlRepository {
public:
    virtual ~ICrlRepository() = default;

    /**
     * @brief Save a CRL
     */
    virtual void save(const CertificateRevocationList& crl) = 0;

    /**
     * @brief Find CRL by ID
     */
    virtual std::optional<CertificateRevocationList> findById(const CrlId& id) = 0;

    /**
     * @brief Find CRL by issuer name and country code
     */
    virtual std::optional<CertificateRevocationList> findByIssuerNameAndCountry(
        const std::string& issuerName,
        const std::string& countryCode
    ) = 0;

    /**
     * @brief Find the latest valid CRL for an issuer
     */
    virtual std::optional<CertificateRevocationList> findLatestByIssuerName(
        const IssuerName& issuerName
    ) = 0;

    /**
     * @brief Find the latest valid CRL for a country
     */
    virtual std::optional<CertificateRevocationList> findLatestByCountry(
        const CountryCode& countryCode
    ) = 0;

    /**
     * @brief Find all CRLs by upload ID
     */
    virtual std::vector<CertificateRevocationList> findByUploadId(const std::string& uploadId) = 0;

    /**
     * @brief Find all CRLs by country
     */
    virtual std::vector<CertificateRevocationList> findByCountry(const std::string& countryCode) = 0;

    /**
     * @brief Find all valid CRLs
     */
    virtual std::vector<CertificateRevocationList> findAllValid() = 0;

    /**
     * @brief Find all expired CRLs
     */
    virtual std::vector<CertificateRevocationList> findExpired() = 0;

    /**
     * @brief Count CRLs by country
     */
    virtual size_t countByCountry(const std::string& countryCode) = 0;

    /**
     * @brief Delete CRL by ID
     */
    virtual void deleteById(const CrlId& id) = 0;

    /**
     * @brief Invalidate all CRLs for an issuer (when new CRL arrives)
     */
    virtual void invalidateByIssuer(const IssuerName& issuerName) = 0;
};

} // namespace certificatevalidation::domain::repository
