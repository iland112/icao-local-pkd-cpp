/**
 * @file UploadToLdapUseCase.hpp
 * @brief Upload to LDAP Use Case
 */

#pragma once

#include "ldapintegration/domain/port/ILdapConnectionPort.hpp"
#include "ldapintegration/domain/model/LdapCertificateEntry.hpp"
#include "ldapintegration/domain/model/LdapCrlEntry.hpp"
#include "ldapintegration/domain/model/LdapMasterListEntry.hpp"
#include "shared/exception/ApplicationException.hpp"
#include <memory>
#include <vector>
#include <functional>
#include <spdlog/spdlog.h>

namespace ldapintegration::application::usecase {

using namespace ldapintegration::domain::port;
using namespace ldapintegration::domain::model;

/**
 * @brief Upload Result DTO
 */
struct UploadToLdapResult {
    int totalCount;
    int successCount;
    int skipCount;
    int errorCount;
    std::vector<std::string> errors;

    [[nodiscard]] bool isSuccess() const noexcept {
        return errorCount == 0;
    }

    [[nodiscard]] std::string getSummary() const {
        return "Total: " + std::to_string(totalCount) +
               ", Success: " + std::to_string(successCount) +
               ", Skipped: " + std::to_string(skipCount) +
               ", Errors: " + std::to_string(errorCount);
    }
};

/**
 * @brief Upload Command for certificates
 */
struct UploadCertificatesCommand {
    std::vector<LdapCertificateEntry> certificates;
    bool skipExisting = true;
    bool updateIfNewer = false;
};

/**
 * @brief Upload Command for CRLs
 */
struct UploadCrlsCommand {
    std::vector<LdapCrlEntry> crls;
    bool updateIfNewer = true;
};

/**
 * @brief Upload Command for Master Lists
 */
struct UploadMasterListsCommand {
    std::vector<LdapMasterListEntry> masterLists;
    bool updateIfNewer = true;
};

/**
 * @brief Upload to LDAP Use Case
 *
 * Handles uploading certificates, CRLs, and Master Lists to LDAP.
 * Supports batch operations with progress tracking.
 */
class UploadToLdapUseCase {
private:
    std::shared_ptr<ILdapConnectionPort> ldapPort_;

public:
    explicit UploadToLdapUseCase(std::shared_ptr<ILdapConnectionPort> ldapPort)
        : ldapPort_(std::move(ldapPort)) {
        if (!ldapPort_) {
            throw shared::exception::ApplicationException(
                "INVALID_LDAP_PORT",
                "LDAP port cannot be null"
            );
        }
    }

    /**
     * @brief Upload certificates to LDAP
     */
    UploadToLdapResult uploadCertificates(
        const UploadCertificatesCommand& command,
        ILdapConnectionPort::ProgressCallback progressCallback = nullptr
    ) {
        spdlog::info("Starting certificate upload: {} certificates", command.certificates.size());

        UploadToLdapResult result{
            .totalCount = static_cast<int>(command.certificates.size()),
            .successCount = 0,
            .skipCount = 0,
            .errorCount = 0,
            .errors = {}
        };

        if (progressCallback) {
            ldapPort_->setProgressCallback(progressCallback);
        }

        for (size_t i = 0; i < command.certificates.size(); ++i) {
            const auto& cert = command.certificates[i];

            try {
                // Check if certificate exists
                auto existing = ldapPort_->findCertificateByFingerprint(
                    cert.getFingerprint(),
                    cert.getEntryType()
                );

                if (existing) {
                    if (command.skipExisting) {
                        ++result.skipCount;
                        spdlog::debug("Skipping existing certificate: {}", cert.getFingerprint());
                        continue;
                    }

                    if (command.updateIfNewer) {
                        // Update existing certificate
                        auto opResult = ldapPort_->saveCertificate(cert);
                        if (opResult.success) {
                            ++result.successCount;
                        } else {
                            ++result.errorCount;
                            result.errors.push_back(opResult.message);
                        }
                        continue;
                    }

                    ++result.skipCount;
                    continue;
                }

                // Add new certificate
                auto opResult = ldapPort_->saveCertificate(cert);
                if (opResult.success) {
                    ++result.successCount;
                } else {
                    ++result.errorCount;
                    result.errors.push_back(opResult.message);
                }

            } catch (const std::exception& e) {
                ++result.errorCount;
                result.errors.push_back(std::string("Certificate upload failed: ") + e.what());
            }

            if (progressCallback) {
                progressCallback(
                    static_cast<int>(i + 1),
                    result.totalCount,
                    "Uploading certificate " + std::to_string(i + 1) + "/" + std::to_string(result.totalCount)
                );
            }
        }

        spdlog::info("Certificate upload complete: {}", result.getSummary());
        return result;
    }

    /**
     * @brief Upload CRLs to LDAP
     */
    UploadToLdapResult uploadCrls(
        const UploadCrlsCommand& command,
        ILdapConnectionPort::ProgressCallback progressCallback = nullptr
    ) {
        spdlog::info("Starting CRL upload: {} CRLs", command.crls.size());

        UploadToLdapResult result{
            .totalCount = static_cast<int>(command.crls.size()),
            .successCount = 0,
            .skipCount = 0,
            .errorCount = 0,
            .errors = {}
        };

        if (progressCallback) {
            ldapPort_->setProgressCallback(progressCallback);
        }

        for (size_t i = 0; i < command.crls.size(); ++i) {
            const auto& crl = command.crls[i];

            try {
                if (command.updateIfNewer) {
                    bool updated = ldapPort_->updateCrlIfNewer(crl);
                    if (updated) {
                        ++result.successCount;
                    } else {
                        ++result.skipCount;
                        spdlog::debug("Skipping older CRL: {}", crl.getIssuerDn());
                    }
                } else {
                    auto opResult = ldapPort_->saveCrl(crl);
                    if (opResult.success) {
                        ++result.successCount;
                    } else {
                        ++result.errorCount;
                        result.errors.push_back(opResult.message);
                    }
                }

            } catch (const std::exception& e) {
                ++result.errorCount;
                result.errors.push_back(std::string("CRL upload failed: ") + e.what());
            }

            if (progressCallback) {
                progressCallback(
                    static_cast<int>(i + 1),
                    result.totalCount,
                    "Uploading CRL " + std::to_string(i + 1) + "/" + std::to_string(result.totalCount)
                );
            }
        }

        spdlog::info("CRL upload complete: {}", result.getSummary());
        return result;
    }

    /**
     * @brief Upload Master Lists to LDAP
     */
    UploadToLdapResult uploadMasterLists(
        const UploadMasterListsCommand& command,
        ILdapConnectionPort::ProgressCallback progressCallback = nullptr
    ) {
        spdlog::info("Starting Master List upload: {} Master Lists", command.masterLists.size());

        UploadToLdapResult result{
            .totalCount = static_cast<int>(command.masterLists.size()),
            .successCount = 0,
            .skipCount = 0,
            .errorCount = 0,
            .errors = {}
        };

        if (progressCallback) {
            ldapPort_->setProgressCallback(progressCallback);
        }

        for (size_t i = 0; i < command.masterLists.size(); ++i) {
            const auto& ml = command.masterLists[i];

            try {
                if (command.updateIfNewer) {
                    bool updated = ldapPort_->updateMasterListIfNewer(ml);
                    if (updated) {
                        ++result.successCount;
                    } else {
                        ++result.skipCount;
                        spdlog::debug("Skipping older Master List: {}", ml.getIssuerDn());
                    }
                } else {
                    auto opResult = ldapPort_->saveMasterList(ml);
                    if (opResult.success) {
                        ++result.successCount;
                    } else {
                        ++result.errorCount;
                        result.errors.push_back(opResult.message);
                    }
                }

            } catch (const std::exception& e) {
                ++result.errorCount;
                result.errors.push_back(std::string("Master List upload failed: ") + e.what());
            }

            if (progressCallback) {
                progressCallback(
                    static_cast<int>(i + 1),
                    result.totalCount,
                    "Uploading Master List " + std::to_string(i + 1) + "/" + std::to_string(result.totalCount)
                );
            }
        }

        spdlog::info("Master List upload complete: {}", result.getSummary());
        return result;
    }

    /**
     * @brief Initialize country structure in LDAP
     */
    void initializeCountry(const std::string& countryCode) {
        spdlog::info("Initializing LDAP structure for country: {}", countryCode);

        auto result = ldapPort_->ensureCountryExists(countryCode);
        if (!result.success) {
            throw shared::exception::ApplicationException(
                "COUNTRY_INIT_FAILED",
                "Failed to initialize country: " + result.message
            );
        }

        // Initialize all OU types
        std::vector<LdapEntryType> types = {
            LdapEntryType::CSCA,
            LdapEntryType::DSC,
            LdapEntryType::DSC_NC,
            LdapEntryType::CRL,
            LdapEntryType::MASTER_LIST
        };

        for (auto type : types) {
            result = ldapPort_->ensureOuExists(type, countryCode);
            if (!result.success) {
                spdlog::warn("Failed to initialize OU for {}: {}",
                    toString(type), result.message);
            }
        }

        spdlog::info("Country initialization complete: {}", countryCode);
    }
};

} // namespace ldapintegration::application::usecase
