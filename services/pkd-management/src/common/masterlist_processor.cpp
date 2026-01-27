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
    PGconn* conn,
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

    // Step 2: Parse CMS structure to extract individual CSCAs
    BIO* bio = BIO_new_mem_buf(mlBytes.data(), static_cast<int>(mlBytes.size()));
    if (!bio) {
        spdlog::error("[ML] Failed to create BIO for Master List");
        return false;
    }

    CMS_ContentInfo* cms = d2i_CMS_bio(bio, nullptr);
    STACK_OF(X509)* certs = nullptr;
    std::string signerDn;

    if (cms) {
        // CMS format
        certs = CMS_get1_certs(cms);
        if (certs && sk_X509_num(certs) > 0) {
            X509* firstCert = sk_X509_value(certs, 0);
            if (firstCert) {
                char subjectBuf[512];
                X509_NAME_oneline(X509_get_subject_name(firstCert), subjectBuf, sizeof(subjectBuf));
                signerDn = subjectBuf;
            }
        }
    } else {
        // Fallback: Try PKCS#7
        BIO_reset(bio);
        PKCS7* p7 = d2i_PKCS7_bio(bio, nullptr);
        if (p7) {
            if (PKCS7_type_is_signed(p7)) {
                certs = p7->d.sign->cert;
                if (certs && sk_X509_num(certs) > 0) {
                    X509* firstCert = sk_X509_value(certs, 0);
                    if (firstCert) {
                        char subjectBuf[512];
                        X509_NAME_oneline(X509_get_subject_name(firstCert), subjectBuf, sizeof(subjectBuf));
                        signerDn = subjectBuf;
                    }
                }
            }
            // Note: Don't free certs for PKCS7, they're managed by p7
            PKCS7_free(p7);
            certs = nullptr;  // Prevent double-free
        }
    }

    BIO_free(bio);

    if (!certs || sk_X509_num(certs) == 0) {
        spdlog::warn("[ML] No certificates found in Master List: {}", entry.dn);
        if (cms) {
            if (certs) sk_X509_pop_free(certs, X509_free);
            CMS_ContentInfo_free(cms);
        }
        return false;
    }

    int totalCscas = sk_X509_num(certs);
    spdlog::info("[ML] Master List contains {} CSCAs: country={}, fingerprint={}",
                totalCscas, countryCode, mlFingerprint.substr(0, 16) + "...");

    // Use cn from entry as fallback signer DN
    if (signerDn.empty()) {
        signerDn = entry.getFirstAttribute("cn");
        if (signerDn.empty()) {
            signerDn = "Unknown";
        }
    }

    // Step 3: Extract and save each CSCA
    int newCount = 0;
    int dupCount = 0;

    for (int i = 0; i < totalCscas; i++) {
        X509* cert = sk_X509_value(certs, i);
        if (!cert) continue;

        // Extract certificate metadata
        CertificateMetadata meta = extractCertificateMetadata(cert);

        if (meta.derData.empty() || meta.fingerprint.empty()) {
            spdlog::warn("[ML] CSCA {}/{} - Failed to extract metadata", i + 1, totalCscas);
            continue;
        }

        // Extract country code from certificate Subject DN (not from LDAP Entry DN)
        // This ensures correct country code for cross-border Master Lists
        std::string certCountryCode = extractCountryCode(meta.subjectDn);
        if (certCountryCode == "XX") {
            // Fallback to LDAP Entry DN country code
            certCountryCode = countryCode;
        }

        // Determine certificate type: Self-signed CSCA or Master List Signer Certificate
        // Master List Signer Certificate: Subject DN != Issuer DN (Cross-signed by CSCA)
        // Self-signed CSCA: Subject DN == Issuer DN
        bool isMasterListSigner = (meta.subjectDn != meta.issuerDn);
        std::string certType = isMasterListSigner ? "CSCA" : "CSCA";  // DB always stores as CSCA
        std::string ldapCertType = isMasterListSigner ? "MLSC" : "CSCA";  // LDAP uses MLSC for Master List Signers

        // Save with duplicate check
        auto [certId, isDuplicate] = certificate_utils::saveCertificateWithDuplicateCheck(
            conn, uploadId, certType, certCountryCode,
            meta.subjectDn, meta.issuerDn, meta.serialNumber, meta.fingerprint,
            meta.notBefore, meta.notAfter, meta.derData,
            "UNKNOWN", ""
        );

        if (certId.empty()) {
            spdlog::error("[ML] CSCA {}/{} - Failed to save to DB", i + 1, totalCscas);
            continue;
        }

        // Track source in certificate_duplicates table
        std::string sourceFileName = "";  // Could extract from uploaded_file if needed
        certificate_utils::trackCertificateDuplicate(
            conn, certId, uploadId, "LDIF_002",
            countryCode, entry.dn, sourceFileName
        );

        if (isDuplicate) {
            // Duplicate detected
            dupCount++;
            certificate_utils::incrementDuplicateCount(conn, certId, uploadId);

            std::string certTypeLabel = isMasterListSigner ? "MLSC (Master List Signer)" : "CSCA (Self-signed)";
            spdlog::info("[ML] {} {}/{} - DUPLICATE - fingerprint: {}, cert_id: {}, subject: {}",
                        certTypeLabel, i + 1, totalCscas, meta.fingerprint.substr(0, 16) + "...",
                        certId, meta.subjectDn);
        } else {
            // New certificate
            newCount++;

            std::string certTypeLabel = isMasterListSigner ? "MLSC (Master List Signer)" : "CSCA (Self-signed)";
            spdlog::info("[ML] {} {}/{} - NEW - fingerprint: {}, cert_id: {}, subject: {}",
                        certTypeLabel, i + 1, totalCscas, meta.fingerprint.substr(0, 16) + "...",
                        certId, meta.subjectDn);

            // Save to LDAP: o=mlsc for Master List Signers, o=csca for Self-signed CSCAs (only new certificates)
            if (ld) {
                std::string ldapDn = saveCertificateToLdap(
                    ld, ldapCertType, certCountryCode,  // Use MLSC or CSCA based on certificate type
                    meta.subjectDn, meta.issuerDn, meta.serialNumber,
                    meta.fingerprint,  // Add fingerprint parameter
                    meta.derData,      // certBinary
                    "", "", "",        // pkdConformanceCode, pkdConformanceText, pkdVersion
                    false              // useLegacyDn=false (use fingerprint-based DN)
                );

                if (!ldapDn.empty()) {
                    stats.ldapCscaStoredCount++;  // Include Master List Signers in CSCA statistics
                    spdlog::debug("[ML] {} {}/{} - Saved to LDAP: {}", certTypeLabel, i + 1, totalCscas, ldapDn);

                    // Update stored_in_ldap flag in DB
                    certificate_utils::updateCertificateLdapStatus(conn, certId, ldapDn);
                } else {
                    spdlog::warn("[ML] {} {}/{} - Failed to save to LDAP", certTypeLabel, i + 1, totalCscas);
                }
            }
        }
    }

    // Cleanup CMS structures
    if (cms) {
        sk_X509_pop_free(certs, X509_free);
        CMS_ContentInfo_free(cms);
    }

    // Update statistics
    stats.cscaExtractedCount += totalCscas;
    stats.cscaNewCount += newCount;
    stats.cscaDuplicateCount += dupCount;

    spdlog::info("[ML] Extracted {} CSCAs: {} new, {} duplicates",
                totalCscas, newCount, dupCount);

    // Step 4: Save original Master List CMS to o=ml (backup)
    std::string mlId = saveMasterList(conn, uploadId, countryCode, signerDn,
                                     mlFingerprint, totalCscas, mlBytes);

    if (!mlId.empty()) {
        stats.mlCount++;
        spdlog::info("[ML] Saved Master List to DB: id={}, country={}", mlId, countryCode);

        // Save to LDAP o=ml (backup)
        if (ld) {
            std::string ldapDn = saveMasterListToLdap(ld, countryCode, signerDn,
                                                     mlFingerprint, mlBytes);
            if (!ldapDn.empty()) {
                updateMasterListLdapStatus(conn, mlId, ldapDn);
                stats.ldapMlStoredCount++;
                spdlog::info("[ML] Saved Master List to LDAP o=ml: {}", ldapDn);
            }
        }
    } else {
        spdlog::error("[ML] Failed to save Master List to DB");
        return false;
    }

    // Step 5: Update upload file statistics
    certificate_utils::updateCscaExtractionStats(conn, uploadId, totalCscas, dupCount);

    return true;
}
// New function for direct Master List file processing
// Uses the same proven logic as parseMasterListEntryV2
// Fixed processMasterListFile() - Extract pkiData (536 certificates)
// Based on main.cpp 4270-4600 logic

bool processMasterListFile(
    PGconn* conn,
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
                if (certs && sk_X509_num(certs) > 0) {
                    // Use first certificate as signer (usually there's only 1 MLSC)
                    signerCert = sk_X509_value(certs, 0);
                    if (signerCert) {
                        X509_up_ref(signerCert);  // Increment reference count
                    }
                    sk_X509_pop_free(certs, X509_free);
                }

                if (!signerCert) {
                    // Fallback: try to find certificate using issuer/serial from SignerInfo
                    X509_NAME* issuer = nullptr;
                    ASN1_INTEGER* serial = nullptr;
                    CMS_SignerInfo_get0_signer_id(si, nullptr, &issuer, &serial);

                    if (issuer && serial) {
                        spdlog::debug("[ML-FILE] SignerInfo {}/{}: Could not find certificate in CMS", i + 1, numSigners);
                    }
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
                    conn, uploadId, "MLSC", countryCode,
                    meta.subjectDn, meta.issuerDn, meta.serialNumber, meta.fingerprint,
                    meta.notBefore, meta.notAfter, meta.derData,
                    "UNKNOWN", ""
                );

                if (!certId.empty()) {
                    certificate_utils::trackCertificateDuplicate(
                        conn, certId, uploadId, "ML_FILE",
                        countryCode, "Master List Signer", ""
                    );

                    if (!isDuplicate) {
                        stats.mlCount++;
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
                                certificate_utils::updateCertificateLdapStatus(conn, certId, ldapDn);
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
                conn, uploadId, certType, certCountryCode,
                meta.subjectDn, meta.issuerDn, meta.serialNumber, meta.fingerprint,
                meta.notBefore, meta.notAfter, meta.derData,
                "UNKNOWN", ""
            );

            if (certId.empty()) {
                spdlog::error("[ML-FILE] Certificate {} - Failed to save", totalCerts);
                X509_free(cert);
                continue;
            }

            certificate_utils::trackCertificateDuplicate(
                conn, certId, uploadId, "ML_FILE",
                certCountryCode, "Master List", ""
            );

            if (isDuplicate) {
                dupCount++;
                certificate_utils::incrementDuplicateCount(conn, certId, uploadId);
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
                        certificate_utils::updateCertificateLdapStatus(conn, certId, ldapDn);
                        stats.ldapCscaStoredCount++;
                    }
                }
            }

            X509_free(cert);
        }

        spdlog::info("[ML-FILE] Extracted {} CSCA/LC certificates: {} new, {} duplicates",
                    totalCerts, newCount, dupCount);

        // Update statistics
        stats.cscaExtractedCount = totalCerts;
        stats.cscaNewCount = newCount;
        stats.cscaDuplicateCount = dupCount;

        // Save Master List to DB
        std::string mlId = saveMasterList(conn, uploadId, countryCode, signerDn,
                                         mlFingerprint, totalCerts, content);

        if (!mlId.empty()) {
            spdlog::info("[ML-FILE] Saved Master List to DB: id={}", mlId);
        }

        certificate_utils::updateCscaExtractionStats(conn, uploadId, totalCerts, dupCount);

        CMS_ContentInfo_free(cms);
        return true;

    } catch (const std::exception& e) {
        spdlog::error("[ML-FILE] Exception during Master List processing: {}", e.what());
        CMS_ContentInfo_free(cms);
        return false;
    }
}
