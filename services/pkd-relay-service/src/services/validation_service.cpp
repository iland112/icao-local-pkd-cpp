/**
 * @file validation_service.cpp
 * @brief Validation service implementation — 3-step revalidation pipeline
 *
 * Step 1: Expiration check (existing logic)
 * Step 2: Trust Chain re-validation for PENDING DSCs
 * Step 3: CRL re-check for VALID/EXPIRED_VALID DSCs
 */
#include "validation_service.h"
#include "../adapters/relay_csca_provider.h"
#include "../adapters/relay_crl_provider.h"

#include <icao/validation/trust_chain_builder.h>
#include <icao/validation/crl_checker.h>
#include <icao/validation/cert_ops.h>
#include <icao/validation/types.h>

#include <spdlog/spdlog.h>
#include <openssl/x509.h>
#include <chrono>
#include <stdexcept>

namespace icao::relay::services {

ValidationService::ValidationService(
    repositories::ValidationRepository* validationRepo,
    repositories::CertificateRepository* certRepo,
    repositories::CrlRepository* crlRepo
)
    : validationRepo_(validationRepo)
    , certRepo_(certRepo)
    , crlRepo_(crlRepo)
{
    if (!validationRepo_) {
        throw std::invalid_argument("ValidationRepository cannot be null");
    }
    if (!certRepo_) {
        throw std::invalid_argument("CertificateRepository cannot be null");
    }
    if (!crlRepo_) {
        throw std::invalid_argument("CrlRepository cannot be null");
    }

    // Create provider adapters and validation library instances
    auto cscaProv = std::make_unique<adapters::RelayCscaProvider>(certRepo_);
    auto crlProv = std::make_unique<adapters::RelayCrlProvider>(crlRepo_);

    trustChainBuilder_ = std::make_unique<icao::validation::TrustChainBuilder>(cscaProv.get());
    crlChecker_ = std::make_unique<icao::validation::CrlChecker>(crlProv.get());

    // Transfer ownership
    cscaProvider_ = std::move(cscaProv);
    crlProvider_ = std::move(crlProv);

    spdlog::debug("[ValidationService] Initialized with Trust Chain + CRL support");
}

ValidationService::~ValidationService() = default;

std::string ValidationService::determineValidationStatus(bool isExpired, const std::string& currentStatus) {
    if (isExpired) {
        return "INVALID";
    }
    if (currentStatus == "VALID") {
        return "VALID";
    }
    return "PENDING";
}

std::vector<uint8_t> ValidationService::decodeHexToDer(const std::string& hexData) {
    std::vector<uint8_t> derBytes;
    size_t hexStart = 0;

    if (hexData.size() > 2 && hexData[0] == '\\' && hexData[1] == 'x') {
        hexStart = 2;
    }

    derBytes.reserve((hexData.size() - hexStart) / 2);
    for (size_t i = hexStart; i + 1 < hexData.size(); i += 2) {
        char h[3] = {hexData[i], hexData[i + 1], 0};
        derBytes.push_back(static_cast<uint8_t>(strtol(h, nullptr, 16)));
    }

    // Handle double-encoded BYTEA
    if (derBytes.size() > 2 && derBytes[0] == 0x5C && derBytes[1] == 0x78) {
        std::vector<uint8_t> innerBytes;
        innerBytes.reserve((derBytes.size() - 2) / 2);
        for (size_t i = 2; i + 1 < derBytes.size(); i += 2) {
            char h[3] = {static_cast<char>(derBytes[i]), static_cast<char>(derBytes[i + 1]), 0};
            innerBytes.push_back(static_cast<uint8_t>(strtol(h, nullptr, 16)));
        }
        derBytes = std::move(innerBytes);
    }

    return derBytes;
}

Json::Value ValidationService::revalidateTrustChains() {
    Json::Value result;
    int tcProcessed = 0, tcNewlyValid = 0, tcStillPending = 0, tcErrors = 0;

    try {
        // Preload CSCA cache
        auto* cscaProv = dynamic_cast<adapters::RelayCscaProvider*>(cscaProvider_.get());
        if (cscaProv) {
            cscaProv->preloadAllCscas();
        }

        Json::Value dscs = validationRepo_->findDscsForTrustChainRevalidation();
        spdlog::info("[ValidationService] Step 2: Trust Chain re-validation — {} DSCs to process", dscs.size());

        for (const auto& dsc : dscs) {
            try {
                std::string id = dsc["id"].asString();
                std::string certDataHex = dsc.get("certificate_data", "").asString();

                if (certDataHex.empty()) {
                    tcErrors++;
                    continue;
                }

                std::vector<uint8_t> derBytes = decodeHexToDer(certDataHex);
                if (derBytes.empty()) {
                    tcErrors++;
                    continue;
                }

                const unsigned char* p = derBytes.data();
                X509* cert = d2i_X509(nullptr, &p, static_cast<long>(derBytes.size()));
                if (!cert) {
                    tcErrors++;
                    continue;
                }

                tcProcessed++;

                icao::validation::TrustChainResult tc = trustChainBuilder_->build(cert);
                X509_free(cert);

                if (tc.valid) {
                    std::string newStatus = tc.dscExpired ? "EXPIRED_VALID" : "VALID";
                    validationRepo_->updateTrustChainStatus(id, newStatus, true, tc.message);
                    tcNewlyValid++;
                } else {
                    tcStillPending++;
                }
            } catch (const std::exception& e) {
                tcErrors++;
                spdlog::warn("[ValidationService] Trust chain error for DSC: {}", e.what());
            }
        }

    } catch (const std::exception& e) {
        spdlog::error("[ValidationService] Trust chain re-validation failed: {}", e.what());
        tcErrors++;
    }

    result["tcProcessed"] = tcProcessed;
    result["tcNewlyValid"] = tcNewlyValid;
    result["tcStillPending"] = tcStillPending;
    result["tcErrors"] = tcErrors;
    return result;
}

Json::Value ValidationService::recheckCrls() {
    Json::Value result;
    int crlChecked = 0, crlRevoked = 0, crlUnavailable = 0, crlExpired = 0, crlErrors = 0;

    try {
        Json::Value dscs = validationRepo_->findDscsForCrlRecheck();
        spdlog::info("[ValidationService] Step 3: CRL re-check — {} DSCs to process", dscs.size());

        for (const auto& dsc : dscs) {
            try {
                std::string id = dsc["id"].asString();
                std::string countryCode = dsc.get("country_code", "").asString();
                std::string certDataHex = dsc.get("certificate_data", "").asString();

                if (certDataHex.empty() || countryCode.empty()) {
                    crlErrors++;
                    continue;
                }

                std::vector<uint8_t> derBytes = decodeHexToDer(certDataHex);
                if (derBytes.empty()) {
                    crlErrors++;
                    continue;
                }

                const unsigned char* p = derBytes.data();
                X509* cert = d2i_X509(nullptr, &p, static_cast<long>(derBytes.size()));
                if (!cert) {
                    crlErrors++;
                    continue;
                }

                crlChecked++;

                icao::validation::CrlCheckResult crl = crlChecker_->check(cert, countryCode);
                X509_free(cert);

                switch (crl.status) {
                    case icao::validation::CrlCheckStatus::REVOKED:
                        crlRevoked++;
                        validationRepo_->updateCrlStatus(id, "INVALID",
                            "Revoked (" + crl.revocationReason + ")");
                        break;
                    case icao::validation::CrlCheckStatus::CRL_UNAVAILABLE:
                        crlUnavailable++;
                        break;
                    case icao::validation::CrlCheckStatus::CRL_EXPIRED:
                        crlExpired++;
                        break;
                    case icao::validation::CrlCheckStatus::VALID:
                    case icao::validation::CrlCheckStatus::NOT_CHECKED:
                    case icao::validation::CrlCheckStatus::CRL_INVALID:
                        break;
                }
            } catch (const std::exception& e) {
                crlErrors++;
                spdlog::warn("[ValidationService] CRL check error: {}", e.what());
            }
        }

    } catch (const std::exception& e) {
        spdlog::error("[ValidationService] CRL re-check failed: {}", e.what());
        crlErrors++;
    }

    result["crlChecked"] = crlChecked;
    result["crlRevoked"] = crlRevoked;
    result["crlUnavailable"] = crlUnavailable;
    result["crlExpired"] = crlExpired;
    result["crlErrors"] = crlErrors;
    return result;
}

Json::Value ValidationService::revalidateAll() {
    auto startTime = std::chrono::steady_clock::now();

    Json::Value response;
    response["success"] = false;

    try {
        spdlog::info("[ValidationService] Starting 3-step certificate revalidation");

        // =============================================
        // Step 1: Expiration check (existing logic)
        // =============================================
        std::vector<domain::ValidationResult> validations = validationRepo_->findAllWithExpirationInfo();

        int totalProcessed = 0;
        int newlyExpired = 0;
        int newlyValid = 0;
        int unchanged = 0;
        int errors = 0;

        for (const auto& validation : validations) {
            try {
                bool currentExpired = !validation.isValidityPeriodValid();
                bool actualExpired = validation.isExpired();

                totalProcessed++;

                if (currentExpired != actualExpired) {
                    std::string newStatus = determineValidationStatus(actualExpired, validation.getValidationStatus());
                    bool newValidityPeriodValid = !actualExpired;

                    bool updated = validationRepo_->updateValidityStatus(
                        validation.getId(), newValidityPeriodValid, newStatus);

                    if (updated) {
                        if (actualExpired && !currentExpired) {
                            newlyExpired++;
                        } else if (!actualExpired && currentExpired) {
                            newlyValid++;
                        }
                    } else {
                        errors++;
                    }
                } else {
                    unchanged++;
                }
            } catch (const std::exception& e) {
                errors++;
                spdlog::error("[ValidationService] Error processing validation {}: {}", validation.getId(), e.what());
            }
        }

        validationRepo_->updateAllUploadExpiredCounts();

        spdlog::info("[ValidationService] Step 1 complete: {} processed, {} expired, {} valid, {} unchanged",
                    totalProcessed, newlyExpired, newlyValid, unchanged);

        // =============================================
        // Step 2: Trust Chain re-validation
        // =============================================
        Json::Value tcResult = revalidateTrustChains();

        // =============================================
        // Step 3: CRL re-check
        // =============================================
        Json::Value crlResult = recheckCrls();

        auto endTime = std::chrono::steady_clock::now();
        int durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();

        // Save extended history
        validationRepo_->saveRevalidationHistoryExtended(
            totalProcessed, newlyExpired, newlyValid, unchanged, errors, durationMs,
            tcResult["tcProcessed"].asInt(), tcResult["tcNewlyValid"].asInt(),
            tcResult["tcStillPending"].asInt(), tcResult["tcErrors"].asInt(),
            crlResult["crlChecked"].asInt(), crlResult["crlRevoked"].asInt(),
            crlResult["crlUnavailable"].asInt(), crlResult["crlExpired"].asInt(),
            crlResult["crlErrors"].asInt()
        );

        // Build response
        response["success"] = true;
        response["totalProcessed"] = totalProcessed;
        response["newlyExpired"] = newlyExpired;
        response["newlyValid"] = newlyValid;
        response["unchanged"] = unchanged;
        response["errors"] = errors;
        response["durationMs"] = durationMs;

        // Step 2 results
        response["tcProcessed"] = tcResult["tcProcessed"];
        response["tcNewlyValid"] = tcResult["tcNewlyValid"];
        response["tcStillPending"] = tcResult["tcStillPending"];
        response["tcErrors"] = tcResult["tcErrors"];

        // Step 3 results
        response["crlChecked"] = crlResult["crlChecked"];
        response["crlRevoked"] = crlResult["crlRevoked"];
        response["crlUnavailable"] = crlResult["crlUnavailable"];
        response["crlExpired"] = crlResult["crlExpired"];
        response["crlErrors"] = crlResult["crlErrors"];

        spdlog::info("[ValidationService] 3-step revalidation complete in {}ms: "
                    "Step1({} processed), Step2(TC {}/{}/{}), Step3(CRL {}/{}/{})",
                    durationMs, totalProcessed,
                    tcResult["tcProcessed"].asInt(), tcResult["tcNewlyValid"].asInt(), tcResult["tcStillPending"].asInt(),
                    crlResult["crlChecked"].asInt(), crlResult["crlRevoked"].asInt(), crlResult["crlUnavailable"].asInt());

    } catch (const std::exception& e) {
        spdlog::error("[ValidationService] Revalidation failed: {}", e.what());
        response["success"] = false;
        response["error"] = e.what();
        response["totalProcessed"] = 0;
        response["errors"] = 1;
    }

    return response;
}

} // namespace icao::relay::services
