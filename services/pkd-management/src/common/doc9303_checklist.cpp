#include "doc9303_checklist.h"
#include "x509_metadata_extractor.h"

#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <openssl/asn1.h>
#include <openssl/objects.h>

#include <algorithm>
#include <sstream>

/**
 * @file doc9303_checklist.cpp
 * @brief ICAO Doc 9303 Compliance Checklist Implementation
 *
 * Reference: "Checks Against Doc9303 Applied to PKD Uploads" (docs/)
 * Covers certificate attributes checked as part of Doc9303 compliance:
 * - §1.1  Version (V3)
 * - §1.2  Serial Number (positive, max 20 octets)
 * - §1.3  Signature Algorithm OID match
 * - §1.4  Issuer (country code present)
 * - §1.8  Subject (country code, country match)
 * - §1.10 Unique Identifiers (must not be present)
 * - §1.11 Extensions (Key Usage, Basic Constraints, EKU, AKI, SKI, etc.)
 * - §2    Signature Algorithm match
 */

namespace common {

// ============================================================================
// JSON serialization
// ============================================================================

Json::Value Doc9303CheckItem::toJson() const {
    Json::Value j;
    j["id"] = id;
    j["category"] = category;
    j["label"] = label;
    j["status"] = status;
    j["message"] = message;
    j["requirement"] = requirement;
    return j;
}

Json::Value Doc9303ChecklistResult::toJson() const {
    Json::Value j;
    j["certificateType"] = certificateType;
    j["totalChecks"] = totalChecks;
    j["passCount"] = passCount;
    j["failCount"] = failCount;
    j["warningCount"] = warningCount;
    j["naCount"] = naCount;
    j["overallStatus"] = overallStatus;

    Json::Value itemsArr(Json::arrayValue);
    for (const auto& item : items) {
        itemsArr.append(item.toJson());
    }
    j["items"] = itemsArr;
    return j;
}

// ============================================================================
// Helper: add check item to result
// ============================================================================

static void addItem(Doc9303ChecklistResult& result, Doc9303CheckItem item) {
    if (item.status == "PASS") result.passCount++;
    else if (item.status == "FAIL") result.failCount++;
    else if (item.status == "WARNING") result.warningCount++;
    else result.naCount++;

    result.totalChecks++;
    result.items.push_back(std::move(item));
}

// ============================================================================
// Helper: extract country code from X509_NAME
// ============================================================================

static std::string extractCountryFromName(X509_NAME* name) {
    if (!name) return "";
    int idx = X509_NAME_get_index_by_NID(name, NID_countryName, -1);
    if (idx < 0) return "";
    X509_NAME_ENTRY* entry = X509_NAME_get_entry(name, idx);
    if (!entry) return "";
    ASN1_STRING* data = X509_NAME_ENTRY_get_data(entry);
    if (!data) return "";
    unsigned char* utf8 = nullptr;
    int len = ASN1_STRING_to_UTF8(&utf8, data);
    if (len <= 0 || !utf8) return "";
    std::string country(reinterpret_cast<char*>(utf8), len);
    OPENSSL_free(utf8);
    return country;
}

// ============================================================================
// Helper: check if extension exists and get criticality
// ============================================================================

struct ExtInfo {
    bool exists = false;
    bool critical = false;
};

static ExtInfo getExtensionInfo(X509* cert, int nid) {
    ExtInfo info;
    int idx = X509_get_ext_by_NID(cert, nid, -1);
    if (idx >= 0) {
        info.exists = true;
        X509_EXTENSION* ext = X509_get_ext(cert, idx);
        if (ext) {
            info.critical = X509_EXTENSION_get_critical(ext) == 1;
        }
    }
    return info;
}

// ============================================================================
// Helper: check Netscape extensions
// ============================================================================

static bool hasNetscapeExtensions(X509* cert) {
    // Netscape Cert Type OID: 2.16.840.1.113730.1.1
    int nid = OBJ_txt2nid("2.16.840.1.113730.1.1");
    if (nid != NID_undef) {
        int idx = X509_get_ext_by_NID(cert, nid, -1);
        if (idx >= 0) return true;
    }
    // Netscape Comment OID: 2.16.840.1.113730.1.13
    nid = OBJ_txt2nid("2.16.840.1.113730.1.13");
    if (nid != NID_undef) {
        int idx = X509_get_ext_by_NID(cert, nid, -1);
        if (idx >= 0) return true;
    }
    return false;
}

// ============================================================================
// Helper: count unknown critical extensions
// ============================================================================

static std::vector<std::string> getUnknownCriticalExtensions(X509* cert) {
    std::vector<std::string> unknown;
    // Known extension NIDs for ICAO certificates
    static const int knownNids[] = {
        NID_authority_key_identifier,
        NID_subject_key_identifier,
        NID_key_usage,
        NID_basic_constraints,
        NID_certificate_policies,
        NID_ext_key_usage,
        NID_crl_distribution_points,
        NID_info_access,                // Authority Information Access (OCSP)
        NID_subject_alt_name,
        NID_issuer_alt_name,
        NID_policy_mappings,
        NID_name_constraints,
        NID_policy_constraints,
        NID_inhibit_any_policy,
        NID_subject_directory_attributes,
        NID_private_key_usage_period,
        NID_freshest_crl,
    };

    int extCount = X509_get_ext_count(cert);
    for (int i = 0; i < extCount; i++) {
        X509_EXTENSION* ext = X509_get_ext(cert, i);
        if (!ext) continue;
        if (X509_EXTENSION_get_critical(ext) != 1) continue;

        const ASN1_OBJECT* obj = X509_EXTENSION_get_object(ext);
        if (!obj) continue;
        int nid = OBJ_obj2nid(obj);

        bool known = false;
        for (int knownNid : knownNids) {
            if (nid == knownNid) { known = true; break; }
        }

        if (!known) {
            char oidBuf[256];
            OBJ_obj2txt(oidBuf, sizeof(oidBuf), obj, 1);
            unknown.push_back(std::string(oidBuf));
        }
    }
    return unknown;
}

// ============================================================================
// Main checklist function
// ============================================================================

Doc9303ChecklistResult runDoc9303Checklist(X509* cert, const std::string& certType) {
    Doc9303ChecklistResult result;
    result.certificateType = certType;

    if (!cert) {
        result.overallStatus = "NON_CONFORMANT";
        addItem(result, {"error", "오류", "인증서 파싱", "FAIL",
                         "NULL 인증서 포인터", ""});
        return result;
    }

    // Extract metadata for reuse
    x509::CertificateMetadata meta = x509::extractMetadata(cert);
    bool isSelfSigned = meta.isSelfSigned;
    bool isLinkCert = meta.isCA && !isSelfSigned &&
                      (certType == "CSCA");  // Link cert = CA + not self-signed CSCA

    // ========================================================================
    // 1. 기본 필드 (Basic Fields)
    // ========================================================================

    // 1.1 Version V3
    {
        int version = X509_get_version(cert);  // 0=v1, 1=v2, 2=v3
        addItem(result, {"version_v3", "기본", "버전 V3",
                         version == 2 ? "PASS" : "FAIL",
                         version == 2 ? "V3" : "V" + std::to_string(version + 1),
                         "인증서 버전은 반드시 V3이어야 합니다"});
    }

    // 1.2 Serial Number - positive
    {
        const ASN1_INTEGER* serial = X509_get0_serialNumber(cert);
        bool positive = true;
        std::string msg;
        if (serial) {
            // ASN1_INTEGER stores sign information
            // If the high bit of first byte is set in DER, it should have a leading 0x00
            const unsigned char* data = ASN1_STRING_get0_data(serial);
            int len = ASN1_STRING_length(serial);
            if (len > 0 && (data[0] & 0x80)) {
                positive = false;
                msg = "일련번호가 음수입니다";
            } else {
                msg = std::to_string(len) + "바이트";
            }
        } else {
            positive = false;
            msg = "일련번호가 없습니다";
        }
        addItem(result, {"serial_positive", "기본", "일련번호 양수",
                         positive ? "PASS" : "FAIL", msg,
                         "일련번호는 양수여야 합니다 (2의 보수 인코딩)"});
    }

    // 1.2 Serial Number - max 20 octets
    {
        const ASN1_INTEGER* serial = X509_get0_serialNumber(cert);
        bool ok = true;
        std::string msg;
        if (serial) {
            int len = ASN1_STRING_length(serial);
            ok = (len <= 20);
            msg = std::to_string(len) + "바이트" + (ok ? "" : " (최대 20바이트 초과)");
        } else {
            ok = false;
            msg = "일련번호가 없습니다";
        }
        addItem(result, {"serial_max_20_octets", "기본", "일련번호 최대 20옥텟",
                         ok ? "PASS" : "FAIL", msg,
                         "일련번호는 최대 20옥텟이어야 합니다"});
    }

    // ========================================================================
    // 2. 서명 알고리즘 (Signature)
    // ========================================================================

    // Signature OID match (TBS signatureAlgorithm == outer signatureAlgorithm)
    {
        const X509_ALGOR* tbsSigAlg = nullptr;
        const X509_ALGOR* outerSigAlg = nullptr;
        X509_get0_signature(nullptr, &outerSigAlg, cert);

        // TBS signature algorithm
        const ASN1_BIT_STRING* unused = nullptr;
        X509_get0_signature(&unused, &outerSigAlg, cert);

        // Get TBS sig alg from cert info
        tbsSigAlg = X509_get0_tbs_sigalg(cert);

        bool match = false;
        std::string msg;
        if (tbsSigAlg && outerSigAlg) {
            match = (OBJ_cmp(tbsSigAlg->algorithm, outerSigAlg->algorithm) == 0);
            char tbsOid[128], outerOid[128];
            OBJ_obj2txt(tbsOid, sizeof(tbsOid), tbsSigAlg->algorithm, 1);
            OBJ_obj2txt(outerOid, sizeof(outerOid), outerSigAlg->algorithm, 1);
            if (match) {
                msg = std::string(tbsOid);
            } else {
                msg = "TBS: " + std::string(tbsOid) + " / 외부: " + std::string(outerOid);
            }
        } else {
            msg = "서명 알고리즘 정보를 추출할 수 없습니다";
        }
        addItem(result, {"sig_algo_match", "서명", "서명 알고리즘 OID 일치",
                         match ? "PASS" : "FAIL", msg,
                         "TBSCertificate의 signatureAlgorithm과 외부 signatureAlgorithm OID가 일치해야 합니다"});
    }

    // ICAO approved signature algorithm
    {
        std::string sigAlg = meta.signatureAlgorithm;
        std::string hashAlg = meta.signatureHashAlgorithm;
        std::string lowerHash = hashAlg;
        std::transform(lowerHash.begin(), lowerHash.end(), lowerHash.begin(), ::tolower);

        bool approvedHash = (lowerHash.find("sha256") != std::string::npos ||
                             lowerHash.find("sha-256") != std::string::npos ||
                             lowerHash.find("sha384") != std::string::npos ||
                             lowerHash.find("sha-384") != std::string::npos ||
                             lowerHash.find("sha512") != std::string::npos ||
                             lowerHash.find("sha-512") != std::string::npos);
        bool approvedPubKey = (meta.publicKeyAlgorithm == "RSA" ||
                               meta.publicKeyAlgorithm == "ECDSA");

        std::string status = (approvedHash && approvedPubKey) ? "PASS" : "FAIL";
        std::string msg = sigAlg + " (" + meta.publicKeyAlgorithm + ")";
        if (!approvedHash) msg += " — 해시 알고리즘 미승인";
        if (!approvedPubKey) msg += " — 공개키 알고리즘 미승인";

        addItem(result, {"sig_algo_approved", "서명", "ICAO 승인 서명 알고리즘",
                         status, msg,
                         "SHA-256/384/512 + RSA 또는 ECDSA만 허용됩니다"});
    }

    // ========================================================================
    // 3. 발급자/주체 (Issuer/Subject)
    // ========================================================================

    // Issuer country code present
    {
        std::string issuerCountry = extractCountryFromName(X509_get_issuer_name(cert));
        addItem(result, {"issuer_country_present", "발급자", "발급자 국가코드 존재",
                         !issuerCountry.empty() ? "PASS" : "FAIL",
                         !issuerCountry.empty() ? issuerCountry : "국가코드(C) 없음",
                         "발급자 DN에 국가코드(C)가 존재해야 합니다"});
    }

    // Subject country code present
    {
        std::string subjectCountry = extractCountryFromName(X509_get_subject_name(cert));
        addItem(result, {"subject_country_present", "주체", "주체 국가코드 존재",
                         !subjectCountry.empty() ? "PASS" : "FAIL",
                         !subjectCountry.empty() ? subjectCountry : "국가코드(C) 없음",
                         "주체 DN에 국가코드(C)가 존재해야 합니다"});
    }

    // Subject/Issuer country match (DSC, MLSC only)
    if (certType == "DSC" || certType == "DSC_NC" || certType == "MLSC") {
        std::string subjectCountry = extractCountryFromName(X509_get_subject_name(cert));
        std::string issuerCountry = extractCountryFromName(X509_get_issuer_name(cert));
        bool match = (!subjectCountry.empty() && !issuerCountry.empty() &&
                      subjectCountry == issuerCountry);
        std::string msg = match ? subjectCountry
                                : ("주체: " + (subjectCountry.empty() ? "없음" : subjectCountry) +
                                   " / 발급자: " + (issuerCountry.empty() ? "없음" : issuerCountry));
        addItem(result, {"subject_issuer_country_match", "주체", "주체/발급자 국가코드 일치",
                         match ? "PASS" : "FAIL", msg,
                         "주체와 발급자의 국가코드가 일치해야 합니다"});
    }

    // ========================================================================
    // 4. 고유 식별자 (Unique Identifiers)
    // ========================================================================
    {
        const ASN1_BIT_STRING* issuerUid = nullptr;
        const ASN1_BIT_STRING* subjectUid = nullptr;
        X509_get0_uids(cert, &issuerUid, &subjectUid);
        bool absent = (!issuerUid && !subjectUid);
        std::string msg = absent ? "미포함" : "고유 식별자가 존재합니다";
        addItem(result, {"unique_id_absent", "고유식별자", "Unique Identifiers 미포함",
                         absent ? "PASS" : "FAIL", msg,
                         "전자여권 인증서에 Unique Identifiers를 사용하면 안 됩니다"});
    }

    // ========================================================================
    // 5. Key Usage
    // ========================================================================

    // Key Usage extension present
    {
        auto kuInfo = getExtensionInfo(cert, NID_key_usage);
        addItem(result, {"key_usage_present", "Key Usage", "Key Usage 확장 존재",
                         kuInfo.exists ? "PASS" : "FAIL",
                         kuInfo.exists ? "존재" : "Key Usage 확장이 없습니다",
                         "Key Usage 확장은 필수입니다"});
    }

    // Key Usage critical
    {
        auto kuInfo = getExtensionInfo(cert, NID_key_usage);
        if (kuInfo.exists) {
            addItem(result, {"key_usage_critical", "Key Usage", "Key Usage Critical 설정",
                             kuInfo.critical ? "PASS" : "FAIL",
                             kuInfo.critical ? "Critical" : "Non-critical",
                             "Key Usage 확장은 반드시 Critical이어야 합니다"});
        } else {
            addItem(result, {"key_usage_critical", "Key Usage", "Key Usage Critical 설정",
                             "NA", "Key Usage 확장이 없어 확인 불가", ""});
        }
    }

    // Key Usage correct values
    {
        const auto& ku = meta.keyUsage;
        bool correct = false;
        std::string expected;
        std::string actual;

        // Build actual key usage string
        for (size_t i = 0; i < ku.size(); i++) {
            if (i > 0) actual += ", ";
            actual += ku[i];
        }
        if (actual.empty()) actual = "없음";

        if (certType == "CSCA") {
            // CSCA: keyCertSign + cRLSign only
            expected = "keyCertSign, cRLSign";
            bool hasKeyCertSign = std::find(ku.begin(), ku.end(), "keyCertSign") != ku.end();
            bool hasCrlSign = std::find(ku.begin(), ku.end(), "cRLSign") != ku.end();
            correct = hasKeyCertSign && hasCrlSign;
        } else if (certType == "DSC" || certType == "DSC_NC") {
            // DSC: digitalSignature only
            expected = "digitalSignature";
            bool hasDigSig = std::find(ku.begin(), ku.end(), "digitalSignature") != ku.end();
            correct = hasDigSig;
        } else if (certType == "MLSC") {
            // MLSC: digitalSignature (used for CMS signing)
            expected = "digitalSignature";
            bool hasDigSig = std::find(ku.begin(), ku.end(), "digitalSignature") != ku.end();
            correct = hasDigSig;
        }

        addItem(result, {"key_usage_correct", "Key Usage",
                         certType == "CSCA" ? "keyCertSign + cRLSign" :
                         "digitalSignature",
                         correct ? "PASS" : "FAIL",
                         "실제: " + actual + (correct ? "" : " — 필요: " + expected),
                         certType == "CSCA"
                             ? "CSCA/링크 인증서: keyCertSign + cRLSign만 허용"
                             : "DSC/MLSC: digitalSignature만 허용"});
    }

    // ========================================================================
    // 6. 기본 제약 (Basic Constraints)
    // ========================================================================

    if (certType == "CSCA") {
        auto bcInfo = getExtensionInfo(cert, NID_basic_constraints);

        // Basic Constraints present
        addItem(result, {"basic_constraints_present", "기본 제약", "Basic Constraints 존재",
                         bcInfo.exists ? "PASS" : "FAIL",
                         bcInfo.exists ? "존재" : "Basic Constraints 확장이 없습니다",
                         "CSCA/링크 인증서에 Basic Constraints는 필수입니다"});

        // Basic Constraints critical
        if (bcInfo.exists) {
            addItem(result, {"basic_constraints_critical", "기본 제약", "Basic Constraints Critical 설정",
                             bcInfo.critical ? "PASS" : "FAIL",
                             bcInfo.critical ? "Critical" : "Non-critical",
                             "CSCA/링크 인증서에서 Basic Constraints는 반드시 Critical이어야 합니다"});
        }

        // CA = TRUE
        addItem(result, {"basic_constraints_ca_true", "기본 제약", "CA = TRUE",
                         meta.isCA ? "PASS" : "FAIL",
                         meta.isCA ? "CA=TRUE" : "CA=FALSE",
                         "CSCA/링크 인증서는 CA=TRUE여야 합니다"});

        // pathLength = 0
        {
            bool pathLenOk = meta.pathLenConstraint.has_value() &&
                             meta.pathLenConstraint.value() == 0;
            std::string msg;
            if (meta.pathLenConstraint.has_value()) {
                msg = "pathLength=" + std::to_string(meta.pathLenConstraint.value());
            } else {
                msg = "pathLength 제한 없음";
            }
            addItem(result, {"basic_constraints_pathlen_zero", "기본 제약",
                             "pathLength = 0",
                             pathLenOk ? "PASS" : "WARNING", msg,
                             "CSCA의 pathLength는 0이어야 합니다 (중간 CA 허용 안 함)"});
        }
    }

    if (certType == "DSC" || certType == "DSC_NC") {
        // DSC: CA must not be asserted
        addItem(result, {"basic_constraints_ca_false", "기본 제약", "CA 미설정",
                         !meta.isCA ? "PASS" : "FAIL",
                         !meta.isCA ? "CA=FALSE (정상)" : "CA=TRUE (DSC에 CA가 설정됨)",
                         "DSC는 CA가 설정되면 안 됩니다"});
    }

    // ========================================================================
    // 7. 확장 키 용도 (Extended Key Usage)
    // ========================================================================

    if (certType == "CSCA") {
        // CSCA: EKU must not be present
        auto ekuInfo = getExtensionInfo(cert, NID_ext_key_usage);
        addItem(result, {"eku_absent", "확장 키 용도", "Extended Key Usage 미포함",
                         !ekuInfo.exists ? "PASS" : "FAIL",
                         !ekuInfo.exists ? "미포함 (정상)" : "EKU가 존재합니다 — CSCA에서 금지",
                         "CSCA에 Extended Key Usage를 사용하면 안 됩니다"});
    }

    if (certType == "DSC" || certType == "DSC_NC") {
        // DSC: EKU must not be present
        auto ekuInfo = getExtensionInfo(cert, NID_ext_key_usage);
        addItem(result, {"eku_absent", "확장 키 용도", "Extended Key Usage 미포함",
                         !ekuInfo.exists ? "PASS" : "FAIL",
                         !ekuInfo.exists ? "미포함 (정상)" : "EKU가 존재합니다 — DSC에서 금지",
                         "DSC에 Extended Key Usage를 사용하면 안 됩니다"});
    }

    if (certType == "MLSC") {
        // MLSC: EKU must be present, critical, with OID 2.23.136.1.1.3
        auto ekuInfo = getExtensionInfo(cert, NID_ext_key_usage);
        if (ekuInfo.exists) {
            // Check critical
            addItem(result, {"eku_mlsc_critical", "확장 키 용도", "EKU Critical 설정",
                             ekuInfo.critical ? "PASS" : "FAIL",
                             ekuInfo.critical ? "Critical" : "Non-critical",
                             "MLSC의 EKU는 반드시 Critical이어야 합니다"});

            // Check for id-icao-mrtd-security-masterListSigningKey (2.23.136.1.1.3)
            bool hasCorrectOid = false;
            EXTENDED_KEY_USAGE* eku = (EXTENDED_KEY_USAGE*)X509_get_ext_d2i(
                cert, NID_ext_key_usage, nullptr, nullptr);
            if (eku) {
                for (int i = 0; i < sk_ASN1_OBJECT_num(eku); i++) {
                    ASN1_OBJECT* obj = sk_ASN1_OBJECT_value(eku, i);
                    char oidBuf[128];
                    OBJ_obj2txt(oidBuf, sizeof(oidBuf), obj, 1);
                    if (std::string(oidBuf) == "2.23.136.1.1.3") {
                        hasCorrectOid = true;
                        break;
                    }
                }
                EXTENDED_KEY_USAGE_free(eku);
            }
            addItem(result, {"eku_mlsc_present", "확장 키 용도",
                             "MLSC EKU OID (2.23.136.1.1.3)",
                             hasCorrectOid ? "PASS" : "FAIL",
                             hasCorrectOid ? "id-icao-mrtd-security-masterListSigningKey"
                                           : "OID 2.23.136.1.1.3이 없습니다",
                             "MLSC는 EKU에 OID 2.23.136.1.1.3을 포함해야 합니다"});
        } else {
            addItem(result, {"eku_mlsc_present", "확장 키 용도",
                             "MLSC EKU 존재",
                             "FAIL", "EKU 확장이 없습니다",
                             "MLSC에는 Extended Key Usage가 필수입니다"});
        }
    }

    // ========================================================================
    // 8. 확장 (Extensions)
    // ========================================================================

    // Authority Key Identifier
    {
        auto akiInfo = getExtensionInfo(cert, NID_authority_key_identifier);
        bool required = !isSelfSigned;  // Mandatory if issuer != subject
        if (required) {
            addItem(result, {"aki_present", "확장", "Authority Key Identifier 존재",
                             akiInfo.exists ? "PASS" : "FAIL",
                             akiInfo.exists ? "존재" : "AKI가 없습니다 (발급자≠주체 시 필수)",
                             "발급자와 주체가 다른 경우 AKI는 필수입니다"});
        } else {
            // Self-signed: AKI is recommended but not strictly required
            addItem(result, {"aki_present", "확장", "Authority Key Identifier 존재",
                             akiInfo.exists ? "PASS" : "WARNING",
                             akiInfo.exists ? "존재" : "AKI 없음 (자체서명 — 권고사항)",
                             "자체서명 인증서에서 AKI는 권고사항입니다"});
        }
    }

    // AKI non-critical
    {
        auto akiInfo = getExtensionInfo(cert, NID_authority_key_identifier);
        if (akiInfo.exists) {
            addItem(result, {"aki_non_critical", "확장", "AKI Non-critical 설정",
                             !akiInfo.critical ? "PASS" : "FAIL",
                             !akiInfo.critical ? "Non-critical (정상)" : "Critical (위반)",
                             "AKI는 Non-critical이어야 합니다"});
        } else {
            addItem(result, {"aki_non_critical", "확장", "AKI Non-critical 설정",
                             "NA", "AKI 확장 없음", ""});
        }
    }

    // Subject Key Identifier (CSCA only: mandatory for self-signed or link cert)
    if (certType == "CSCA") {
        auto skiInfo = getExtensionInfo(cert, NID_subject_key_identifier);
        bool required = isSelfSigned || isLinkCert;
        if (required) {
            addItem(result, {"ski_present", "확장", "Subject Key Identifier 존재",
                             skiInfo.exists ? "PASS" : "FAIL",
                             skiInfo.exists ? "존재" : "SKI가 없습니다 (자체서명/링크 시 필수)",
                             "자체서명 또는 링크 인증서에서 SKI는 필수입니다"});
        } else {
            addItem(result, {"ski_present", "확장", "Subject Key Identifier 존재",
                             skiInfo.exists ? "PASS" : "WARNING",
                             skiInfo.exists ? "존재" : "SKI 없음",
                             "SKI 존재를 권장합니다"});
        }

        // SKI non-critical
        if (skiInfo.exists) {
            addItem(result, {"ski_non_critical", "확장", "SKI Non-critical 설정",
                             !skiInfo.critical ? "PASS" : "FAIL",
                             !skiInfo.critical ? "Non-critical (정상)" : "Critical (위반)",
                             "SKI는 Non-critical이어야 합니다"});
        }
    }

    // Certificate Policies non-critical
    {
        auto cpInfo = getExtensionInfo(cert, NID_certificate_policies);
        if (cpInfo.exists) {
            addItem(result, {"cert_policies_non_critical", "확장",
                             "Certificate Policies Non-critical 설정",
                             !cpInfo.critical ? "PASS" : "FAIL",
                             !cpInfo.critical ? "Non-critical (정상)" : "Critical (위반)",
                             "Certificate Policies는 Non-critical이어야 합니다"});
        } else {
            addItem(result, {"cert_policies_non_critical", "확장",
                             "Certificate Policies Non-critical 설정",
                             "NA", "Certificate Policies 확장 없음 (선택 사항)", ""});
        }
    }

    // No Netscape Extensions
    {
        bool hasNetscape = hasNetscapeExtensions(cert);
        addItem(result, {"no_netscape_extensions", "확장", "Netscape Extensions 미포함",
                         !hasNetscape ? "PASS" : "FAIL",
                         !hasNetscape ? "미포함 (정상)" : "Netscape 확장이 존재합니다",
                         "Netscape Extensions는 전자여권 인증서에서 금지됩니다"});
    }

    // No unknown critical extensions
    {
        auto unknown = getUnknownCriticalExtensions(cert);
        if (unknown.empty()) {
            addItem(result, {"no_unknown_critical_ext", "확장",
                             "알 수 없는 Critical 확장 없음",
                             "PASS", "미발견", ""});
        } else {
            std::string oids;
            for (size_t i = 0; i < unknown.size(); i++) {
                if (i > 0) oids += ", ";
                oids += unknown[i];
            }
            addItem(result, {"no_unknown_critical_ext", "확장",
                             "알 수 없는 Critical 확장 없음",
                             "FAIL", "알 수 없는 Critical 확장: " + oids,
                             "인식할 수 없는 확장이 Critical로 설정되면 안 됩니다"});
        }
    }

    // ========================================================================
    // 9. 키 크기 (Key Size)
    // ========================================================================

    // Minimum key size
    {
        int keySize = meta.publicKeySize;
        std::string pubAlg = meta.publicKeyAlgorithm;
        bool ok = true;
        std::string msg;

        if (pubAlg == "RSA") {
            ok = (keySize >= 2048);
            msg = "RSA " + std::to_string(keySize) + "비트" +
                  (ok ? "" : " (최소 2048비트 미만)");
        } else if (pubAlg == "ECDSA") {
            ok = (keySize >= 224);
            msg = "ECDSA " + std::to_string(keySize) + "비트";
            if (meta.publicKeyCurve.has_value()) {
                msg += " (" + meta.publicKeyCurve.value() + ")";
            }
            if (!ok) msg += " (최소 224비트 미만)";
        } else {
            msg = pubAlg + " " + std::to_string(keySize) + "비트";
        }

        addItem(result, {"key_size_minimum", "키 크기", "최소 키 크기 충족",
                         ok ? "PASS" : "FAIL", msg,
                         "RSA: 최소 2048비트, ECDSA: 최소 224비트"});
    }

    // Recommended key size
    {
        int keySize = meta.publicKeySize;
        std::string pubAlg = meta.publicKeyAlgorithm;
        bool recommended = true;
        std::string msg;

        if (pubAlg == "RSA") {
            recommended = (keySize >= 3072);
            msg = "RSA " + std::to_string(keySize) + "비트" +
                  (recommended ? " (권고 충족)" : " (3072비트 이상 권고)");
        } else if (pubAlg == "ECDSA") {
            // ICAO approved curves: P-256 or higher
            bool approvedCurve = false;
            if (meta.publicKeyCurve.has_value()) {
                const auto& curve = meta.publicKeyCurve.value();
                approvedCurve = (curve == "prime256v1" || curve == "secp256r1" ||
                                 curve == "secp384r1" || curve == "secp521r1");
            }
            recommended = approvedCurve || keySize >= 256;
            msg = "ECDSA " + std::to_string(keySize) + "비트";
            if (meta.publicKeyCurve.has_value()) {
                msg += " (" + meta.publicKeyCurve.value() + ")";
            }
            if (!recommended) msg += " (P-256/384/521 권고)";
        } else {
            msg = pubAlg + " " + std::to_string(keySize) + "비트";
            recommended = false;
        }

        addItem(result, {"key_size_recommended", "키 크기", "권고 키 크기 충족",
                         recommended ? "PASS" : "WARNING", msg,
                         "RSA: 3072비트 이상, ECDSA: P-256/384/521 커브 권고"});
    }

    // ========================================================================
    // Final Assessment
    // ========================================================================

    if (result.failCount > 0) {
        result.overallStatus = "NON_CONFORMANT";
    } else if (result.warningCount > 0) {
        result.overallStatus = "WARNING";
    } else {
        result.overallStatus = "CONFORMANT";
    }

    return result;
}

} // namespace common
