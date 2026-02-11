#include "masterlist_processor.h"
#include "certificate_utils.h"
#include "main_utils.h"
#include "../common.h"  // For LdifEntry structure
#include <spdlog/spdlog.h>
#include <openssl/cms.h>
#include <openssl/pkcs7.h>
#include <openssl/bio.h>
#include <openssl/x509.h>
#include <openssl/evp.h>
#include <map>

/**
 * @brief Extract X.509 certificate metadata
 */
struct CertificateMetadata {
    std::string subjectDn;
    std::string issuerDn;
    std::string serialNumber;
    std::string fingerprint;
    std::string notBefore;
    std::string notAfter;
    std::vector<uint8_t> derData;
};

static CertificateMetadata extractCertificateMetadata(X509* cert) {
    CertificateMetadata meta;

    // Extract Subject DN
    char subjectBuf[512];
    X509_NAME_oneline(X509_get_subject_name(cert), subjectBuf, sizeof(subjectBuf));
    meta.subjectDn = subjectBuf;

    // Extract Issuer DN
    char issuerBuf[512];
    X509_NAME_oneline(X509_get_issuer_name(cert), issuerBuf, sizeof(issuerBuf));
    meta.issuerDn = issuerBuf;

    // Extract Serial Number (hex)
    ASN1_INTEGER* serial = X509_get_serialNumber(cert);
    if (serial) {
        BIGNUM* bn = ASN1_INTEGER_to_BN(serial, nullptr);
        if (bn) {
            char* hexSerial = BN_bn2hex(bn);
            if (hexSerial) {
                meta.serialNumber = hexSerial;
                OPENSSL_free(hexSerial);
            }
            BN_free(bn);
        }
    }

    // Extract Validity
    const ASN1_TIME* notBefore = X509_get0_notBefore(cert);
    const ASN1_TIME* notAfter = X509_get0_notAfter(cert);

    if (notBefore) {
        BIO* bio = BIO_new(BIO_s_mem());
        ASN1_TIME_print(bio, notBefore);
        char timeBuf[64];
        int len = BIO_read(bio, timeBuf, sizeof(timeBuf) - 1);
        if (len > 0) {
            timeBuf[len] = '\0';
            meta.notBefore = timeBuf;
        }
        BIO_free(bio);
    }

    if (notAfter) {
        BIO* bio = BIO_new(BIO_s_mem());
        ASN1_TIME_print(bio, notAfter);
        char timeBuf[64];
        int len = BIO_read(bio, timeBuf, sizeof(timeBuf) - 1);
        if (len > 0) {
            timeBuf[len] = '\0';
            meta.notAfter = timeBuf;
        }
        BIO_free(bio);
    }

    // Convert to DER format
    unsigned char* derBuf = nullptr;
    int derLen = i2d_X509(cert, &derBuf);
    if (derLen > 0 && derBuf) {
        meta.derData.assign(derBuf, derBuf + derLen);
        OPENSSL_free(derBuf);
    }

    // Calculate SHA-256 fingerprint
    if (!meta.derData.empty()) {
        meta.fingerprint = computeFileHash(meta.derData);
    }

    return meta;
}

bool parseMasterListEntryV2(
    LDAP* ld,
    const std::string& uploadId,
    const LdifEntry& entry,
    MasterListStats& stats
) {
    // Step 1: Extract and decode pkdMasterListContent
    std::string base64Value = entry.getFirstAttribute("pkdMasterListContent;binary");
    if (base64Value.empty()) {
        base64Value = entry.getFirstAttribute("pkdMasterListContent");
    }
    if (base64Value.empty()) {
        spdlog::warn("[ML] No pkdMasterListContent found in entry: {}", entry.dn);
        return false;
    }

    std::vector<uint8_t> mlBytes = base64Decode(base64Value);
    if (mlBytes.empty()) {
        spdlog::error("[ML] Failed to decode Master List content: {}", entry.dn);
        return false;
    }

    spdlog::info("[ML] Parsing Master List entry: dn={}, size={} bytes", entry.dn, mlBytes.size());

    // Extract country code from DN
    std::string countryCode = extractCountryCodeFromDn(entry.dn);

    // Calculate fingerprint of entire Master List
    std::string mlFingerprint = computeFileHash(mlBytes);

    // Step 2: Parse CMS SignedData structure
    BIO* bio = BIO_new_mem_buf(mlBytes.data(), static_cast<int>(mlBytes.size()));
    if (!bio) {
        spdlog::error("[ML-LDIF] Failed to create BIO for Master List: {}", entry.dn);
        return false;
    }

    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (!cms) {
        spdlog::error("[ML-LDIF] Failed to parse Master List as CMS SignedData: {}", entry.dn);
        return false;
    }

    spdlog::info("[ML-LDIF] CMS SignedData parsed successfully: dn={}, size={} bytes", entry.dn, mlBytes.size());

    int totalCerts = 0;
    int newCount = 0;
    int dupCount = 0;
    std::string signerDn = "Unknown";

    try {
        // ====================================================================
        // Step 2a: Extract MLSC certificates from CMS SignedData
        // ====================================================================
        // First, get all certificates from the CMS SignedData.certificates field
        STACK_OF(X509)* certs = CMS_get1_certs(cms);
        int numCerts = certs ? sk_X509_num(certs) : 0;
        spdlog::info("[ML-LDIF] CMS SignedData contains {} certificate(s)", numCerts);

        // Get SignerInfo entries to match certificates
        STACK_OF(CMS_SignerInfo)* signerInfos = CMS_get0_SignerInfos(cms);
        if (signerInfos && sk_CMS_SignerInfo_num(signerInfos) > 0) {
            int numSigners = sk_CMS_SignerInfo_num(signerInfos);
            spdlog::info("[ML-LDIF] Found {} SignerInfo entry(ies)", numSigners);

            for (int i = 0; i < numSigners; i++) {
                CMS_SignerInfo* si = sk_CMS_SignerInfo_value(signerInfos, i);
                if (!si) continue;

                // Try to find matching certificate from CMS certificates
                X509* signerCert = nullptr;

                if (certs && numCerts > 0) {
                    // Match certificate with SignerInfo using issuer and serial
                    for (int j = 0; j < numCerts; j++) {
                        X509* cert = sk_X509_value(certs, j);
                        if (CMS_SignerInfo_cert_cmp(si, cert) == 0) {
                            signerCert = cert;
                            spdlog::info("[ML-LDIF] MLSC {}/{} - Matched certificate from CMS certificates field (index {})",
                                        i + 1, numSigners, j);
                            break;
                        }
                    }
                }

                if (!signerCert) {
                    // Get issuer and serial from SignerInfo for logging
                    X509_NAME* issuer = nullptr;
                    ASN1_INTEGER* serial = nullptr;
                    CMS_SignerInfo_get0_signer_id(si, nullptr, &issuer, &serial);

                    char issuerBuf[512] = {0};
                    if (issuer) {
                        X509_NAME_oneline(issuer, issuerBuf, sizeof(issuerBuf));
                    }

                    spdlog::warn("[ML-LDIF] MLSC {}/{} - No embedded certificate found (Issuer: {}). "
                                "Master List only references MLSC, not embedding it.",
                                i + 1, numSigners, issuerBuf);
                    continue;
                }

                // Extract signer DN
                char subjectBuf[512];
                X509_NAME_oneline(X509_get_subject_name(signerCert), subjectBuf, sizeof(subjectBuf));
                signerDn = subjectBuf;

                // Extract MLSC metadata
                CertificateMetadata meta = extractCertificateMetadata(signerCert);
                if (meta.derData.empty() || meta.fingerprint.empty()) {
                    spdlog::warn("[ML-LDIF] MLSC {}/{} - Failed to extract metadata", i + 1, numSigners);
                    continue;
                }

                // Extract country code (usually UN for MLSC)
                std::string certCountryCode = extractCountryCode(meta.subjectDn);
                if (certCountryCode == "XX") {
                    certCountryCode = countryCode;  // Fallback to LDAP entry country
                }

                spdlog::info("[ML-LDIF] MLSC {}/{} - Signer DN: {}, Country: {}",
                            i + 1, numSigners, signerDn, certCountryCode);

                // Save MLSC with duplicate check
                auto [certId, isDuplicate] = certificate_utils::saveCertificateWithDuplicateCheck(
                    uploadId, "MLSC", certCountryCode,
                    meta.subjectDn, meta.issuerDn, meta.serialNumber, meta.fingerprint,
                    meta.notBefore, meta.notAfter, meta.derData,
                    "UNKNOWN", ""
                );

                if (certId.empty()) {
                    spdlog::error("[ML-LDIF] MLSC {}/{} - Failed to save to DB, reason: Database operation failed",
                                 i + 1, numSigners);
                    continue;
                }

                // Track source
                certificate_utils::trackCertificateDuplicate(
                    certId, uploadId, "LDIF_002",
                    certCountryCode, entry.dn, ""
                );

                if (isDuplicate) {
                    certificate_utils::incrementDuplicateCount(certId, uploadId);
                    spdlog::info("[ML-LDIF] MLSC {}/{} - DUPLICATE - fingerprint: {}, cert_id: {}, reason: Already exists in DB",
                                i + 1, numSigners, meta.fingerprint.substr(0, 16) + "...", certId);
                } else {
                    stats.mlscCount++;  // Track MLSC count (v2.1.1)
                    spdlog::info("[ML-LDIF] MLSC {}/{} - NEW - fingerprint: {}, cert_id: {}",
                                i + 1, numSigners, meta.fingerprint.substr(0, 16) + "...", certId);

                    // Save MLSC to LDAP (o=mlsc)
                    if (ld) {
                        std::string ldapDn = saveCertificateToLdap(
                            ld, "MLSC", certCountryCode,
                            meta.subjectDn, meta.issuerDn, meta.serialNumber,
                            meta.fingerprint, meta.derData,
                            "", "", "",
                            false  // useLegacyDn=false
                        );

                        if (!ldapDn.empty()) {
                            certificate_utils::updateCertificateLdapStatus(certId, ldapDn);
                            spdlog::info("[ML-LDIF] MLSC {}/{} - Saved to LDAP: {}", i + 1, numSigners, ldapDn);
                        } else {
                            spdlog::warn("[ML-LDIF] MLSC {}/{} - Failed to save to LDAP, reason: LDAP operation failed",
                                        i + 1, numSigners);
                        }
                    }
                }
            }
        }

        // Free certificates stack
        if (certs) {
            sk_X509_pop_free(certs, X509_free);
        }

        // ====================================================================
        // Step 2b: Extract CSCA/LC certificates from pkiData
        // ====================================================================
        ASN1_OCTET_STRING** contentPtr = CMS_get0_content(cms);
        if (!contentPtr || !*contentPtr) {
            spdlog::warn("[ML-LDIF] No encapsulated content (pkiData) found: {}", entry.dn);
            CMS_ContentInfo_free(cms);
            return true;  // MLSC extraction succeeded, no pkiData is acceptable
        }

        const unsigned char* contentData = ASN1_STRING_get0_data(*contentPtr);
        int contentLen = ASN1_STRING_length(*contentPtr);

        spdlog::info("[ML-LDIF] Encapsulated content length: {} bytes", contentLen);

        // Parse MasterList ASN.1 structure: MasterList ::= SEQUENCE { version INTEGER OPTIONAL, certList SET OF Certificate }
        const unsigned char* p = contentData;
        long remaining = contentLen;

        // Parse outer SEQUENCE
        int tag, xclass;
        long seqLen;
        int ret = ASN1_get_object(&p, &seqLen, &tag, &xclass, remaining);

        if (ret == 0x80 || tag != V_ASN1_SEQUENCE) {
            spdlog::error("[ML-LDIF] Invalid Master List structure: expected SEQUENCE, dn={}", entry.dn);
            CMS_ContentInfo_free(cms);
            return false;
        }

        const unsigned char* seqEnd = p + seqLen;

        // Check first element: could be version (INTEGER) or certList (SET)
        const unsigned char* elemStart = p;
        long elemLen;
        ret = ASN1_get_object(&p, &elemLen, &tag, &xclass, seqEnd - p);

        const unsigned char* certSetStart = nullptr;
        long certSetLen = 0;

        if (tag == V_ASN1_INTEGER) {
            // Has version, skip it and read next element (certList)
            p += elemLen;
            if (p < seqEnd) {
                ret = ASN1_get_object(&p, &elemLen, &tag, &xclass, seqEnd - p);
                if (tag == V_ASN1_SET) {
                    certSetStart = p;
                    certSetLen = elemLen;
                }
            }
        } else if (tag == V_ASN1_SET) {
            // No version, this is the certList
            certSetStart = p;
            certSetLen = elemLen;
        }

        if (!certSetStart || certSetLen == 0) {
            spdlog::warn("[ML-LDIF] No certList SET found in Master List structure: {}", entry.dn);
            CMS_ContentInfo_free(cms);
            return true;  // MLSC extraction succeeded, empty certList is acceptable
        }

        spdlog::info("[ML-LDIF] Found certList SET: {} bytes", certSetLen);

        // Parse certificates from SET
        const unsigned char* certPtr = certSetStart;
        const unsigned char* certSetEnd = certSetStart + certSetLen;

        while (certPtr < certSetEnd) {
            // Parse each certificate
            const unsigned char* certStart = certPtr;
            X509* cert = d2i_X509(nullptr, &certPtr, certSetEnd - certStart);

            if (!cert) {
                spdlog::warn("[ML-LDIF] Failed to parse certificate in certList SET");
                break;
            }

            totalCerts++;

            // Extract certificate metadata
            CertificateMetadata meta = extractCertificateMetadata(cert);
            if (meta.derData.empty() || meta.fingerprint.empty()) {
                spdlog::warn("[ML-LDIF] Certificate {} - Failed to extract metadata, reason: Metadata extraction failed", totalCerts);
                X509_free(cert);
                continue;
            }

            // Extract country code from Subject DN
            std::string certCountryCode = extractCountryCode(meta.subjectDn);
            if (certCountryCode == "XX") {
                // Try Issuer DN as fallback (for link certificates)
                certCountryCode = extractCountryCode(meta.issuerDn);
                if (certCountryCode == "XX") {
                    spdlog::warn("[ML-LDIF] Certificate {} - Could not extract country from Subject or Issuer DN, fingerprint: {}",
                                totalCerts, meta.fingerprint.substr(0, 16) + "...");
                    certCountryCode = countryCode;  // Use LDAP entry country as last resort
                }
            }

            // Determine if link certificate
            bool isLinkCertificate = (meta.subjectDn != meta.issuerDn);
            std::string certType = "CSCA";
            std::string ldapCertType = isLinkCertificate ? "LC" : "CSCA";

            // Save with duplicate check
            auto [certId, isDuplicate] = certificate_utils::saveCertificateWithDuplicateCheck(
                uploadId, certType, certCountryCode,
                meta.subjectDn, meta.issuerDn, meta.serialNumber, meta.fingerprint,
                meta.notBefore, meta.notAfter, meta.derData,
                "UNKNOWN", ""
            );

            if (certId.empty()) {
                std::string certTypeLabel = isLinkCertificate ? "LC (Link Certificate)" : "CSCA (Self-signed)";
                spdlog::error("[ML-LDIF] {} {} - Failed to save to DB, reason: Database operation failed, fingerprint: {}",
                             certTypeLabel, totalCerts, meta.fingerprint.substr(0, 16) + "...");
                X509_free(cert);
                continue;
            }

            certificate_utils::trackCertificateDuplicate(
                certId, uploadId, "LDIF_002",
                certCountryCode, entry.dn, ""
            );

            if (isDuplicate) {
                dupCount++;
                certificate_utils::incrementDuplicateCount(certId, uploadId);
                std::string certTypeLabel = isLinkCertificate ? "LC" : "CSCA";
                spdlog::debug("[ML-LDIF] {} {} - DUPLICATE - fingerprint: {}, cert_id: {}, reason: Already exists in DB",
                            certTypeLabel, totalCerts, meta.fingerprint.substr(0, 16) + "...", certId);
            } else {
                newCount++;
                std::string certTypeLabel = isLinkCertificate ? "LC (Link Certificate)" : "CSCA (Self-signed)";
                spdlog::info("[ML-LDIF] {} {} - NEW - Country: {}, fingerprint: {}, cert_id: {}",
                            certTypeLabel, totalCerts, certCountryCode, meta.fingerprint.substr(0, 16) + "...", certId);

                // Save to LDAP
                if (ld) {
                    std::string ldapDn = saveCertificateToLdap(
                        ld, ldapCertType, certCountryCode,
                        meta.subjectDn, meta.issuerDn, meta.serialNumber,
                        meta.fingerprint, meta.derData,
                        "", "", "",
                        false  // useLegacyDn=false
                    );

                    if (!ldapDn.empty()) {
                        certificate_utils::updateCertificateLdapStatus(certId, ldapDn);
                        stats.ldapCscaStoredCount++;
                        spdlog::debug("[ML-LDIF] {} {} - Saved to LDAP: {}", certTypeLabel, totalCerts, ldapDn);
                    } else {
                        spdlog::warn("[ML-LDIF] {} {} - Failed to save to LDAP, reason: LDAP operation failed",
                                    certTypeLabel, totalCerts);
                    }
                }
            }

            X509_free(cert);
        }

        spdlog::info("[ML-LDIF] Extracted {} CSCA/LC certificates: {} new, {} duplicates",
                    totalCerts, newCount, dupCount);

        // Update statistics
        stats.cscaExtractedCount += totalCerts;
        stats.cscaNewCount += newCount;
        stats.cscaDuplicateCount += dupCount;

    } catch (const std::exception& e) {
        spdlog::error("[ML-LDIF] Exception during Master List processing: {}, dn={}", e.what(), entry.dn);
        CMS_ContentInfo_free(cms);
        return false;
    }

    CMS_ContentInfo_free(cms);

    // Step 4: Save original Master List CMS to o=ml (backup)
    std::string mlId = saveMasterList(uploadId, countryCode, signerDn,
                                     mlFingerprint, totalCerts, mlBytes);

    if (!mlId.empty()) {
        spdlog::info("[ML-LDIF] Saved Master List to DB: id={}, country={}", mlId, countryCode);

        // Save to LDAP o=ml (backup)
        if (ld) {
            std::string ldapDn = saveMasterListToLdap(ld, countryCode, signerDn,
                                                     mlFingerprint, mlBytes);
            if (!ldapDn.empty()) {
                updateMasterListLdapStatus(mlId, ldapDn);
                stats.ldapMlStoredCount++;
                spdlog::info("[ML-LDIF] Saved Master List to LDAP o=ml: {}", ldapDn);
            } else {
                spdlog::warn("[ML-LDIF] Failed to save Master List to LDAP o=ml, reason: LDAP operation failed");
            }
        }
    } else {
        spdlog::error("[ML-LDIF] Failed to save Master List to DB, reason: Database operation failed");
        return false;
    }

    // Step 5: Update upload file statistics
    certificate_utils::updateCscaExtractionStats(uploadId, totalCerts, dupCount);

    return true;
}
// New function for direct Master List file processing
// Uses the same proven logic as parseMasterListEntryV2
// Fixed processMasterListFile() - Extract pkiData (536 certificates)
// Based on main.cpp 4270-4600 logic

bool processMasterListFile(
    LDAP* ld,
    const std::string& uploadId,
    const std::vector<uint8_t>& content,
    MasterListStats& stats
) {
    spdlog::info("[ML-FILE] Processing Master List file: {} bytes", content.size());

    // Reset stats
    stats = MasterListStats();

    // Validate CMS format: first byte must be 0x30 (SEQUENCE tag)
    if (content.empty() || content[0] != 0x30) {
        spdlog::error("[ML-FILE] Invalid Master List: not a valid CMS structure (missing SEQUENCE tag)");
        return false;
    }

    // Parse as CMS SignedData
    BIO* bio = BIO_new_mem_buf(content.data(), static_cast<int>(content.size()));
    if (!bio) {
        spdlog::error("[ML-FILE] Failed to create BIO");
        return false;
    }

    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    BIO_free(bio);

    if (!cms) {
        spdlog::error("[ML-FILE] Failed to parse Master List as CMS SignedData");
        return false;
    }

    spdlog::info("[ML-FILE] CMS SignedData parsed successfully");

    int totalCerts = 0;
    int newCount = 0;
    int dupCount = 0;
    std::string mlFingerprint = computeFileHash(content);
    std::string signerDn = "Unknown";
    std::string countryCode = "UN";  // Default to UN (ICAO)

    try {
        // ====================================================================
        // Step 1: Extract MLSC certificates from SignerInfo
        // ====================================================================
        // Note: CMS_get1_certs() returns SignedData.certificates field, which is empty in ICAO Master Lists
        // We need to extract certificates from SignerInfo instead
        STACK_OF(CMS_SignerInfo)* signerInfos = CMS_get0_SignerInfos(cms);
        if (signerInfos && sk_CMS_SignerInfo_num(signerInfos) > 0) {
            int numSigners = sk_CMS_SignerInfo_num(signerInfos);
            spdlog::info("[ML-FILE] Found {} SignerInfo entries", numSigners);

            // Extract signing certificates from each SignerInfo
            for (int i = 0; i < numSigners; i++) {
                CMS_SignerInfo* si = sk_CMS_SignerInfo_value(signerInfos, i);
                if (!si) continue;

                // Get the signing certificate from this SignerInfo
                X509* signerCert = nullptr;

                // Try to get certificate from SignerInfo's certificate chain
                STACK_OF(X509)* certs = CMS_get1_certs(cms);
                int numCerts = certs ? sk_X509_num(certs) : 0;

                if (certs && numCerts > 0) {
                    spdlog::info("[ML-FILE] CMS SignedData contains {} certificate(s)", numCerts);

                    // Match certificate with SignerInfo using issuer and serial
                    for (int j = 0; j < numCerts; j++) {
                        X509* cert = sk_X509_value(certs, j);
                        if (CMS_SignerInfo_cert_cmp(si, cert) == 0) {
                            signerCert = cert;
                            X509_up_ref(signerCert);  // Increment reference count
                            spdlog::info("[ML-FILE] MLSC {}/{} - Matched certificate from CMS certificates field (index {})",
                                        i + 1, numSigners, j);
                            break;
                        }
                    }
                    sk_X509_pop_free(certs, X509_free);
                }

                if (!signerCert) {
                    // Get issuer and serial from SignerInfo for logging
                    X509_NAME* issuer = nullptr;
                    ASN1_INTEGER* serial = nullptr;
                    CMS_SignerInfo_get0_signer_id(si, nullptr, &issuer, &serial);

                    char issuerBuf[512] = {0};
                    if (issuer) {
                        X509_NAME_oneline(issuer, issuerBuf, sizeof(issuerBuf));
                    }

                    spdlog::warn("[ML-FILE] MLSC {}/{} - No embedded certificate found (Issuer: {}). "
                                "Master List only references MLSC, not embedding it.",
                                i + 1, numSigners, issuerBuf);
                    continue;
                }

                // Extract metadata and save MLSC
                CertificateMetadata meta = extractCertificateMetadata(signerCert);
                if (meta.derData.empty() || meta.fingerprint.empty()) {
                    spdlog::warn("[ML-FILE] MLSC {}/{} - Failed to extract metadata", i + 1, numSigners);
                    X509_free(signerCert);
                    continue;
                }

                // Extract country from signer DN (usually UN for ICAO)
                char subjectBuf[512];
                X509_NAME_oneline(X509_get_subject_name(signerCert), subjectBuf, sizeof(subjectBuf));
                signerDn = subjectBuf;
                countryCode = extractCountryCode(signerDn);
                if (countryCode == "XX") {
                    countryCode = "UN";  // Default to UN for ICAO Master List
                }

                spdlog::info("[ML-FILE] MLSC {}/{} - Signer DN: {}, Country: {}",
                            i + 1, numSigners, signerDn, countryCode);

                // Save MLSC with duplicate check
                auto [certId, isDuplicate] = certificate_utils::saveCertificateWithDuplicateCheck(
                    uploadId, "MLSC", countryCode,
                    meta.subjectDn, meta.issuerDn, meta.serialNumber, meta.fingerprint,
                    meta.notBefore, meta.notAfter, meta.derData,
                    "UNKNOWN", ""
                );

                if (!certId.empty()) {
                    certificate_utils::trackCertificateDuplicate(
                        certId, uploadId, "ML_FILE",
                        countryCode, "Master List Signer", ""
                    );

                    if (!isDuplicate) {
                        stats.mlscCount++;
                        spdlog::info("[ML-FILE] MLSC {}/{} - NEW - fingerprint: {}, cert_id: {}",
                                    i + 1, numSigners, meta.fingerprint.substr(0, 16) + "...", certId);

                        // Save MLSC to LDAP (o=mlsc,c=UN)
                        if (ld) {
                            std::string ldapDn = saveCertificateToLdap(
                                ld, "MLSC", countryCode,
                                meta.subjectDn, meta.issuerDn, meta.serialNumber,
                                meta.fingerprint, meta.derData,
                                "", "", "",
                                false  // useLegacyDn=false
                            );

                            if (!ldapDn.empty()) {
                                certificate_utils::updateCertificateLdapStatus(certId, ldapDn);
                                spdlog::info("[ML-FILE] MLSC {}/{} - Saved to LDAP: {}", i + 1, numSigners, ldapDn);
                            }
                        }
                    } else {
                        spdlog::info("[ML-FILE] MLSC {}/{} - DUPLICATE - fingerprint: {}",
                                    i + 1, numSigners, meta.fingerprint.substr(0, 16) + "...");
                    }
                }

                X509_free(signerCert);
            }
        } else {
            spdlog::warn("[ML-FILE] No SignerInfo found in CMS SignedData");
        }

        // ====================================================================
        // Step 2: Extract CSCA/LC certificates from pkiData
        // ====================================================================
        ASN1_OCTET_STRING** contentPtr = CMS_get0_content(cms);
        if (!contentPtr || !*contentPtr) {
            spdlog::error("[ML-FILE] Failed to extract encapsulated content (pkiData)");
            CMS_ContentInfo_free(cms);
            return false;
        }

        const unsigned char* contentData = ASN1_STRING_get0_data(*contentPtr);
        int contentLen = ASN1_STRING_length(*contentPtr);

        spdlog::info("[ML-FILE] Encapsulated content length: {} bytes", contentLen);

        // Parse the Master List ASN.1 structure
        // MasterList ::= SEQUENCE { version INTEGER OPTIONAL, certList SET OF Certificate }
        const unsigned char* p = contentData;
        long remaining = contentLen;

        // Parse outer SEQUENCE
        int tag, xclass;
        long seqLen;
        int ret = ASN1_get_object(&p, &seqLen, &tag, &xclass, remaining);

        if (ret == 0x80 || tag != V_ASN1_SEQUENCE) {
            spdlog::error("[ML-FILE] Invalid Master List structure: expected SEQUENCE");
            CMS_ContentInfo_free(cms);
            return false;
        }

        const unsigned char* seqEnd = p + seqLen;

        // Check first element: could be version (INTEGER) or certList (SET)
        const unsigned char* elemStart = p;
        long elemLen;
        ret = ASN1_get_object(&p, &elemLen, &tag, &xclass, seqEnd - p);

        const unsigned char* certSetStart = nullptr;
        long certSetLen = 0;

        if (tag == V_ASN1_INTEGER) {
            // Has version, skip it and read next element (certList)
            p += elemLen;
            if (p < seqEnd) {
                ret = ASN1_get_object(&p, &elemLen, &tag, &xclass, seqEnd - p);
                if (tag == V_ASN1_SET) {
                    certSetStart = p;
                    certSetLen = elemLen;
                }
            }
        } else if (tag == V_ASN1_SET) {
            // No version, this is the certList
            certSetStart = p;
            certSetLen = elemLen;
        }

        if (!certSetStart || certSetLen == 0) {
            spdlog::error("[ML-FILE] Failed to find certList SET in Master List structure");
            CMS_ContentInfo_free(cms);
            return false;
        }

        spdlog::info("[ML-FILE] Found certList SET: {} bytes", certSetLen);

        // Parse certificates from SET
        const unsigned char* certPtr = certSetStart;
        const unsigned char* certSetEnd = certSetStart + certSetLen;

        while (certPtr < certSetEnd) {
            // Parse each certificate
            const unsigned char* certStart = certPtr;
            X509* cert = d2i_X509(nullptr, &certPtr, certSetEnd - certStart);

            if (!cert) {
                spdlog::warn("[ML-FILE] Failed to parse certificate in certList SET");
                break;
            }

            totalCerts++;

            // Extract certificate metadata
            CertificateMetadata meta = extractCertificateMetadata(cert);
            if (meta.derData.empty() || meta.fingerprint.empty()) {
                spdlog::warn("[ML-FILE] Certificate {}/{} - Failed to extract metadata", totalCerts, "?");
                X509_free(cert);
                continue;
            }

            // Extract country code from certificate (NOT from Master List signer)
            std::string certCountryCode = extractCountryCode(meta.subjectDn);
            if (certCountryCode == "XX") {
                // Try issuer DN as fallback (for link certificates)
                certCountryCode = extractCountryCode(meta.issuerDn);
                if (certCountryCode == "XX") {
                    spdlog::warn("[ML-FILE] Certificate {} - Could not extract country from Subject or Issuer DN: {}",
                                totalCerts, meta.subjectDn);
                    // Keep as "XX" - do NOT use UN as fallback
                }
            }

            // Determine if link certificate
            bool isLinkCertificate = (meta.subjectDn != meta.issuerDn);
            std::string certType = "CSCA";
            std::string ldapCertType = isLinkCertificate ? "LC" : "CSCA";

            // Save with duplicate check
            auto [certId, isDuplicate] = certificate_utils::saveCertificateWithDuplicateCheck(
                uploadId, certType, certCountryCode,
                meta.subjectDn, meta.issuerDn, meta.serialNumber, meta.fingerprint,
                meta.notBefore, meta.notAfter, meta.derData,
                "UNKNOWN", ""
            );

            if (certId.empty()) {
                spdlog::error("[ML-FILE] Certificate {} - Failed to save", totalCerts);
                X509_free(cert);
                continue;
            }

            if (isDuplicate) {
                dupCount++;
                certificate_utils::trackCertificateDuplicate(
                    certId, uploadId, "ML_FILE",
                    certCountryCode, "Master List", ""
                );
                certificate_utils::incrementDuplicateCount(certId, uploadId);
            } else {
                newCount++;
                std::string certTypeLabel = isLinkCertificate ? "LC (Link Certificate)" : "CSCA (Self-signed)";
                spdlog::info("[ML-FILE] {} {} - NEW - Country: {}, fingerprint: {}, cert_id: {}",
                            certTypeLabel, totalCerts, certCountryCode, meta.fingerprint.substr(0, 16) + "...", certId);

                // Save to LDAP
                if (ld) {
                    std::string ldapDn = saveCertificateToLdap(
                        ld, ldapCertType, certCountryCode,
                        meta.subjectDn, meta.issuerDn, meta.serialNumber,
                        meta.fingerprint, meta.derData,
                        "", "", "",
                        false  // useLegacyDn=false
                    );

                    if (!ldapDn.empty()) {
                        certificate_utils::updateCertificateLdapStatus(certId, ldapDn);
                        stats.ldapCscaStoredCount++;
                    }
                }
            }

            X509_free(cert);
        }

        spdlog::info("[ML-FILE] Extracted {} CSCA/LC certificates: {} new, {} duplicates",
                    totalCerts, newCount, dupCount);

        // Update statistics
        stats.mlCount = 1;  // One Master List file processed
        stats.cscaExtractedCount = totalCerts;
        stats.cscaNewCount = newCount;
        stats.cscaDuplicateCount = dupCount;

        // Save Master List to DB
        std::string mlId = saveMasterList(uploadId, countryCode, signerDn,
                                         mlFingerprint, totalCerts, content);

        if (!mlId.empty()) {
            spdlog::info("[ML-FILE] Saved Master List to DB: id={}", mlId);
        }

        certificate_utils::updateCscaExtractionStats(uploadId, totalCerts, dupCount);

        CMS_ContentInfo_free(cms);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[ML-FILE] Exception during Master List processing: {}", e.what());
        CMS_ContentInfo_free(cms);
        return false;
    }
}
