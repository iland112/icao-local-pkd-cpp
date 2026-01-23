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

        // Save with duplicate check
        auto [certId, isDuplicate] = certificate_utils::saveCertificateWithDuplicateCheck(
            conn, uploadId, "CSCA", countryCode,
            meta.subjectDn, meta.issuerDn, meta.serialNumber, meta.fingerprint,
            meta.notBefore, meta.notAfter, meta.derData,
            "UNKNOWN", ""
        );

        if (certId < 0) {
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

            spdlog::info("[ML] CSCA {}/{} - DUPLICATE - fingerprint: {}, cert_id: {}, subject: {}",
                        i + 1, totalCscas, meta.fingerprint.substr(0, 16) + "...",
                        certId, meta.subjectDn);
        } else {
            // New certificate
            newCount++;

            spdlog::info("[ML] CSCA {}/{} - NEW - fingerprint: {}, cert_id: {}, subject: {}",
                        i + 1, totalCscas, meta.fingerprint.substr(0, 16) + "...",
                        certId, meta.subjectDn);

            // Save to LDAP o=csca (only new certificates)
            if (ld) {
                std::string ldapDn = saveCertificateToLdap(
                    ld, "CSCA", countryCode,
                    meta.subjectDn, meta.issuerDn, meta.serialNumber,
                    meta.fingerprint,  // Add fingerprint parameter
                    meta.derData       // certBinary
                );

                if (!ldapDn.empty()) {
                    stats.ldapCscaStoredCount++;
                    spdlog::debug("[ML] CSCA {}/{} - Saved to LDAP: {}", i + 1, totalCscas, ldapDn);
                } else {
                    spdlog::warn("[ML] CSCA {}/{} - Failed to save to LDAP", i + 1, totalCscas);
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
