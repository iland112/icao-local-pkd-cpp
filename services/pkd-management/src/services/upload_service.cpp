/** @file upload_service.cpp
 *  @brief UploadService implementation
 */

#include "upload_service.h"
#include "../infrastructure/service_container.h"
#include "../repositories/crl_repository.h"
#include "../repositories/deviation_list_repository.h"
#include <spdlog/spdlog.h>
#include <uuid/uuid.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <thread>
#include <cstdlib>
#include <optional>
#include <memory>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/pem.h>

// Certificate parsing libraries
#include "file_detector.h"
#include "pem_parser.h"
#include "der_parser.h"
#include "cert_type_detector.h"
#include <icao/x509/certificate_parser.h>
#include <dl_parser.h>

// Doc 9303 compliance checklist
#include "../common/doc9303_checklist.h"

// processLdifFileAsync() moved to UploadHandler (Phase D3)
// Called via g_services->uploadHandler()->processLdifFileAsync()

// Utility functions (moved to main_utils.cpp)
#include "../common/main_utils.h"

// LDAP save functions (v2.13.0: migrated to LdapStorageService)
#include "ldap_storage_service.h"

// Global service container (defined in main.cpp)
extern infrastructure::ServiceContainer* g_services;

// Certificate save utility
#include "../common/certificate_utils.h"

namespace services {

using namespace icao::certificate_parser;

/**
 * @brief Parse collection number from ICAO filename
 * e.g., "icaopkd-001-complete-009667.ldif" → "001"
 *       "ICAO_ml_December2025.ml" → "ML"
 */
static std::string parseCollectionNumber(const std::string& fileName) {
    // Match "icaopkd-NNN-" pattern
    auto pos = fileName.find("icaopkd-");
    if (pos != std::string::npos && fileName.size() >= pos + 11) {
        std::string num = fileName.substr(pos + 8, 3);
        if (num.size() == 3 && std::isdigit(num[0]) && std::isdigit(num[1]) && std::isdigit(num[2])) {
            return num;
        }
    }
    // Master List files
    if (fileName.find("ICAO_ml") != std::string::npos || fileName.find(".ml") != std::string::npos) {
        return "ML";
    }
    return "";
}

// --- Constructor ---

UploadService::UploadService(
    repositories::UploadRepository* uploadRepo,
    repositories::CertificateRepository* certRepo,
    common::LdapConnectionPool* ldapPool,
    repositories::DeviationListRepository* dlRepo
)
    : uploadRepo_(uploadRepo)
    , certRepo_(certRepo)
    , ldapPool_(ldapPool)
    , dlRepo_(dlRepo)
{
    if (!uploadRepo_) {
        throw std::invalid_argument("UploadService: uploadRepo cannot be nullptr");
    }
    if (!certRepo_) {
        throw std::invalid_argument("UploadService: certRepo cannot be nullptr");
    }
    spdlog::info("UploadService initialized with Repository dependencies{}",
                dlRepo_ ? " (DL support enabled)" : "");
}

// --- Public Methods - Individual Certificate Upload ---

UploadService::CertificateUploadResult UploadService::uploadCertificate(
    const std::string& fileName,
    const std::vector<uint8_t>& fileContent,
    const std::string& uploadedBy
)
{
    spdlog::info("[UploadService] uploadCertificate - fileName: {}, size: {} bytes", fileName, fileContent.size());

    CertificateUploadResult result;
    result.success = false;
    result.status = "PENDING";

    try {
        // Step 1: Compute file hash and check duplicate
        std::string fileHash = computeFileHash(fileContent);
        auto duplicateUpload = uploadRepo_->findByFileHash(fileHash);
        if (duplicateUpload) {
            result.success = false;
            result.status = "DUPLICATE";
            result.uploadId = duplicateUpload->id;
            result.errorMessage = "Duplicate file detected. This file has already been uploaded.";
            result.message = "File with hash " + fileHash.substr(0, 16) + "... already exists";
            return result;
        }

        // Step 2: Detect file format
        FileFormat format = FileDetector::detectFormat(fileName, fileContent);
        result.fileFormat = FileDetector::formatToString(format);

        if (format == FileFormat::UNKNOWN || format == FileFormat::LDIF ||
            format == FileFormat::ML || format == FileFormat::BIN) {
            result.status = "FAILED";
            result.errorMessage = "Unsupported file format for certificate upload. Use LDIF or Master List upload for " + result.fileFormat + " files.";
            return result;
        }

        spdlog::info("[UploadService] Detected format: {} for file: {}", result.fileFormat, fileName);

        // Step 3: Create upload record
        result.uploadId = generateUploadId();
        repositories::Upload upload;
        upload.id = result.uploadId;
        upload.fileName = fileName;
        upload.originalFileName = fileName;
        upload.collectionNumber = parseCollectionNumber(fileName);
        upload.fileHash = fileHash;
        upload.fileFormat = result.fileFormat;
        upload.fileSize = fileContent.size();
        upload.status = "PROCESSING";
        upload.uploadedBy = uploadedBy;

        if (!uploadRepo_->insert(upload)) {
            throw std::runtime_error("Failed to insert upload record");
        }

        // Step 4: Get LDAP connection (optional, RAII - auto-released on scope exit)
        LDAP* ld = nullptr;
        std::optional<common::LdapConnection> ldapConn;
        if (ldapPool_) {
            try {
                ldapConn.emplace(ldapPool_->acquire());
                if (ldapConn->isValid()) ld = ldapConn->get();
            } catch (const std::exception& e) {
                spdlog::warn("[UploadService] Could not acquire LDAP connection: {}", e.what());
            }
        }

        // Step 5: Parse and process based on format
        if (format == FileFormat::CRL) {
            processCrlFile(result, fileContent, ld);
        } else {
            // PEM, DER, CER, P7B, DL → certificate processing
            std::vector<X509*> certs;

            if (format == FileFormat::PEM) {
                auto pemResult = PemParser::parse(fileContent);
                if (pemResult.success) {
                    // Transfer ownership
                    for (auto* cert : pemResult.certificates) {
                        certs.push_back(X509_dup(cert));
                    }
                } else {
                    throw std::runtime_error("PEM parsing failed: " + pemResult.errorMessage);
                }
            } else if (format == FileFormat::DER || format == FileFormat::CER) {
                auto derResult = DerParser::parse(fileContent);
                if (derResult.success && derResult.certificate) {
                    certs.push_back(X509_dup(derResult.certificate));
                } else {
                    throw std::runtime_error("DER parsing failed: " + derResult.errorMessage);
                }
            } else if (format == FileFormat::DL) {
                // DL: Use DlParser for full deviation extraction + certificate processing
                processDlFile(result, fileContent, ld);
            } else if (format == FileFormat::P7B) {
                certs = icao::x509::extractCertificatesFromCms(fileContent);
                if (certs.empty()) {
                    throw std::runtime_error("P7B parsing failed: no certificates found in CMS SignedData");
                }
            }

            if (!certs.empty()) {
                spdlog::info("[UploadService] Parsed {} certificates from {} file", certs.size(), result.fileFormat);

                // Process each certificate (with exception-safe cleanup)
                for (size_t i = 0; i < certs.size(); ++i) {
                    try {
                        processSingleCertificate(result, certs[i], fileContent, ld);
                    } catch (...) {
                        // Free current and remaining certs before re-throwing
                        for (size_t j = i; j < certs.size(); ++j) X509_free(certs[j]);
                        certs.clear();
                        throw;
                    }
                    X509_free(certs[i]);
                }
                certs.clear();
            }
        }

        // Step 6: Update upload statistics
        result.certificateCount = result.cscaCount + result.dscCount + result.dscNcCount + result.mlscCount;
        uploadRepo_->updateStatistics(result.uploadId, result.cscaCount, result.dscCount,
                                       result.dscNcCount, result.crlCount, result.mlscCount, 0);
        uploadRepo_->updateStatus(result.uploadId, "COMPLETED", "");

        result.success = true;
        result.status = "COMPLETED";
        int totalProcessed = result.certificateCount + result.crlCount;
        result.message = "Processed " + std::to_string(totalProcessed) + " item(s) from " + result.fileFormat + " file";

        spdlog::info("[UploadService] Certificate upload completed: {} certs, {} CRLs, {} duplicates, {} LDAP stored",
                    result.certificateCount, result.crlCount, result.duplicateCount, result.ldapStoredCount);

    } catch (const std::exception& e) {
        spdlog::error("[UploadService] uploadCertificate failed: {}", e.what());
        result.success = false;
        result.status = "FAILED";
        result.errorMessage = e.what();

        if (!result.uploadId.empty()) {
            uploadRepo_->updateStatus(result.uploadId, "FAILED", e.what());
        }
    }

    return result;
}

// --- Public Methods - Certificate Preview (parse only, no save) ---

UploadService::CertificatePreviewResult UploadService::previewCertificate(
    const std::string& fileName,
    const std::vector<uint8_t>& fileContent
)
{
    spdlog::info("[UploadService] previewCertificate - fileName: {}, size: {} bytes", fileName, fileContent.size());

    CertificatePreviewResult result;

    try {
        // Step 1: Compute file hash and check duplicate
        std::string fileHash = computeFileHash(fileContent);
        auto duplicateUpload = uploadRepo_->findByFileHash(fileHash);
        if (duplicateUpload) {
            result.isDuplicate = true;
            result.duplicateUploadId = duplicateUpload->id;
            // Continue with preview — just warn about duplicate
        }

        // Step 2: Detect file format
        FileFormat format = FileDetector::detectFormat(fileName, fileContent);
        result.fileFormat = FileDetector::formatToString(format);

        if (format == FileFormat::UNKNOWN || format == FileFormat::LDIF ||
            format == FileFormat::ML || format == FileFormat::BIN) {
            result.errorMessage = "Unsupported file format for certificate upload. Use LDIF or Master List upload for " + result.fileFormat + " files.";
            return result;
        }

        // Step 3: Parse based on format
        if (format == FileFormat::CRL) {
            // CRL preview
            const uint8_t* data = fileContent.data();
            X509_CRL* crl = d2i_X509_CRL(nullptr, &data, static_cast<long>(fileContent.size()));
            if (!crl) {
                BIO* bio = BIO_new_mem_buf(fileContent.data(), static_cast<int>(fileContent.size()));
                if (bio) {
                    crl = PEM_read_bio_X509_CRL(bio, nullptr, nullptr, nullptr);
                    BIO_free(bio);
                }
            }
            if (!crl) {
                result.errorMessage = "Failed to parse CRL file (neither DER nor PEM format)";
                return result;
            }

            CrlPreviewItem crlItem;
            crlItem.issuerDn = x509NameToString(X509_CRL_get_issuer(crl));
            crlItem.countryCode = extractCountryCode(crlItem.issuerDn);
            crlItem.thisUpdate = asn1TimeToIso8601(X509_CRL_get0_lastUpdate(crl));
            if (X509_CRL_get0_nextUpdate(crl)) {
                crlItem.nextUpdate = asn1TimeToIso8601(X509_CRL_get0_nextUpdate(crl));
            }
            ASN1_INTEGER* crlNumAsn1 = static_cast<ASN1_INTEGER*>(
                X509_CRL_get_ext_d2i(crl, NID_crl_number, nullptr, nullptr));
            if (crlNumAsn1) {
                crlItem.crlNumber = asn1IntegerToHex(crlNumAsn1);
                ASN1_INTEGER_free(crlNumAsn1);
            }
            STACK_OF(X509_REVOKED)* revokedStack = X509_CRL_get_REVOKED(crl);
            crlItem.revokedCount = revokedStack ? sk_X509_REVOKED_num(revokedStack) : 0;

            result.crlInfo = crlItem;
            result.hasCrlInfo = true;
            X509_CRL_free(crl);

        } else if (format == FileFormat::DL) {
            // DL preview — parse with DlParser
            using namespace icao::certificate_parser;
            DlParseResult dlResult = DlParser::parse(fileContent);

            if (dlResult.success) {
                result.dlIssuerCountry = dlResult.issuerCountry;
                result.dlVersion = dlResult.version;
                result.dlHashAlgorithm = dlResult.hashAlgorithm;
                result.dlSignatureValid = dlResult.signatureVerified;
                result.dlSigningTime = dlResult.signingTime;
                result.dlEContentType = dlResult.eContentType;
                result.dlCmsDigestAlgorithm = dlResult.cmsDigestAlgorithm;
                result.dlCmsSignatureAlgorithm = dlResult.cmsSignatureAlgorithm;
                if (dlResult.signerCertificate) {
                    result.dlSignerDn = x509NameToString(X509_get_subject_name(dlResult.signerCertificate));
                }

                // Extract certificate previews
                for (auto* cert : dlResult.certificates) {
                    CertificatePreviewItem item;
                    item.subjectDn = x509NameToString(X509_get_subject_name(cert));
                    item.issuerDn = x509NameToString(X509_get_issuer_name(cert));
                    item.serialNumber = asn1IntegerToHex(X509_get_serialNumber(cert));
                    item.countryCode = extractCountryCode(item.subjectDn);
                    if (item.countryCode == "XX") item.countryCode = extractCountryCode(item.issuerDn);
                    item.notBefore = asn1TimeToIso8601(X509_get0_notBefore(cert));
                    item.notAfter = asn1TimeToIso8601(X509_get0_notAfter(cert));

                    auto certInfo = CertTypeDetector::detectType(cert);
                    switch (certInfo.type) {
                        case CertificateType::CSCA:
                        case CertificateType::LINK_CERT:
                            item.certificateType = "CSCA";
                            break;
                        case CertificateType::DSC:
                            item.certificateType = "DSC";
                            break;
                        case CertificateType::DSC_NC:
                            item.certificateType = "DSC_NC";
                            break;
                        case CertificateType::MLSC:
                            item.certificateType = "MLSC";
                            break;
                        default:
                            item.certificateType = "DSC";
                            break;
                    }
                    item.isSelfSigned = certInfo.is_self_signed;
                    item.isLinkCertificate = (certInfo.type == CertificateType::LINK_CERT);

                    // Check expiration
                    if (X509_cmp_current_time(X509_get0_notAfter(cert)) < 0) {
                        item.isExpired = true;
                    }

                    // Key info
                    EVP_PKEY* pkey = X509_get0_pubkey(cert);
                    if (pkey) {
                        item.keySize = EVP_PKEY_bits(pkey);
                        int pkeyId = EVP_PKEY_id(pkey);
                        if (pkeyId == EVP_PKEY_RSA) item.publicKeyAlgorithm = "RSA";
                        else if (pkeyId == EVP_PKEY_EC) item.publicKeyAlgorithm = "EC";
                        else item.publicKeyAlgorithm = "Unknown";
                    }

                    // Signature algorithm
                    int sigNid = X509_get_signature_nid(cert);
                    item.signatureAlgorithm = OBJ_nid2sn(sigNid);

                    // Fingerprint
                    unsigned char* derBuf = nullptr;
                    int derLen = i2d_X509(cert, &derBuf);
                    if (derLen > 0 && derBuf) {
                        std::vector<uint8_t> derBytes(derBuf, derBuf + derLen);
                        OPENSSL_free(derBuf);
                        item.fingerprintSha256 = computeFileHash(derBytes);
                    }

                    result.certificates.push_back(item);
                }

                // Extract deviation previews
                for (const auto& dev : dlResult.deviations) {
                    DeviationPreviewItem devItem;
                    devItem.certificateIssuerDn = dev.certificateIssuerDn;
                    devItem.certificateSerialNumber = dev.certificateSerialNumber;
                    devItem.defectDescription = dev.defectDescription;
                    devItem.defectTypeOid = dev.defectTypeOid;
                    devItem.defectCategory = dev.defectCategory;
                    result.deviations.push_back(devItem);
                }
            } else {
                // Fallback: try extracting certificates from CMS
                auto certs = icao::x509::extractCertificatesFromCms(fileContent);
                for (auto* cert : certs) {
                    CertificatePreviewItem item;
                    item.subjectDn = x509NameToString(X509_get_subject_name(cert));
                    item.issuerDn = x509NameToString(X509_get_issuer_name(cert));
                    item.serialNumber = asn1IntegerToHex(X509_get_serialNumber(cert));
                    item.countryCode = extractCountryCode(item.subjectDn);
                    if (item.countryCode == "XX") item.countryCode = extractCountryCode(item.issuerDn);
                    auto certInfo = CertTypeDetector::detectType(cert);
                    switch (certInfo.type) {
                        case CertificateType::CSCA:
                        case CertificateType::LINK_CERT: item.certificateType = "CSCA"; break;
                        case CertificateType::DSC: item.certificateType = "DSC"; break;
                        case CertificateType::DSC_NC: item.certificateType = "DSC_NC"; break;
                        case CertificateType::MLSC: item.certificateType = "MLSC"; break;
                        default: item.certificateType = "DSC"; break;
                    }
                    result.certificates.push_back(item);
                    X509_free(cert);
                }
                result.message = "DL parsing failed, extracted certificates only: " + dlResult.errorMessage;
            }

        } else {
            // PEM, DER, CER, P7B — certificate preview
            std::vector<X509*> certs;

            if (format == FileFormat::PEM) {
                auto pemResult = PemParser::parse(fileContent);
                if (pemResult.success) {
                    for (auto* cert : pemResult.certificates) {
                        certs.push_back(X509_dup(cert));
                    }
                } else {
                    result.errorMessage = "PEM parsing failed: " + pemResult.errorMessage;
                    return result;
                }
            } else if (format == FileFormat::DER || format == FileFormat::CER) {
                auto derResult = DerParser::parse(fileContent);
                if (derResult.success && derResult.certificate) {
                    certs.push_back(X509_dup(derResult.certificate));
                } else {
                    result.errorMessage = "DER parsing failed: " + derResult.errorMessage;
                    return result;
                }
            } else if (format == FileFormat::P7B) {
                certs = icao::x509::extractCertificatesFromCms(fileContent);
                if (certs.empty()) {
                    result.errorMessage = "P7B parsing failed: no certificates found in CMS SignedData";
                    return result;
                }
            }

            // Extract metadata from each certificate (with exception-safe cleanup)
            for (size_t i = 0; i < certs.size(); ++i) {
                auto* cert = certs[i];
                try {
                    CertificatePreviewItem item;
                    item.subjectDn = x509NameToString(X509_get_subject_name(cert));
                    item.issuerDn = x509NameToString(X509_get_issuer_name(cert));
                    item.serialNumber = asn1IntegerToHex(X509_get_serialNumber(cert));
                    item.countryCode = extractCountryCode(item.subjectDn);
                    if (item.countryCode == "XX") item.countryCode = extractCountryCode(item.issuerDn);
                    item.notBefore = asn1TimeToIso8601(X509_get0_notBefore(cert));
                    item.notAfter = asn1TimeToIso8601(X509_get0_notAfter(cert));

                    auto certInfo = CertTypeDetector::detectType(cert);
                    switch (certInfo.type) {
                        case CertificateType::CSCA:
                        case CertificateType::LINK_CERT:
                            item.certificateType = "CSCA";
                            break;
                        case CertificateType::DSC:
                            item.certificateType = "DSC";
                            break;
                        case CertificateType::DSC_NC:
                            item.certificateType = "DSC_NC";
                            break;
                        case CertificateType::MLSC:
                            item.certificateType = "MLSC";
                            break;
                        default:
                            item.certificateType = "DSC";
                            break;
                    }
                    item.isSelfSigned = certInfo.is_self_signed;
                    item.isLinkCertificate = (certInfo.type == CertificateType::LINK_CERT);

                    // Check expiration
                    if (X509_cmp_current_time(X509_get0_notAfter(cert)) < 0) {
                        item.isExpired = true;
                    }

                    // Key info
                    EVP_PKEY* pkey = X509_get0_pubkey(cert);
                    if (pkey) {
                        item.keySize = EVP_PKEY_bits(pkey);
                        int pkeyId = EVP_PKEY_id(pkey);
                        if (pkeyId == EVP_PKEY_RSA) item.publicKeyAlgorithm = "RSA";
                        else if (pkeyId == EVP_PKEY_EC) item.publicKeyAlgorithm = "EC";
                        else item.publicKeyAlgorithm = "Unknown";
                    }

                    // Signature algorithm
                    int sigNid = X509_get_signature_nid(cert);
                    item.signatureAlgorithm = OBJ_nid2sn(sigNid);

                    // Fingerprint
                    unsigned char* derBuf = nullptr;
                    int derLen = i2d_X509(cert, &derBuf);
                    if (derLen > 0 && derBuf) {
                        std::vector<uint8_t> derBytes(derBuf, derBuf + derLen);
                        OPENSSL_free(derBuf);
                        item.fingerprintSha256 = computeFileHash(derBytes);
                    }

                    // Doc 9303 compliance checklist
                    item.doc9303Checklist = common::runDoc9303Checklist(cert, item.certificateType);

                    result.certificates.push_back(item);
                } catch (...) {
                    // Free current and remaining certs before re-throwing
                    for (size_t j = i; j < certs.size(); ++j) X509_free(certs[j]);
                    certs.clear();
                    throw;
                }
                X509_free(cert);
            }
        }

        result.success = true;
        int totalItems = static_cast<int>(result.certificates.size()) + (result.hasCrlInfo ? 1 : 0);
        result.message = "Parsed " + std::to_string(totalItems) + " item(s) from " + result.fileFormat + " file";

        spdlog::info("[UploadService] Certificate preview: format={}, certs={}, deviations={}, hasCrl={}, duplicate={}",
                    result.fileFormat, result.certificates.size(), result.deviations.size(),
                    result.hasCrlInfo, result.isDuplicate);

    } catch (const std::exception& e) {
        spdlog::error("[UploadService] previewCertificate failed: {}", e.what());
        result.errorMessage = e.what();
    }

    return result;
}

// LDIF/ML upload, stats, history methods removed — moved to pkd-relay (v2.41.0)

// --- Private Helper Methods ---

std::string UploadService::generateUploadId()
{
    uuid_t uuid;
    char uuidStr[37];
    uuid_generate(uuid);
    uuid_unparse_lower(uuid, uuidStr);
    return std::string(uuidStr);
}

std::string UploadService::computeFileHash(const std::vector<uint8_t>& content)
{
    unsigned char hash[32];
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        spdlog::error("EVP_MD_CTX_new() allocation failed in computeFileHash");
        return "";
    }
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, content.data(), content.size());
    unsigned int len = 0;
    EVP_DigestFinal_ex(ctx, hash, &len);
    EVP_MD_CTX_free(ctx);

    std::stringstream ss;
    for (unsigned int i = 0; i < len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return ss.str();
}

// --- Private Helpers - Individual Certificate Processing ---

void UploadService::processSingleCertificate(CertificateUploadResult& result, X509* cert,
                                              const std::vector<uint8_t>& rawContent, LDAP* ld)
{
    // Extract certificate metadata
    std::string subjectDn = x509NameToString(X509_get_subject_name(cert));
    std::string issuerDn = x509NameToString(X509_get_issuer_name(cert));
    std::string serialNumber = asn1IntegerToHex(X509_get_serialNumber(cert));
    std::string notBefore = asn1TimeToIso8601(X509_get0_notBefore(cert));
    std::string notAfter = asn1TimeToIso8601(X509_get0_notAfter(cert));

    // Get DER encoding of certificate
    unsigned char* derBuf = nullptr;
    int derLen = i2d_X509(cert, &derBuf);
    if (derLen <= 0 || !derBuf) {
        spdlog::error("[UploadService] Failed to encode certificate to DER");
        return;
    }
    std::vector<uint8_t> derBytes(derBuf, derBuf + derLen);
    OPENSSL_free(derBuf);

    std::string fingerprint = computeFileHash(derBytes);
    std::string countryCode = extractCountryCode(subjectDn);
    if (countryCode == "XX") {
        countryCode = extractCountryCode(issuerDn);
    }

    // Detect certificate type using CertTypeDetector
    auto certInfo = CertTypeDetector::detectType(cert);
    std::string certType;

    switch (certInfo.type) {
        case CertificateType::CSCA:
        case CertificateType::LINK_CERT:
            certType = "CSCA";
            break;
        case CertificateType::DSC:
            certType = "DSC";
            break;
        case CertificateType::DSC_NC:
            certType = "DSC_NC";
            break;
        case CertificateType::MLSC:
            certType = "MLSC";
            break;
        default:
            certType = "DSC";  // Default to DSC for unknown types
            break;
    }

    spdlog::info("[UploadService] Certificate: type={}, country={}, fingerprint={}...",
                certType, countryCode, fingerprint.substr(0, 16));

    // Save to DB with duplicate check
    auto [certId, isDuplicate] = certificate_utils::saveCertificateWithDuplicateCheck(
        result.uploadId, certType, countryCode,
        subjectDn, issuerDn, serialNumber, fingerprint,
        notBefore, notAfter, derBytes,
        "UNKNOWN", "", nullptr,  // Validation will be done separately
        "FILE_UPLOAD"
    );

    if (certId.empty()) {
        spdlog::warn("[UploadService] Failed to save certificate to DB");
        return;
    }

    if (isDuplicate) {
        result.duplicateCount++;
    }

    // Update counts
    if (certType == "CSCA") result.cscaCount++;
    else if (certType == "DSC") result.dscCount++;
    else if (certType == "DSC_NC") result.dscNcCount++;
    else if (certType == "MLSC") result.mlscCount++;

    // Save to LDAP
    if (ld) {
        std::string ldapCertType = certType;
        if (certType == "CSCA" && certInfo.type == CertificateType::LINK_CERT) {
            ldapCertType = "LC";
        }

        std::string ldapDn = g_services->ldapStorageService()->saveCertificateToLdap(
                                                    ld, ldapCertType, countryCode,
                                                    subjectDn, issuerDn, serialNumber,
                                                    fingerprint, derBytes, "", "", "", false);
        if (!ldapDn.empty()) {
            certRepo_->updateCertificateLdapStatus(certId, ldapDn);
            result.ldapStoredCount++;
        }
    }
}

void UploadService::processCrlFile(CertificateUploadResult& result,
                                    const std::vector<uint8_t>& fileContent, LDAP* ld)
{
    // Try DER format first
    const uint8_t* data = fileContent.data();
    X509_CRL* crl = d2i_X509_CRL(nullptr, &data, static_cast<long>(fileContent.size()));

    // If DER fails, try PEM format
    if (!crl) {
        BIO* bio = BIO_new_mem_buf(fileContent.data(), static_cast<int>(fileContent.size()));
        if (bio) {
            crl = PEM_read_bio_X509_CRL(bio, nullptr, nullptr, nullptr);
            BIO_free(bio);
        }
    }

    if (!crl) {
        throw std::runtime_error("Failed to parse CRL file (neither DER nor PEM format)");
    }

    // RAII guard: ensure X509_CRL is freed on all paths (including exceptions)
    auto crlGuard = std::unique_ptr<X509_CRL, decltype(&X509_CRL_free)>(crl, X509_CRL_free);

    // Extract CRL metadata
    std::string issuerDn = x509NameToString(X509_CRL_get_issuer(crl));
    std::string thisUpdate = asn1TimeToIso8601(X509_CRL_get0_lastUpdate(crl));
    std::string nextUpdate;
    if (X509_CRL_get0_nextUpdate(crl)) {
        nextUpdate = asn1TimeToIso8601(X509_CRL_get0_nextUpdate(crl));
    }

    std::string crlNumber;
    ASN1_INTEGER* crlNumAsn1 = static_cast<ASN1_INTEGER*>(
        X509_CRL_get_ext_d2i(crl, NID_crl_number, nullptr, nullptr));
    if (crlNumAsn1) {
        crlNumber = asn1IntegerToHex(crlNumAsn1);
        ASN1_INTEGER_free(crlNumAsn1);
    }

    // Get DER encoding
    unsigned char* derBuf = nullptr;
    int derLen = i2d_X509_CRL(crl, &derBuf);
    std::vector<uint8_t> derBytes;
    if (derLen > 0 && derBuf) {
        derBytes.assign(derBuf, derBuf + derLen);
        OPENSSL_free(derBuf);
    } else {
        derBytes = fileContent;  // Use original content if re-encoding fails
    }

    std::string fingerprint = computeFileHash(derBytes);
    std::string countryCode = extractCountryCode(issuerDn);

    spdlog::info("[UploadService] CRL: issuer={}, country={}, thisUpdate={}, entries={}",
                issuerDn.substr(0, 60), countryCode, thisUpdate,
                X509_CRL_get_REVOKED(crl) ? sk_X509_REVOKED_num(X509_CRL_get_REVOKED(crl)) : 0);

    // Save to DB via CrlRepository
    std::string crlId = g_services->crlRepository()->save(result.uploadId, countryCode, issuerDn,
                                               thisUpdate, nextUpdate, crlNumber, fingerprint, derBytes);

    if (!crlId.empty()) {
        result.crlCount++;

        // Save revoked certificates
        STACK_OF(X509_REVOKED)* revokedStack = X509_CRL_get_REVOKED(crl);
        if (revokedStack) {
            int revokedCount = sk_X509_REVOKED_num(revokedStack);
            for (int i = 0; i < revokedCount; i++) {
                X509_REVOKED* revoked = sk_X509_REVOKED_value(revokedStack, i);
                if (revoked) {
                    std::string serialNum = asn1IntegerToHex(X509_REVOKED_get0_serialNumber(revoked));
                    std::string revDate = asn1TimeToIso8601(X509_REVOKED_get0_revocationDate(revoked));
                    std::string reason = "unspecified";

                    ASN1_ENUMERATED* reasonEnum = static_cast<ASN1_ENUMERATED*>(
                        X509_REVOKED_get_ext_d2i(revoked, NID_crl_reason, nullptr, nullptr));
                    if (reasonEnum) {
                        long reasonCode = ASN1_ENUMERATED_get(reasonEnum);
                        switch (reasonCode) {
                            case 1: reason = "keyCompromise"; break;
                            case 2: reason = "cACompromise"; break;
                            case 3: reason = "affiliationChanged"; break;
                            case 4: reason = "superseded"; break;
                            case 5: reason = "cessationOfOperation"; break;
                            case 6: reason = "certificateHold"; break;
                        }
                        ASN1_ENUMERATED_free(reasonEnum);
                    }
                    g_services->crlRepository()->saveRevokedCertificate(crlId, serialNum, revDate, reason);
                }
            }
        }

        // Save to LDAP
        if (ld) {
            std::string ldapDn = g_services->ldapStorageService()->saveCrlToLdap(ld, countryCode, issuerDn, fingerprint, derBytes);
            if (!ldapDn.empty()) {
                g_services->crlRepository()->updateLdapStatus(crlId, ldapDn);
                result.ldapStoredCount++;
            }
        }
    }

    // crlGuard RAII handles X509_CRL_free(crl) automatically
}

// --- Private Helpers - DL (Deviation List) Processing ---

void UploadService::processDlFile(CertificateUploadResult& result,
                                   const std::vector<uint8_t>& fileContent, LDAP* ld)
{
    using namespace icao::certificate_parser;

    spdlog::info("[UploadService] Processing DL file with DlParser ({} bytes)", fileContent.size());

    // Step 1: Parse DL using dedicated parser (CMS API + ASN.1 deviation extraction)
    DlParseResult dlResult = DlParser::parse(fileContent);

    if (!dlResult.success) {
        spdlog::warn("[UploadService] DlParser failed: {}, falling back to CMS certificate extraction",
                    dlResult.errorMessage);
        // Fallback: extract certificates only (same as P7B)
        auto certs = icao::x509::extractCertificatesFromCms(fileContent);
        if (certs.empty()) {
            throw std::runtime_error("DL parsing failed: " + dlResult.errorMessage);
        }
        for (auto* cert : certs) {
            processSingleCertificate(result, cert, fileContent, ld);
            X509_free(cert);
        }
        return;
    }

    spdlog::info("[UploadService] DL parsed: country={}, version={}, deviations={}, certs={}",
                dlResult.issuerCountry, dlResult.version,
                dlResult.deviations.size(), dlResult.certificates.size());

    // Step 2: Process embedded certificates from CMS wrapper (signer cert + CSCA)
    for (auto* cert : dlResult.certificates) {
        processSingleCertificate(result, cert, fileContent, ld);
    }

    // Step 3: Save DL metadata and deviation entries to DB
    if (dlRepo_) {
        try {
            // Compute DL fingerprint
            std::string fingerprint = computeFileHash(fileContent);

            // Get signer cert ID if available
            std::string signerDn;
            std::string signerCertId;
            if (dlResult.signerCertificate) {
                signerDn = x509NameToString(X509_get_subject_name(dlResult.signerCertificate));
            }

            // Save DL record
            std::string dlId = dlRepo_->save(
                result.uploadId,
                dlResult.issuerCountry,
                dlResult.version,
                dlResult.hashAlgorithm,
                dlResult.signingTime,
                fileContent,
                fingerprint,
                signerDn,
                signerCertId,  // empty - no FK lookup for now
                dlResult.signatureVerified,
                static_cast<int>(dlResult.deviations.size())
            );

            if (!dlId.empty()) {
                spdlog::info("[UploadService] DL saved to DB: id={}, country={}",
                            dlId.substr(0, 8), dlResult.issuerCountry);

                // Save each deviation entry
                for (const auto& deviation : dlResult.deviations) {
                    std::string entryId = dlRepo_->saveDeviationEntry(dlId, deviation);
                    if (!entryId.empty()) {
                        spdlog::debug("[UploadService] Deviation entry saved: oid={}, desc={}",
                                     deviation.defectTypeOid,
                                     deviation.defectDescription.substr(0, 50));
                    }
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("[UploadService] DL metadata save failed (non-fatal): {}", e.what());
        }
    } else {
        spdlog::warn("[UploadService] DL repository not available, skipping deviation data storage");
    }
}

} // namespace services
