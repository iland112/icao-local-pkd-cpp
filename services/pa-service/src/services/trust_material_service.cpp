#include "trust_material_service.h"
#include "../repositories/ldap_certificate_repository.h"
#include "../repositories/ldap_crl_repository.h"
#include "../repositories/trust_material_request_repository.h"
#include "../auth/personal_info_crypto.h"
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <algorithm>
#include <cstring>

namespace services {

TrustMaterialService::TrustMaterialService(
    repositories::LdapCertificateRepository* certRepo,
    repositories::LdapCrlRepository* crlRepo,
    repositories::TrustMaterialRequestRepository* requestRepo)
    : certRepo_(certRepo), crlRepo_(crlRepo), requestRepo_(requestRepo)
{
    if (!certRepo_ || !crlRepo_ || !requestRepo_) {
        throw std::invalid_argument("TrustMaterialService: repository pointers cannot be nullptr");
    }
    spdlog::info("[TrustMaterialService] Initialized");
}

std::string TrustMaterialService::base64Encode(const std::vector<uint8_t>& data) {
    BIO* b64 = BIO_new(BIO_f_base64());
    BIO* bmem = BIO_new(BIO_s_mem());
    if (!b64 || !bmem) {
        if (b64) BIO_free(b64);
        if (bmem) BIO_free(bmem);
        return "";
    }
    b64 = BIO_push(b64, bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, data.data(), static_cast<int>(data.size()));
    BIO_flush(b64);

    BUF_MEM* bptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);
    return result;
}

TrustMaterialService::MrzFields TrustMaterialService::parseMrz(const std::string& mrzText) {
    MrzFields fields;
    if (mrzText.empty()) return fields;

    // MRZ TD3 (passport): 2 lines × 44 chars
    // Line 1: P<NATIONALITY... (pos 2-4 = nationality)
    // Line 2: DOCNUMBER... (pos 0-8 = document number)
    auto lines = std::vector<std::string>{};
    std::string line;
    for (char c : mrzText) {
        if (c == '\n' || c == '\r') {
            if (!line.empty()) {
                lines.push_back(line);
                line.clear();
            }
        } else {
            line += c;
        }
    }
    if (!line.empty()) lines.push_back(line);

    if (lines.empty()) return fields;

    // Document type: first 1-2 chars of line 1
    if (lines[0].size() >= 2) {
        fields.documentType = lines[0].substr(0, 1);
        if (lines[0][1] != '<') {
            fields.documentType += lines[0][1];
        }
    }

    // Nationality: chars 2-4 of line 1
    if (lines[0].size() >= 5) {
        std::string nat = lines[0].substr(2, 3);
        // Remove trailing '<'
        while (!nat.empty() && nat.back() == '<') nat.pop_back();
        fields.nationality = nat;
    }

    // Document number: chars 0-8 of line 2
    if (lines.size() >= 2 && lines[1].size() >= 9) {
        std::string docNum = lines[1].substr(0, 9);
        // Remove trailing '<'
        while (!docNum.empty() && docNum.back() == '<') docNum.pop_back();
        fields.documentNumber = docNum;
    }

    return fields;
}

TrustMaterialService::TrustMaterialResponse TrustMaterialService::fetchTrustMaterials(
    const TrustMaterialRequest& request)
{
    auto startTime = std::chrono::high_resolution_clock::now();
    TrustMaterialResponse response;
    response.data = Json::Value();

    // Validate country code (2-3 uppercase letters)
    if (request.countryCode.empty() || request.countryCode.size() > 3) {
        response.errorMessage = "Invalid country code";
        return response;
    }
    for (char c : request.countryCode) {
        if (!std::isupper(static_cast<unsigned char>(c))) {
            response.errorMessage = "Country code must be uppercase letters";
            return response;
        }
    }

    std::string countryCode = request.countryCode.substr(0, 2); // Normalize to alpha-2

    // Decrypt MRZ if provided
    MrzFields mrzFields;
    if (!request.encryptedMrz.empty()) {
        try {
            std::string decryptedMrz = auth::pii::decrypt(request.encryptedMrz);
            if (!decryptedMrz.empty()) {
                mrzFields = parseMrz(decryptedMrz);
                spdlog::debug("[TrustMaterialService] MRZ parsed: nationality={}, docType={}",
                    mrzFields.nationality, mrzFields.documentType);
            }
        } catch (const std::exception& e) {
            spdlog::warn("[TrustMaterialService] MRZ decryption failed (non-critical): {}", e.what());
        }
    }

    // Fetch CSCAs from LDAP
    Json::Value cscaArray(Json::arrayValue);
    Json::Value linkCertArray(Json::arrayValue);
    int cscaCount = 0;
    int linkCertCount = 0;

    try {
        auto cscaCerts = certRepo_->findAllCscasByCountry(countryCode);
        spdlog::info("[TrustMaterialService] Found {} CSCA/LC certs for country {}", cscaCerts.size(), countryCode);

        for (X509* cert : cscaCerts) {
            if (!cert) continue;

            // Extract metadata
            char* subjectStr = X509_NAME_oneline(X509_get_subject_name(cert), nullptr, 0);
            char* issuerStr = X509_NAME_oneline(X509_get_issuer_name(cert), nullptr, 0);
            std::string subjectDn = subjectStr ? subjectStr : "";
            std::string issuerDn = issuerStr ? issuerStr : "";
            if (subjectStr) OPENSSL_free(subjectStr);
            if (issuerStr) OPENSSL_free(issuerStr);

            // Serialize to DER
            int derLen = i2d_X509(cert, nullptr);
            if (derLen <= 0) {
                X509_free(cert);
                continue;
            }
            std::vector<uint8_t> derBytes(derLen);
            unsigned char* p = derBytes.data();
            i2d_X509(cert, &p);

            // Determine if self-signed (root CSCA) or link certificate
            bool isSelfSigned = (X509_check_issued(cert, cert) == X509_V_OK);

            Json::Value certEntry;
            certEntry["subjectDn"] = subjectDn;
            certEntry["issuerDn"] = issuerDn;
            certEntry["derBase64"] = base64Encode(derBytes);

            // Get validity period
            const ASN1_TIME* notBefore = X509_get0_notBefore(cert);
            const ASN1_TIME* notAfter = X509_get0_notAfter(cert);
            if (notBefore) {
                BIO* bio = BIO_new(BIO_s_mem());
                if (bio) {
                    ASN1_TIME_print(bio, notBefore);
                    char buf[128] = {};
                    BIO_read(bio, buf, sizeof(buf) - 1);
                    certEntry["notBefore"] = std::string(buf);
                    BIO_free(bio);
                }
            }
            if (notAfter) {
                BIO* bio = BIO_new(BIO_s_mem());
                if (bio) {
                    ASN1_TIME_print(bio, notAfter);
                    char buf[128] = {};
                    BIO_read(bio, buf, sizeof(buf) - 1);
                    certEntry["notAfter"] = std::string(buf);
                    BIO_free(bio);
                }
            }

            if (isSelfSigned) {
                cscaArray.append(certEntry);
                cscaCount++;
            } else {
                linkCertArray.append(certEntry);
                linkCertCount++;
            }

            X509_free(cert);
        }
    } catch (const std::exception& e) {
        spdlog::error("[TrustMaterialService] CSCA lookup failed: {}", e.what());
        response.errorMessage = "CSCA lookup failed: " + std::string(e.what());
    }

    // Fetch CRL from LDAP
    Json::Value crlArray(Json::arrayValue);
    int crlCount = 0;

    try {
        X509_CRL* crl = crlRepo_->findCrlByCountry(countryCode);
        if (crl) {
            int crlDerLen = i2d_X509_CRL(crl, nullptr);
            if (crlDerLen > 0) {
                std::vector<uint8_t> crlDer(crlDerLen);
                unsigned char* cp = crlDer.data();
                i2d_X509_CRL(crl, &cp);

                Json::Value crlEntry;

                char* issuerStr = X509_NAME_oneline(X509_CRL_get_issuer(crl), nullptr, 0);
                crlEntry["issuerDn"] = issuerStr ? issuerStr : "";
                if (issuerStr) OPENSSL_free(issuerStr);

                const ASN1_TIME* thisUpdate = X509_CRL_get0_lastUpdate(crl);
                const ASN1_TIME* nextUpdate = X509_CRL_get0_nextUpdate(crl);
                if (thisUpdate) {
                    BIO* bio = BIO_new(BIO_s_mem());
                    if (bio) {
                        ASN1_TIME_print(bio, thisUpdate);
                        char buf[128] = {};
                        BIO_read(bio, buf, sizeof(buf) - 1);
                        crlEntry["thisUpdate"] = std::string(buf);
                        BIO_free(bio);
                    }
                }
                if (nextUpdate) {
                    BIO* bio = BIO_new(BIO_s_mem());
                    if (bio) {
                        ASN1_TIME_print(bio, nextUpdate);
                        char buf[128] = {};
                        BIO_read(bio, buf, sizeof(buf) - 1);
                        crlEntry["nextUpdate"] = std::string(buf);
                        BIO_free(bio);
                    }
                }

                crlEntry["derBase64"] = base64Encode(crlDer);
                crlArray.append(crlEntry);
                crlCount++;
            }
            X509_CRL_free(crl);
        }
    } catch (const std::exception& e) {
        spdlog::warn("[TrustMaterialService] CRL lookup failed (non-critical): {}", e.what());
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    int processingMs = static_cast<int>(
        std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count());

    // Save audit record
    try {
        repositories::TrustMaterialRequestRepository::RequestRecord record;
        record.countryCode = countryCode;
        record.dscIssuerDn = request.dscIssuerDn;
        record.mrzNationality = mrzFields.nationality;
        record.mrzDocumentType = mrzFields.documentType;
        record.mrzDocumentNumber = mrzFields.documentNumber;
        record.cscaCount = cscaCount;
        record.linkCertCount = linkCertCount;
        record.crlCount = crlCount;
        record.clientIp = request.clientIp;
        record.userAgent = request.userAgent;
        record.requestedBy = request.requestedBy;
        record.apiClientId = request.apiClientId;
        record.processingTimeMs = processingMs;
        record.status = (cscaCount > 0) ? "SUCCESS" : "NOT_FOUND";
        requestRepo_->insert(record);
    } catch (const std::exception& e) {
        spdlog::warn("[TrustMaterialService] Audit record insert failed (non-critical): {}", e.what());
    }

    // Build response
    response.data["countryCode"] = countryCode;
    response.data["csca"] = cscaArray;
    response.data["linkCertificates"] = linkCertArray;
    response.data["crl"] = crlArray;

    // Add current timestamp
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    char timeBuf[64];
    struct tm tmBuf;
    gmtime_r(&tt, &tmBuf);
    strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%dT%H:%M:%SZ", &tmBuf);
    response.data["timestamp"] = std::string(timeBuf);
    response.data["processingTimeMs"] = processingMs;

    response.success = (cscaCount > 0 || crlCount > 0);
    if (!response.success && response.errorMessage.empty()) {
        response.errorMessage = "No trust materials found for country: " + countryCode;
    }

    spdlog::info("[TrustMaterialService] Response: country={}, csca={}, linkCert={}, crl={}, {}ms",
        countryCode, cscaCount, linkCertCount, crlCount, processingMs);

    return response;
}

} // namespace services
