#include "dl_parser.h"
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/asn1.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include <cstring>
#include <algorithm>
#include <sstream>
#include <iomanip>

namespace icao {
namespace certificate_parser {

// ICAO DL OID: 2.23.136.1.1.7
static const unsigned char DL_OID_BYTES[] = {
    0x06, 0x06, 0x67, 0x81, 0x08, 0x01, 0x01, 0x07
};

// ============================================================================
// ASN.1 low-level helpers
// ============================================================================

namespace {
    // Read ASN.1 TLV header, return content pointer and length.
    // Returns false if parsing fails.
    bool readTlv(const unsigned char*& p, long& remaining,
                 int& tag, int& tagClass, long& contentLen) {
        if (remaining <= 0) return false;
        const unsigned char* start = p;
        int constructed = 0;
        int ret = ASN1_get_object(&p, &contentLen, &tag, &tagClass, remaining);
        if (ret & 0x80) return false;  // error
        constructed = ret & V_ASN1_CONSTRUCTED;
        (void)constructed;
        remaining -= (p - start);
        return true;
    }

    // Skip over a complete TLV element
    bool skipTlv(const unsigned char*& p, long& remaining) {
        int tag, tagClass;
        long contentLen;
        if (!readTlv(p, remaining, tag, tagClass, contentLen)) return false;
        if (contentLen > remaining) return false;
        p += contentLen;
        remaining -= contentLen;
        return true;
    }

    // Read an INTEGER as hex string
    std::string readIntegerAsHex(const unsigned char* data, long len) {
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');
        for (long i = 0; i < len; i++) {
            oss << std::setw(2) << static_cast<int>(data[i]);
        }
        return oss.str();
    }

    // Read a PrintableString/UTF8String
    std::string readString(const unsigned char* data, long len) {
        return std::string(reinterpret_cast<const char*>(data), len);
    }

    // Read an OID and return dotted notation
    std::string readOid(const unsigned char* data, long len) {
        // Build a minimal DER-encoded OID (tag + length + content) for d2i_ASN1_OBJECT
        std::vector<unsigned char> derOid;
        derOid.push_back(0x06);  // OID tag
        if (len < 128) {
            derOid.push_back(static_cast<unsigned char>(len));
        } else {
            derOid.push_back(0x81);
            derOid.push_back(static_cast<unsigned char>(len));
        }
        derOid.insert(derOid.end(), data, data + len);

        const unsigned char* p = derOid.data();
        ASN1_OBJECT* obj = d2i_ASN1_OBJECT(nullptr, &p, static_cast<long>(derOid.size()));
        if (!obj) return "";
        char buf[256];
        OBJ_obj2txt(buf, sizeof(buf), obj, 1);  // 1 = numeric form
        ASN1_OBJECT_free(obj);
        return std::string(buf);
    }
}

// ============================================================================
// Public methods
// ============================================================================

DlParseResult DlParser::parse(const std::vector<uint8_t>& data) {
    DlParseResult result;

    if (data.empty()) {
        result.errorMessage = "Empty data";
        return result;
    }

    // Verify DL OID is present
    if (!containsDlOid(data)) {
        result.errorMessage = "DL OID (2.23.136.1.1.7) not found";
        return result;
    }

    // Parse as CMS ContentInfo (primary path for ICAO DL files)
    ERR_clear_error();
    const unsigned char* p = data.data();
    CMS_ContentInfo* cms = d2i_CMS_ContentInfo(nullptr, &p, static_cast<long>(data.size()));

    if (!cms) {
        result.errorMessage = "Failed to parse CMS ContentInfo structure";
        return result;
    }

    // Verify it's SignedData
    const ASN1_OBJECT* ctype = CMS_get0_type(cms);
    if (OBJ_obj2nid(ctype) != NID_pkcs7_signed) {
        CMS_ContentInfo_free(cms);
        result.errorMessage = "Not a CMS SignedData structure";
        return result;
    }

    // Extract signer certificate
    result.signerCertificate = extractSignerCertificateFromCms(cms);

    // Extract metadata from signer certificate
    if (result.signerCertificate) {
        result.issuerCountry = getCountryFromCert(result.signerCertificate);
        std::string org = getOrganizationFromCert(result.signerCertificate);
        if (!org.empty()) {
            result.issuerOrg = org;
        }
    }

    // Extract version and hashAlgorithm from eContent
    auto metadata = extractContentMetadata(cms);
    result.version = metadata.version;
    result.hashAlgorithm = metadata.hashAlgorithm;

    // Extract CMS-level metadata
    {
        // eContentType
        const ASN1_OBJECT* ectype = CMS_get0_eContentType(cms);
        if (ectype) {
            char buf[128];
            OBJ_obj2txt(buf, sizeof(buf), ectype, 1);  // numeric form
            result.eContentType = std::string(buf);
        }

        // CMS digest algorithm(s) from SignerInfo
        STACK_OF(CMS_SignerInfo)* signerInfos = CMS_get0_SignerInfos(cms);
        if (signerInfos && sk_CMS_SignerInfo_num(signerInfos) > 0) {
            CMS_SignerInfo* si = sk_CMS_SignerInfo_value(signerInfos, 0);
            if (si) {
                // Digest algorithm
                X509_ALGOR* digestAlg = nullptr;
                X509_ALGOR* sigAlg = nullptr;
                CMS_SignerInfo_get0_algs(si, nullptr, nullptr, &digestAlg, &sigAlg);
                if (digestAlg) {
                    const ASN1_OBJECT* digObj = nullptr;
                    X509_ALGOR_get0(&digObj, nullptr, nullptr, digestAlg);
                    if (digObj) {
                        char buf[128];
                        OBJ_obj2txt(buf, sizeof(buf), digObj, 1);
                        result.cmsDigestAlgorithm = oidToAlgorithmName(std::string(buf));
                    }
                }
                // Signature algorithm
                if (sigAlg) {
                    const ASN1_OBJECT* sigObj = nullptr;
                    X509_ALGOR_get0(&sigObj, nullptr, nullptr, sigAlg);
                    if (sigObj) {
                        int nid = OBJ_obj2nid(sigObj);
                        if (nid != NID_undef) {
                            result.cmsSignatureAlgorithm = OBJ_nid2sn(nid);
                        } else {
                            char buf[128];
                            OBJ_obj2txt(buf, sizeof(buf), sigObj, 1);
                            result.cmsSignatureAlgorithm = std::string(buf);
                        }
                    }
                }
            }
        }
    }

    // Extract signing time from SignerInfo
    result.signingTime = extractSigningTime(cms);

    // Verify signature
    result.signatureVerified = verifyCmsSignature(cms, result.signerCertificate);

    // Extract deviation entries from eContent
    result.deviations = extractDeviationsFromCms(cms);

    // Extract all embedded certificates
    result.certificates = extractCertificatesFromCms(cms);

    result.success = true;
    CMS_ContentInfo_free(cms);

    return result;
}

bool DlParser::containsDlOid(const std::vector<uint8_t>& data) {
    if (data.size() < sizeof(DL_OID_BYTES)) {
        return false;
    }
    auto it = std::search(
        data.begin(), data.end(),
        DL_OID_BYTES, DL_OID_BYTES + sizeof(DL_OID_BYTES)
    );
    return it != data.end();
}

// ============================================================================
// CMS extraction methods
// ============================================================================

X509* DlParser::extractSignerCertificateFromCms(CMS_ContentInfo* cms) {
    if (!cms) return nullptr;

    // Get all certificates from CMS
    STACK_OF(X509)* certs = CMS_get1_certs(cms);
    if (!certs || sk_X509_num(certs) == 0) {
        if (certs) sk_X509_pop_free(certs, X509_free);
        return nullptr;
    }

    // Find the signer certificate (has deviationListSigningKey EKU)
    // OID 2.23.136.1.1.8
    X509* signerCert = nullptr;
    int numCerts = sk_X509_num(certs);

    for (int i = 0; i < numCerts; i++) {
        X509* cert = sk_X509_value(certs, i);
        if (!cert) continue;

        // Check for DL Signer EKU or pick the non-CA cert
        BASIC_CONSTRAINTS* bc = (BASIC_CONSTRAINTS*)X509_get_ext_d2i(cert, NID_basic_constraints, nullptr, nullptr);
        bool isCA = false;
        if (bc) {
            isCA = bc->ca ? true : false;
            BASIC_CONSTRAINTS_free(bc);
        }

        if (!isCA) {
            signerCert = X509_dup(cert);
            break;
        }
    }

    // Fallback: if no non-CA cert found, use first cert
    if (!signerCert && numCerts > 0) {
        X509* first = sk_X509_value(certs, 0);
        if (first) signerCert = X509_dup(first);
    }

    sk_X509_pop_free(certs, X509_free);
    return signerCert;
}

std::vector<X509*> DlParser::extractCertificatesFromCms(CMS_ContentInfo* cms) {
    std::vector<X509*> certificates;
    if (!cms) return certificates;

    STACK_OF(X509)* certs = CMS_get1_certs(cms);
    if (!certs) return certificates;

    int numCerts = sk_X509_num(certs);
    for (int i = 0; i < numCerts; i++) {
        X509* cert = sk_X509_value(certs, i);
        if (cert) {
            X509* copy = X509_dup(cert);
            if (copy) certificates.push_back(copy);
        }
    }

    sk_X509_pop_free(certs, X509_free);
    return certificates;
}

std::string DlParser::extractSigningTime(CMS_ContentInfo* cms) {
    if (!cms) return "";

    STACK_OF(CMS_SignerInfo)* signerInfos = CMS_get0_SignerInfos(cms);
    if (!signerInfos || sk_CMS_SignerInfo_num(signerInfos) == 0) return "";

    CMS_SignerInfo* si = sk_CMS_SignerInfo_value(signerInfos, 0);
    if (!si) return "";

    // Get signing time from signed attributes
    int idx = CMS_signed_get_attr_by_NID(si, NID_pkcs9_signingTime, -1);
    if (idx < 0) return "";

    X509_ATTRIBUTE* attr = CMS_signed_get_attr(si, idx);
    if (!attr) return "";

    ASN1_TYPE* attrVal = X509_ATTRIBUTE_get0_type(attr, 0);
    if (!attrVal) return "";

    ASN1_TIME* sigTime = nullptr;
    if (attrVal->type == V_ASN1_UTCTIME) {
        sigTime = attrVal->value.utctime;
    } else if (attrVal->type == V_ASN1_GENERALIZEDTIME) {
        sigTime = attrVal->value.generalizedtime;
    }

    if (!sigTime) return "";

    // Convert ASN1_TIME to ISO 8601 format (YYYY-MM-DD HH:MM:SS)
    struct tm tmResult = {};
    if (ASN1_TIME_to_tm(sigTime, &tmResult) != 1) {
        return "";
    }

    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d",
             tmResult.tm_year + 1900,
             tmResult.tm_mon + 1,
             tmResult.tm_mday,
             tmResult.tm_hour,
             tmResult.tm_min,
             tmResult.tm_sec);
    return std::string(buf);
}

bool DlParser::verifyCmsSignature(CMS_ContentInfo* cms, X509* signerCert) {
    if (!cms || !signerCert) return false;

    X509_STORE* store = X509_STORE_new();
    if (!store) return false;
    X509_STORE_add_cert(store, signerCert);

    // CMS_verify with CMS_NO_SIGNER_CERT_VERIFY to skip full chain validation
    // We just want to verify the signature itself
    int flags = CMS_NO_SIGNER_CERT_VERIFY;
    int ret = CMS_verify(cms, nullptr, store, nullptr, nullptr, flags);

    X509_STORE_free(store);
    return ret == 1;
}

// ============================================================================
// eContent metadata extraction (version + hashAlgorithm)
// ============================================================================

DlParser::ContentMetadata DlParser::extractContentMetadata(CMS_ContentInfo* cms) {
    ContentMetadata meta;
    if (!cms) return meta;

    ASN1_OCTET_STRING** pContent = CMS_get0_content(cms);
    if (!pContent || !*pContent) return meta;

    const unsigned char* data = ASN1_STRING_get0_data(*pContent);
    long dataLen = ASN1_STRING_length(*pContent);
    if (!data || dataLen <= 0) return meta;

    const unsigned char* p = data;
    long remaining = dataLen;

    // Outer SEQUENCE
    int tag, tagClass;
    long contentLen;
    if (!readTlv(p, remaining, tag, tagClass, contentLen)) return meta;
    if (tag != V_ASN1_SEQUENCE) return meta;
    remaining = contentLen;

    // version INTEGER
    if (!readTlv(p, remaining, tag, tagClass, contentLen)) return meta;
    if (tag != V_ASN1_INTEGER) return meta;
    if (contentLen > 0) {
        meta.version = static_cast<int>(p[0]);
    }
    p += contentLen;
    remaining -= contentLen;

    // hashAlgorithm AlgorithmIdentifier ::= SEQUENCE { algorithm OID, parameters ANY OPTIONAL }
    const unsigned char* algoSeqStart = p;
    if (!readTlv(p, remaining, tag, tagClass, contentLen)) return meta;
    if (tag != V_ASN1_SEQUENCE) return meta;

    long algoRemaining = contentLen;
    // Read the OID inside
    if (!readTlv(p, algoRemaining, tag, tagClass, contentLen)) return meta;
    if (tag == V_ASN1_OBJECT && contentLen > 0) {
        std::string oid = readOid(p, contentLen);
        meta.hashAlgorithm = oidToAlgorithmName(oid);
    }

    return meta;
}

// ============================================================================
// ASN.1 Deviation content parsing
// ============================================================================

std::vector<DeviationEntry> DlParser::extractDeviationsFromCms(CMS_ContentInfo* cms) {
    std::vector<DeviationEntry> deviations;
    if (!cms) return deviations;

    // Get eContent (the DER-encoded DeviationList)
    ASN1_OCTET_STRING** pContent = CMS_get0_content(cms);
    if (!pContent || !*pContent) return deviations;

    const unsigned char* data = ASN1_STRING_get0_data(*pContent);
    long dataLen = ASN1_STRING_length(*pContent);
    if (!data || dataLen <= 0) return deviations;

    // Parse: DeviationList ::= SEQUENCE { version, hashAlgorithm, deviations SET OF }
    const unsigned char* p = data;
    long remaining = dataLen;

    // Outer SEQUENCE
    int tag, tagClass;
    long contentLen;
    if (!readTlv(p, remaining, tag, tagClass, contentLen)) return deviations;
    if (tag != V_ASN1_SEQUENCE) return deviations;
    remaining = contentLen;  // limit to SEQUENCE content

    // version INTEGER
    const unsigned char* intStart = p;
    if (!readTlv(p, remaining, tag, tagClass, contentLen)) return deviations;
    if (tag != V_ASN1_INTEGER) return deviations;
    // version value (skip, already have it)
    p += contentLen;
    remaining -= contentLen;

    // hashAlgorithm SEQUENCE (skip)
    if (!skipTlv(p, remaining)) return deviations;

    // deviations SET OF SignerDeviation
    if (!readTlv(p, remaining, tag, tagClass, contentLen)) return deviations;
    if (tag != V_ASN1_SET) return deviations;

    long setRemaining = contentLen;
    const unsigned char* setEnd = p + contentLen;

    // Iterate SET OF SignerDeviation
    while (p < setEnd && setRemaining > 0) {
        // SignerDeviation ::= SEQUENCE { signerIdentifier, defects }
        const unsigned char* sdStart = p;
        if (!readTlv(p, setRemaining, tag, tagClass, contentLen)) break;
        if (tag != V_ASN1_SEQUENCE) break;

        long sdRemaining = contentLen;

        // signerIdentifier: CertificateIdentifier ::= SEQUENCE { [1] IssuerAndSerialNumber }
        const unsigned char* ciStart = p;
        if (!readTlv(p, sdRemaining, tag, tagClass, contentLen)) break;
        if (tag != V_ASN1_SEQUENCE) break;

        long ciRemaining = contentLen;
        std::string issuerDn;
        std::string serialNumber;

        // Look for [1] IMPLICIT (context tag 1 = issuerAndSerialNumber)
        const unsigned char* innerP = p;
        long innerRem = ciRemaining;

        int innerTag, innerClass;
        long innerLen;
        if (readTlv(innerP, innerRem, innerTag, innerClass, innerLen)) {
            if (innerClass == (V_ASN1_CONTEXT_SPECIFIC | V_ASN1_CONSTRUCTED) ||
                (innerTag == 1 && (innerClass & V_ASN1_CONTEXT_SPECIFIC))) {
                // IssuerAndSerialNumber ::= SEQUENCE { issuer Name, serialNumber INTEGER }
                // The content is: SEQUENCE(Name) + INTEGER
                const unsigned char* isnP = innerP;
                long isnRem = innerLen;

                // Parse issuer Name (SEQUENCE)
                const unsigned char* nameStart = isnP;
                int nameTag, nameClass;
                long nameLen;
                if (readTlv(isnP, isnRem, nameTag, nameClass, nameLen)) {
                    if (nameTag == V_ASN1_SEQUENCE) {
                        // Decode X509_NAME from the SEQUENCE bytes
                        const unsigned char* nameBytes = nameStart;
                        X509_NAME* name = d2i_X509_NAME(nullptr, &nameBytes,
                            (isnP - nameStart) + nameLen);
                        if (name) {
                            issuerDn = x509NameToString(name);
                            X509_NAME_free(name);
                        }
                        isnP += nameLen;
                        isnRem -= nameLen;
                    }
                }

                // Parse serialNumber INTEGER
                if (isnRem > 0) {
                    int serTag, serClass;
                    long serLen;
                    if (readTlv(isnP, isnRem, serTag, serClass, serLen)) {
                        if (serTag == V_ASN1_INTEGER && serLen > 0) {
                            serialNumber = readIntegerAsHex(isnP, serLen);
                        }
                    }
                }
            }
        }

        // Skip past CertificateIdentifier
        p += ciRemaining;
        sdRemaining -= ciRemaining;

        // defects SET OF Defect
        if (sdRemaining <= 0) break;
        if (!readTlv(p, sdRemaining, tag, tagClass, contentLen)) break;
        if (tag != V_ASN1_SET) break;

        long defectsRemaining = contentLen;
        const unsigned char* defectsEnd = p + contentLen;

        while (p < defectsEnd && defectsRemaining > 0) {
            // Defect ::= SEQUENCE { description PrintableString, defectType OID, parameters [0] }
            if (!readTlv(p, defectsRemaining, tag, tagClass, contentLen)) break;
            if (tag != V_ASN1_SEQUENCE) break;

            long defectRemaining = contentLen;
            DeviationEntry entry;
            entry.certificateIssuerDn = issuerDn;
            entry.certificateSerialNumber = serialNumber;

            // Read elements within the Defect SEQUENCE
            while (defectRemaining > 0) {
                const unsigned char* elemStart = p;
                int elemTag, elemClass;
                long elemLen;
                if (!readTlv(p, defectRemaining, elemTag, elemClass, elemLen)) break;

                if (elemTag == V_ASN1_PRINTABLESTRING || elemTag == V_ASN1_UTF8STRING) {
                    entry.defectDescription = readString(p, elemLen);
                } else if (elemTag == V_ASN1_OBJECT) {
                    entry.defectTypeOid = readOid(p, elemLen);
                    entry.defectCategory = classifyDeviationOid(entry.defectTypeOid);
                } else if (elemClass & V_ASN1_CONTEXT_SPECIFIC) {
                    // [0] parameters
                    entry.defectParameters.assign(p, p + elemLen);
                }

                p += elemLen;
                defectRemaining -= elemLen;
            }

            if (!entry.defectTypeOid.empty()) {
                deviations.push_back(std::move(entry));
            }
        }

        // Advance past remaining SignerDeviation content
        if (sdRemaining > 0) {
            p += sdRemaining;
            setRemaining -= sdRemaining;
        }
    }

    return deviations;
}

// ============================================================================
// Helper methods
// ============================================================================

std::string DlParser::getCountryFromCert(X509* cert) {
    if (!cert) return "";
    X509_NAME* subject = X509_get_subject_name(cert);
    if (!subject) return "";
    int pos = X509_NAME_get_index_by_NID(subject, NID_countryName, -1);
    if (pos < 0) return "";
    X509_NAME_ENTRY* entry = X509_NAME_get_entry(subject, pos);
    if (!entry) return "";
    ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
    if (!data) return "";
    const unsigned char* str = ASN1_STRING_get0_data(data);
    if (!str) return "";
    return std::string(reinterpret_cast<const char*>(str), ASN1_STRING_length(data));
}

std::string DlParser::getOrganizationFromCert(X509* cert) {
    if (!cert) return "";
    X509_NAME* subject = X509_get_subject_name(cert);
    if (!subject) return "";
    int pos = X509_NAME_get_index_by_NID(subject, NID_organizationName, -1);
    if (pos < 0) return "";
    X509_NAME_ENTRY* entry = X509_NAME_get_entry(subject, pos);
    if (!entry) return "";
    ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
    if (!data) return "";
    const unsigned char* str = ASN1_STRING_get0_data(data);
    if (!str) return "";
    return std::string(reinterpret_cast<const char*>(str), ASN1_STRING_length(data));
}

std::string DlParser::classifyDeviationOid(const std::string& oid) {
    // OID prefix classification:
    // 2.23.136.1.1.7.1.x = CertOrKey
    // 2.23.136.1.1.7.2.x = LDS
    // 2.23.136.1.1.7.3.x = MRZ
    // 2.23.136.1.1.7.4.x = Chip
    if (oid.find("2.23.136.1.1.7.1") == 0) return "CertOrKey";
    if (oid.find("2.23.136.1.1.7.2") == 0) return "LDS";
    if (oid.find("2.23.136.1.1.7.3") == 0) return "MRZ";
    if (oid.find("2.23.136.1.1.7.4") == 0) return "Chip";
    return "Unknown";
}

std::string DlParser::oidToAlgorithmName(const std::string& oid) {
    // Common hash algorithm OIDs
    if (oid == "1.3.14.3.2.26") return "SHA-1";
    if (oid == "2.16.840.1.101.3.4.2.1") return "SHA-256";
    if (oid == "2.16.840.1.101.3.4.2.2") return "SHA-384";
    if (oid == "2.16.840.1.101.3.4.2.3") return "SHA-512";
    if (oid == "2.16.840.1.101.3.4.2.4") return "SHA-224";
    if (oid == "1.2.840.113549.2.5") return "MD5";
    // Return OID itself if not recognized
    return oid;
}

std::string DlParser::x509NameToString(X509_NAME* name) {
    if (!name) return "";
    BIO* bio = BIO_new(BIO_s_mem());
    if (!bio) return "";
    X509_NAME_print_ex(bio, name, 0, XN_FLAG_RFC2253);
    char* buf = nullptr;
    long len = BIO_get_mem_data(bio, &buf);
    std::string result;
    if (buf && len > 0) {
        result.assign(buf, len);
    }
    BIO_free(bio);
    return result;
}

} // namespace certificate_parser
} // namespace icao
