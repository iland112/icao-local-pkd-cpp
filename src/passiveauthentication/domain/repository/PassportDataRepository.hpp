#pragma once

#include "passiveauthentication/domain/model/PassportData.hpp"
#include "passiveauthentication/domain/model/PassportDataId.hpp"
#include "passiveauthentication/domain/model/PassiveAuthenticationStatus.hpp"
#include <optional>
#include <vector>
#include <string>

namespace pa::domain::repository {

/**
 * Repository interface for PassportData aggregate.
 */
class PassportDataRepository {
public:
    virtual ~PassportDataRepository() = default;

    /**
     * Save passport data.
     *
     * @param passportData Passport data to save
     */
    virtual void save(const model::PassportData& passportData) = 0;

    /**
     * Find passport data by ID.
     *
     * @param id Passport data ID
     * @return PassportData if found
     */
    virtual std::optional<model::PassportData> findById(const model::PassportDataId& id) = 0;

    /**
     * Find passport data by verification ID string.
     *
     * @param verificationId Verification ID string
     * @return PassportData if found
     */
    virtual std::optional<model::PassportData> findByVerificationId(const std::string& verificationId) = 0;

    /**
     * Find all passport data with pagination.
     *
     * @param offset Starting offset
     * @param limit Maximum number of results
     * @return Vector of passport data
     */
    virtual std::vector<model::PassportData> findAll(int offset, int limit) = 0;

    /**
     * Find passport data by status.
     *
     * @param status Verification status filter
     * @param offset Starting offset
     * @param limit Maximum number of results
     * @return Vector of passport data
     */
    virtual std::vector<model::PassportData> findByStatus(
        model::PassiveAuthenticationStatus status,
        int offset,
        int limit
    ) = 0;

    /**
     * Find passport data by issuing country.
     *
     * @param countryCode ISO 3166-1 country code
     * @param offset Starting offset
     * @param limit Maximum number of results
     * @return Vector of passport data
     */
    virtual std::vector<model::PassportData> findByCountry(
        const std::string& countryCode,
        int offset,
        int limit
    ) = 0;

    /**
     * Count all passport data.
     *
     * @return Total count
     */
    virtual long countAll() = 0;

    /**
     * Count passport data by status.
     *
     * @param status Verification status
     * @return Count for status
     */
    virtual long countByStatus(model::PassiveAuthenticationStatus status) = 0;

    /**
     * Delete passport data by ID.
     *
     * @param id Passport data ID
     * @return true if deleted
     */
    virtual bool deleteById(const model::PassportDataId& id) = 0;
};

} // namespace pa::domain::repository
