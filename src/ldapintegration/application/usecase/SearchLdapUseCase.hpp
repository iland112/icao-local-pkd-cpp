/**
 * @file SearchLdapUseCase.hpp
 * @brief Search LDAP Use Case
 */

#pragma once

#include "ldapintegration/domain/port/ILdapConnectionPort.hpp"
#include "ldapintegration/domain/model/LdapCertificateEntry.hpp"
#include "ldapintegration/domain/model/LdapCrlEntry.hpp"
#include "ldapintegration/domain/model/LdapMasterListEntry.hpp"
#include "shared/exception/ApplicationException.hpp"
#include <memory>
#include <vector>
#include <optional>
#include <spdlog/spdlog.h>

namespace ldapintegration::application::usecase {

using namespace ldapintegration::domain::port;
using namespace ldapintegration::domain::model;

/**
 * @brief Certificate Search Query
 */
struct CertificateSearchQuery {
    std::optional<std::string> countryCode;
    std::optional<std::string> fingerprint;
    std::optional<std::string> serialNumber;
    std::optional<std::string> issuerDn;
    std::optional<std::string> subjectDn;
    std::optional<LdapEntryType> entryType;
    bool includeExpired = false;
    int limit = 100;
    int offset = 0;
};

/**
 * @brief Certificate Search Result
 */
struct CertificateSearchResult {
    std::vector<LdapCertificateEntry> certificates;
    int totalCount;
    int page;
    int pageSize;
    bool hasMore;
};

/**
 * @brief CRL Search Query
 */
struct CrlSearchQuery {
    std::optional<std::string> countryCode;
    std::optional<std::string> issuerDn;
    bool includeExpired = false;
    int limit = 100;
    int offset = 0;
};

/**
 * @brief CRL Search Result
 */
struct CrlSearchResult {
    std::vector<LdapCrlEntry> crls;
    int totalCount;
    int page;
    int pageSize;
    bool hasMore;
};

/**
 * @brief Search LDAP Use Case
 *
 * Provides search capabilities for LDAP entries.
 */
class SearchLdapUseCase {
private:
    std::shared_ptr<ILdapConnectionPort> ldapPort_;

public:
    explicit SearchLdapUseCase(std::shared_ptr<ILdapConnectionPort> ldapPort)
        : ldapPort_(std::move(ldapPort)) {
        if (!ldapPort_) {
            throw shared::exception::ApplicationException(
                "INVALID_LDAP_PORT",
                "LDAP port cannot be null"
            );
        }
    }

    /**
     * @brief Search certificates
     */
    CertificateSearchResult searchCertificates(const CertificateSearchQuery& query) {
        spdlog::debug("Searching certificates with query");

        CertificateSearchResult result{
            .certificates = {},
            .totalCount = 0,
            .page = query.offset / query.limit + 1,
            .pageSize = query.limit,
            .hasMore = false
        };

        try {
            // Determine entry types to search
            std::vector<LdapEntryType> types;
            if (query.entryType) {
                types.push_back(*query.entryType);
            } else {
                types = {LdapEntryType::CSCA, LdapEntryType::DSC, LdapEntryType::DSC_NC};
            }

            // Search by fingerprint if provided
            if (query.fingerprint) {
                for (auto type : types) {
                    auto cert = ldapPort_->findCertificateByFingerprint(*query.fingerprint, type);
                    if (cert) {
                        if (!query.includeExpired && cert->isExpired()) {
                            continue;
                        }
                        result.certificates.push_back(std::move(*cert));
                        break;
                    }
                }
                result.totalCount = static_cast<int>(result.certificates.size());
                return result;
            }

            // Search by issuer DN if provided
            if (query.issuerDn) {
                for (auto type : types) {
                    auto cert = ldapPort_->findCertificateByIssuerDn(*query.issuerDn, type);
                    if (cert) {
                        if (!query.includeExpired && cert->isExpired()) {
                            continue;
                        }
                        result.certificates.push_back(std::move(*cert));
                    }
                }
                result.totalCount = static_cast<int>(result.certificates.size());
                return result;
            }

            // Search by country if provided
            if (query.countryCode) {
                for (auto type : types) {
                    auto certs = ldapPort_->findCertificatesByCountry(*query.countryCode, type);
                    for (auto& cert : certs) {
                        if (!query.includeExpired && cert.isExpired()) {
                            continue;
                        }
                        result.certificates.push_back(std::move(cert));
                    }
                }
            }

            result.totalCount = static_cast<int>(result.certificates.size());

            // Apply pagination
            if (query.offset < static_cast<int>(result.certificates.size())) {
                auto start = result.certificates.begin() + query.offset;
                auto end = (query.offset + query.limit < static_cast<int>(result.certificates.size()))
                    ? start + query.limit
                    : result.certificates.end();

                std::vector<LdapCertificateEntry> paginated(start, end);
                result.certificates = std::move(paginated);
                result.hasMore = (query.offset + query.limit < result.totalCount);
            } else {
                result.certificates.clear();
            }

            spdlog::info("Certificate search found {} results", result.totalCount);

        } catch (const std::exception& e) {
            spdlog::error("Certificate search failed: {}", e.what());
            throw shared::exception::ApplicationException(
                "SEARCH_FAILED",
                std::string("Certificate search failed: ") + e.what()
            );
        }

        return result;
    }

    /**
     * @brief Search CRLs
     */
    CrlSearchResult searchCrls(const CrlSearchQuery& query) {
        spdlog::debug("Searching CRLs with query");

        CrlSearchResult result{
            .crls = {},
            .totalCount = 0,
            .page = query.offset / query.limit + 1,
            .pageSize = query.limit,
            .hasMore = false
        };

        try {
            // Search by issuer DN if provided
            if (query.issuerDn) {
                auto crl = ldapPort_->findCrlByIssuerDn(*query.issuerDn);
                if (crl) {
                    if (!query.includeExpired && crl->isExpired()) {
                        return result;
                    }
                    result.crls.push_back(std::move(*crl));
                }
                result.totalCount = static_cast<int>(result.crls.size());
                return result;
            }

            // Search by country if provided
            if (query.countryCode) {
                auto crls = ldapPort_->findCrlsByCountry(*query.countryCode);
                for (auto& crl : crls) {
                    if (!query.includeExpired && crl.isExpired()) {
                        continue;
                    }
                    result.crls.push_back(std::move(crl));
                }
            }

            result.totalCount = static_cast<int>(result.crls.size());

            // Apply pagination
            if (query.offset < static_cast<int>(result.crls.size())) {
                auto start = result.crls.begin() + query.offset;
                auto end = (query.offset + query.limit < static_cast<int>(result.crls.size()))
                    ? start + query.limit
                    : result.crls.end();

                std::vector<LdapCrlEntry> paginated(start, end);
                result.crls = std::move(paginated);
                result.hasMore = (query.offset + query.limit < result.totalCount);
            } else {
                result.crls.clear();
            }

            spdlog::info("CRL search found {} results", result.totalCount);

        } catch (const std::exception& e) {
            spdlog::error("CRL search failed: {}", e.what());
            throw shared::exception::ApplicationException(
                "SEARCH_FAILED",
                std::string("CRL search failed: ") + e.what()
            );
        }

        return result;
    }

    /**
     * @brief Find CSCA certificate for DSC verification
     */
    std::optional<LdapCertificateEntry> findCscaForDsc(const std::string& issuerDn) {
        spdlog::debug("Finding CSCA for DSC issuer: {}", issuerDn);

        // First try to find exact match
        auto csca = ldapPort_->findCertificateByIssuerDn(issuerDn, LdapEntryType::CSCA);
        if (csca && csca->isCurrentlyValid()) {
            return csca;
        }

        // If not found, try searching by subject DN
        // (CSCA's subject DN should match DSC's issuer DN)
        std::string baseDn = ldapPort_->getBaseDn();
        std::string filter = "(subjectDN=" + escapeLdapFilter(issuerDn) + ")";

        auto results = ldapPort_->search(
            LdapSearchFilter::subtree(
                getOuPath(LdapEntryType::CSCA, baseDn),
                filter
            )
        );

        if (!results.empty()) {
            spdlog::info("Found CSCA for DSC issuer: {}", issuerDn);
            // Convert to LdapCertificateEntry (simplified)
            // Real implementation would properly parse the result
        }

        spdlog::warn("CSCA not found for DSC issuer: {}", issuerDn);
        return std::nullopt;
    }

    /**
     * @brief Find CRL for certificate revocation check
     */
    std::optional<LdapCrlEntry> findCrlForCertificate(const std::string& issuerDn) {
        spdlog::debug("Finding CRL for certificate issuer: {}", issuerDn);

        auto crl = ldapPort_->findCrlByIssuerDn(issuerDn);
        if (crl) {
            if (crl->isExpired()) {
                spdlog::warn("Found expired CRL for issuer: {}", issuerDn);
            } else {
                spdlog::debug("Found valid CRL for issuer: {}", issuerDn);
            }
        }

        return crl;
    }

    /**
     * @brief Check if a certificate is revoked
     */
    bool isCertificateRevoked(const std::string& issuerDn, const std::string& serialNumber) {
        spdlog::debug("Checking revocation status for serial: {}", serialNumber);

        auto crl = ldapPort_->findCrlByIssuerDn(issuerDn);
        if (!crl) {
            spdlog::warn("No CRL found for issuer: {}", issuerDn);
            return false;  // No CRL available, assume not revoked
        }

        return crl->isSerialNumberRevoked(serialNumber);
    }

private:
    std::string escapeLdapFilter(const std::string& value) {
        std::string result;
        for (char c : value) {
            switch (c) {
                case '*':  result += "\\2a"; break;
                case '(':  result += "\\28"; break;
                case ')':  result += "\\29"; break;
                case '\\': result += "\\5c"; break;
                case '\0': result += "\\00"; break;
                default:   result += c;
            }
        }
        return result;
    }
};

} // namespace ldapintegration::application::usecase
