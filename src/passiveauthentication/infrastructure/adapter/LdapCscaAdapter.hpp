#pragma once

#include "passiveauthentication/domain/port/LdapCscaPort.hpp"
#include "ldapintegration/domain/port/ILdapConnectionPort.hpp"
#include "shared/exception/InfrastructureException.hpp"
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <spdlog/spdlog.h>
#include <memory>

namespace pa::infrastructure::adapter {

/**
 * LDAP adapter for CSCA certificate lookup.
 *
 * Uses the existing LDAP integration module to retrieve CSCA certificates.
 */
class LdapCscaAdapter : public domain::port::LdapCscaPort {
private:
    std::shared_ptr<ldap::domain::port::ILdapConnectionPort> ldapPort_;
    std::string baseDn_;

    X509* parseCertificateFromDer(const std::vector<uint8_t>& derData) {
        if (derData.empty()) {
            return nullptr;
        }

        const uint8_t* p = derData.data();
        X509* cert = d2i_X509(nullptr, &p, static_cast<long>(derData.size()));

        if (!cert) {
            spdlog::warn("Failed to parse certificate from DER data");
        }

        return cert;
    }

public:
    LdapCscaAdapter(
        std::shared_ptr<ldap::domain::port::ILdapConnectionPort> ldapPort,
        const std::string& baseDn = "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com"
    ) : ldapPort_(std::move(ldapPort)), baseDn_(baseDn) {}

    X509* findBySubjectDn(const std::string& subjectDn) override {
        spdlog::debug("Looking up CSCA by subject DN: {}", subjectDn);

        try {
            // Search for CSCA certificate in LDAP
            // The certificate is stored with its subject DN as part of the entry DN
            auto certData = ldapPort_->searchCertificateBySubjectDn(subjectDn, "csca");

            if (certData.empty()) {
                spdlog::debug("CSCA not found for DN: {}", subjectDn);
                return nullptr;
            }

            return parseCertificateFromDer(certData);

        } catch (const std::exception& e) {
            spdlog::error("Error looking up CSCA: {}", e.what());
            return nullptr;
        }
    }

    std::vector<X509*> findByCountry(const std::string& countryCode) override {
        spdlog::debug("Looking up all CSCAs for country: {}", countryCode);

        std::vector<X509*> result;

        try {
            auto certificates = ldapPort_->searchCertificatesByCountry(countryCode, "csca");

            for (const auto& certData : certificates) {
                X509* cert = parseCertificateFromDer(certData);
                if (cert) {
                    result.push_back(cert);
                }
            }

            spdlog::info("Found {} CSCA certificates for country {}", result.size(), countryCode);

        } catch (const std::exception& e) {
            spdlog::error("Error looking up CSCAs for country: {}", e.what());
        }

        return result;
    }

    bool existsBySubjectDn(const std::string& subjectDn) override {
        spdlog::debug("Checking if CSCA exists for DN: {}", subjectDn);

        try {
            return ldapPort_->certificateExistsBySubjectDn(subjectDn, "csca");
        } catch (const std::exception& e) {
            spdlog::error("Error checking CSCA existence: {}", e.what());
            return false;
        }
    }
};

} // namespace pa::infrastructure::adapter
