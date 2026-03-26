#pragma once

/**
 * @file upload_service.h
 * @brief Upload Service — Individual certificate upload business logic
 *
 * LDIF/ML upload moved to pkd-relay (v2.41.0).
 * Remaining: individual certificate upload (PEM, DER, P7B, DL, CRL) and preview.
 */

#include <string>
#include <vector>
#include <ldap.h>
#include <json/json.h>
#include "../common/doc9303_checklist.h"
#include "../repositories/upload_repository.h"
#include "../repositories/certificate_repository.h"
#include <ldap_connection_pool.h>

namespace repositories { class DeviationListRepository; }

namespace services {

class UploadService {
public:
    UploadService(
        repositories::UploadRepository* uploadRepo,
        repositories::CertificateRepository* certRepo,
        common::LdapConnectionPool* ldapPool,
        repositories::DeviationListRepository* dlRepo = nullptr
    );

    ~UploadService() = default;

    // --- Certificate Preview (parse only, no DB/LDAP save) ---

    struct CertificatePreviewItem {
        std::string subjectDn;
        std::string issuerDn;
        std::string serialNumber;
        std::string countryCode;
        std::string certificateType;
        bool isSelfSigned = false;
        bool isLinkCertificate = false;
        std::string notBefore;
        std::string notAfter;
        bool isExpired = false;
        std::string signatureAlgorithm;
        std::string publicKeyAlgorithm;
        int keySize = 0;
        std::string fingerprintSha256;
        common::Doc9303ChecklistResult doc9303Checklist;
    };

    struct DeviationPreviewItem {
        std::string certificateIssuerDn;
        std::string certificateSerialNumber;
        std::string defectDescription;
        std::string defectTypeOid;
        std::string defectCategory;
    };

    struct CrlPreviewItem {
        std::string issuerDn;
        std::string countryCode;
        std::string thisUpdate;
        std::string nextUpdate;
        std::string crlNumber;
        int revokedCount = 0;
    };

    struct CertificatePreviewResult {
        bool success = false;
        std::string fileFormat;
        bool isDuplicate = false;
        std::string duplicateUploadId;
        std::string message;
        std::string errorMessage;
        std::vector<CertificatePreviewItem> certificates;
        std::vector<DeviationPreviewItem> deviations;
        CrlPreviewItem crlInfo;
        bool hasCrlInfo = false;
        std::string dlIssuerCountry;
        int dlVersion = 0;
        std::string dlHashAlgorithm;
        bool dlSignatureValid = false;
        std::string dlSigningTime;
        std::string dlEContentType;
        std::string dlCmsDigestAlgorithm;
        std::string dlCmsSignatureAlgorithm;
        std::string dlSignerDn;
    };

    CertificatePreviewResult previewCertificate(
        const std::string& fileName,
        const std::vector<uint8_t>& fileContent
    );

    // --- Individual Certificate Upload ---

    struct CertificateUploadResult {
        bool success;
        std::string uploadId;
        std::string message;
        std::string fileFormat;
        int certificateCount = 0;
        int cscaCount = 0;
        int dscCount = 0;
        int dscNcCount = 0;
        int mlscCount = 0;
        int crlCount = 0;
        int ldapStoredCount = 0;
        int duplicateCount = 0;
        std::string status;
        std::string errorMessage;
    };

    CertificateUploadResult uploadCertificate(
        const std::string& fileName,
        const std::vector<uint8_t>& fileContent,
        const std::string& uploadedBy
    );

private:
    repositories::UploadRepository* uploadRepo_;
    repositories::CertificateRepository* certRepo_;
    common::LdapConnectionPool* ldapPool_;
    repositories::DeviationListRepository* dlRepo_;

    std::string generateUploadId();
    static std::string computeFileHash(const std::vector<uint8_t>& content);
    static LDAP* getLdapWriteConnection();
    static std::string scrubCredentials(const std::string& message);

    void processSingleCertificate(CertificateUploadResult& result, X509* cert,
                                   const std::vector<uint8_t>& rawContent, LDAP* ld);
    void processCrlFile(CertificateUploadResult& result,
                        const std::vector<uint8_t>& fileContent, LDAP* ld);
    void processDlFile(CertificateUploadResult& result,
                       const std::vector<uint8_t>& fileContent, LDAP* ld);
};

} // namespace services
