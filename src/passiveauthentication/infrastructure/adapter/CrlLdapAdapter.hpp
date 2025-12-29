#pragma once

#include "passiveauthentication/domain/port/CrlLdapPort.hpp"
#include "passiveauthentication/domain/model/CrlCheckResult.hpp"
#include "ldapintegration/domain/port/ILdapConnectionPort.hpp"
#include "shared/exception/InfrastructureException.hpp"
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <spdlog/spdlog.h>
#include <memory>
#include <ctime>

namespace pa::infrastructure::adapter {

/**
 * LDAP adapter for CRL operations.
 *
 * Provides CRL retrieval and certificate revocation checking.
 */
class CrlLdapAdapter : public domain::port::CrlLdapPort {
private:
    std::shared_ptr<ldap::domain::port::ILdapConnectionPort> ldapPort_;

    X509_CRL* parseCrlFromDer(const std::vector<uint8_t>& derData) {
        if (derData.empty()) {
            return nullptr;
        }

        const uint8_t* p = derData.data();
        X509_CRL* crl = d2i_X509_CRL(nullptr, &p, static_cast<long>(derData.size()));

        if (!crl) {
            spdlog::warn("Failed to parse CRL from DER data");
        }

        return crl;
    }

    // Convert ASN1_TIME to time_point
    std::chrono::system_clock::time_point asn1TimeToTimePoint(const ASN1_TIME* asn1Time) {
        struct tm tm = {};
        ASN1_TIME_to_tm(asn1Time, &tm);
        time_t t = mktime(&tm);
        return std::chrono::system_clock::from_time_t(t);
    }

public:
    explicit CrlLdapAdapter(
        std::shared_ptr<ldap::domain::port::ILdapConnectionPort> ldapPort
    ) : ldapPort_(std::move(ldapPort)) {}

    X509_CRL* getCrl(
        const std::string& cscaSubjectDn,
        const std::string& countryCode
    ) override {
        spdlog::debug("Looking up CRL for CSCA: {}, country: {}", cscaSubjectDn, countryCode);

        try {
            auto crlData = ldapPort_->searchCrlByIssuer(cscaSubjectDn, countryCode);

            if (crlData.empty()) {
                spdlog::debug("CRL not found for CSCA: {}", cscaSubjectDn);
                return nullptr;
            }

            X509_CRL* crl = parseCrlFromDer(crlData);

            if (crl) {
                spdlog::debug("Successfully retrieved CRL for CSCA: {}", cscaSubjectDn);
            }

            return crl;

        } catch (const std::exception& e) {
            spdlog::error("Error looking up CRL: {}", e.what());
            return nullptr;
        }
    }

    domain::model::CrlCheckResult checkRevocation(
        X509* cert,
        X509_CRL* crl,
        X509* cscaCert
    ) override {
        spdlog::debug("Checking certificate revocation status");

        if (!cert || !crl || !cscaCert) {
            return domain::model::CrlCheckResult::invalid("Invalid parameters for revocation check");
        }

        // Step 1: Verify CRL signature
        if (!verifyCrlSignature(crl, cscaCert)) {
            return domain::model::CrlCheckResult::invalid("CRL signature verification failed");
        }

        // Step 2: Check CRL validity (nextUpdate)
        const ASN1_TIME* nextUpdate = X509_CRL_get0_nextUpdate(crl);
        if (nextUpdate) {
            int dayDiff = 0, secDiff = 0;
            if (ASN1_TIME_diff(&dayDiff, &secDiff, nullptr, nextUpdate)) {
                if (dayDiff < 0 || (dayDiff == 0 && secDiff < 0)) {
                    spdlog::warn("CRL has expired");
                    return domain::model::CrlCheckResult::expired("CRL has expired (nextUpdate passed)");
                }
            }
        }

        // Step 3: Check if certificate is in revoked list
        X509_REVOKED* revoked = nullptr;
        int ret = X509_CRL_get0_by_cert(crl, &revoked, cert);

        if (ret == 1 && revoked) {
            // Certificate is revoked
            const ASN1_TIME* revTime = X509_REVOKED_get0_revocationDate(revoked);
            auto revocationDate = asn1TimeToTimePoint(revTime);

            // Get revocation reason if available
            int reason = -1;
            ASN1_ENUMERATED* reasonEnum = nullptr;
            int idx = X509_REVOKED_get_ext_by_NID(revoked, NID_crl_reason, -1);
            if (idx >= 0) {
                X509_EXTENSION* ext = X509_REVOKED_get_ext(revoked, idx);
                if (ext) {
                    reasonEnum = static_cast<ASN1_ENUMERATED*>(X509V3_EXT_d2i(ext));
                    if (reasonEnum) {
                        reason = ASN1_ENUMERATED_get(reasonEnum);
                        ASN1_ENUMERATED_free(reasonEnum);
                    }
                }
            }

            spdlog::warn("Certificate is REVOKED, reason code: {}", reason);
            return domain::model::CrlCheckResult::revoked(revocationDate, reason);
        }

        // Certificate is not revoked
        spdlog::debug("Certificate is not revoked");
        return domain::model::CrlCheckResult::valid();
    }

    bool verifyCrlSignature(X509_CRL* crl, X509* cscaCert) override {
        if (!crl || !cscaCert) {
            return false;
        }

        EVP_PKEY* pubKey = X509_get_pubkey(cscaCert);
        if (!pubKey) {
            spdlog::error("Failed to get CSCA public key for CRL verification");
            return false;
        }

        int ret = X509_CRL_verify(crl, pubKey);
        EVP_PKEY_free(pubKey);

        if (ret == 1) {
            spdlog::debug("CRL signature verification passed");
            return true;
        } else {
            spdlog::warn("CRL signature verification failed");
            return false;
        }
    }
};

} // namespace pa::infrastructure::adapter
