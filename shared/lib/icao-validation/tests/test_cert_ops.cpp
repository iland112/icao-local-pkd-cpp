/**
 * @file test_cert_ops.cpp
 * @brief Unit tests for cert_ops — pure X.509 operations
 *
 * Tests idempotency: identical inputs must produce identical outputs across
 * repeated invocations (no hidden state, no side effects).
 */

#include <gtest/gtest.h>
#include <icao/validation/cert_ops.h>
#include "test_helpers.h"

using namespace icao::validation;
using namespace test_helpers;

class CertOpsTest : public ::testing::Test {
protected:
    UniqueKey rsaKey_;
    UniqueKey ecKey_;
    UniqueCert rootCa_;
    UniqueCert dsc_;

    void SetUp() override {
        rsaKey_ = generateRsaKey(2048);
        ecKey_ = generateEcKey();

        auto dscKey = generateRsaKey(2048);
        rootCa_ = createRootCa(rsaKey_.get(), "Test Root CSCA");
        dsc_ = createDsc(dscKey.get(), rsaKey_.get(), rootCa_.get(), "Test DSC");
    }
};

// ============================================================================
// verifyCertificateSignature
// ============================================================================

TEST_F(CertOpsTest, VerifySignature_ValidChain) {
    EXPECT_TRUE(verifyCertificateSignature(dsc_.get(), rootCa_.get()));
}

TEST_F(CertOpsTest, VerifySignature_SelfSigned) {
    EXPECT_TRUE(verifyCertificateSignature(rootCa_.get(), rootCa_.get()));
}

TEST_F(CertOpsTest, VerifySignature_WrongIssuer) {
    auto otherKey = generateRsaKey(2048);
    auto otherCa = createRootCa(otherKey.get(), "Other CSCA");
    EXPECT_FALSE(verifyCertificateSignature(dsc_.get(), otherCa.get()));
}

TEST_F(CertOpsTest, VerifySignature_NullCert) {
    EXPECT_FALSE(verifyCertificateSignature(nullptr, rootCa_.get()));
    EXPECT_FALSE(verifyCertificateSignature(dsc_.get(), nullptr));
    EXPECT_FALSE(verifyCertificateSignature(nullptr, nullptr));
}

// ============================================================================
// isCertificateExpired / isCertificateNotYetValid
// ============================================================================

TEST_F(CertOpsTest, Expired_ValidCert) {
    EXPECT_FALSE(isCertificateExpired(rootCa_.get()));
}

TEST_F(CertOpsTest, Expired_ExpiredCert) {
    auto expired = createExpiredCert(rsaKey_.get());
    EXPECT_TRUE(isCertificateExpired(expired.get()));
}

TEST_F(CertOpsTest, Expired_NullReturnsTrue) {
    EXPECT_TRUE(isCertificateExpired(nullptr));
}

TEST_F(CertOpsTest, NotYetValid_ValidCert) {
    EXPECT_FALSE(isCertificateNotYetValid(rootCa_.get()));
}

TEST_F(CertOpsTest, NotYetValid_FutureCert) {
    auto future = createFutureCert(rsaKey_.get());
    EXPECT_TRUE(isCertificateNotYetValid(future.get()));
}

TEST_F(CertOpsTest, NotYetValid_NullReturnsTrue) {
    EXPECT_TRUE(isCertificateNotYetValid(nullptr));
}

// ============================================================================
// isSelfSigned
// ============================================================================

TEST_F(CertOpsTest, SelfSigned_RootCa) {
    EXPECT_TRUE(isSelfSigned(rootCa_.get()));
}

TEST_F(CertOpsTest, SelfSigned_Dsc) {
    EXPECT_FALSE(isSelfSigned(dsc_.get()));
}

TEST_F(CertOpsTest, SelfSigned_NullReturnsFalse) {
    EXPECT_FALSE(isSelfSigned(nullptr));
}

// ============================================================================
// isLinkCertificate
// ============================================================================

TEST_F(CertOpsTest, LinkCert_RootCaIsNotLink) {
    // Root CA is self-signed, so NOT a link certificate
    EXPECT_FALSE(isLinkCertificate(rootCa_.get()));
}

TEST_F(CertOpsTest, LinkCert_DscIsNotLink) {
    // DSC has no CA:TRUE
    EXPECT_FALSE(isLinkCertificate(dsc_.get()));
}

TEST_F(CertOpsTest, LinkCert_ActualLinkCert) {
    auto linkKey = generateRsaKey(2048);
    auto link = createLinkCert(linkKey.get(), rsaKey_.get(), rootCa_.get());
    EXPECT_TRUE(isLinkCertificate(link.get()));
}

TEST_F(CertOpsTest, LinkCert_NullReturnsFalse) {
    EXPECT_FALSE(isLinkCertificate(nullptr));
}

// ============================================================================
// getSubjectDn / getIssuerDn
// ============================================================================

TEST_F(CertOpsTest, SubjectDn_ContainsCountryAndCn) {
    std::string dn = getSubjectDn(rootCa_.get());
    EXPECT_NE(dn.find("KR"), std::string::npos);
    EXPECT_NE(dn.find("Test Root CSCA"), std::string::npos);
}

TEST_F(CertOpsTest, IssuerDn_DscMatchesCascaSubject) {
    std::string dscIssuer = getIssuerDn(dsc_.get());
    std::string cscaSubject = getSubjectDn(rootCa_.get());
    EXPECT_EQ(dscIssuer, cscaSubject);
}

TEST_F(CertOpsTest, DnExtraction_NullReturnsEmpty) {
    EXPECT_EQ(getSubjectDn(nullptr), "");
    EXPECT_EQ(getIssuerDn(nullptr), "");
}

// ============================================================================
// getCertificateFingerprint
// ============================================================================

TEST_F(CertOpsTest, Fingerprint_Is64CharHex) {
    std::string fp = getCertificateFingerprint(rootCa_.get());
    EXPECT_EQ(fp.size(), 64u);  // SHA-256 = 32 bytes = 64 hex chars
    for (char c : fp) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}

TEST_F(CertOpsTest, Fingerprint_DifferentCertsDifferent) {
    std::string fp1 = getCertificateFingerprint(rootCa_.get());
    std::string fp2 = getCertificateFingerprint(dsc_.get());
    EXPECT_NE(fp1, fp2);
}

TEST_F(CertOpsTest, Fingerprint_NullReturnsEmpty) {
    EXPECT_EQ(getCertificateFingerprint(nullptr), "");
}

// ============================================================================
// normalizeDnForComparison
// ============================================================================

TEST_F(CertOpsTest, NormalizeDn_SlashFormat) {
    std::string normalized = normalizeDnForComparison("/C=KR/O=Gov/CN=Test");
    EXPECT_FALSE(normalized.empty());
}

TEST_F(CertOpsTest, NormalizeDn_CommaFormat) {
    std::string normalized = normalizeDnForComparison("CN=Test,O=Gov,C=KR");
    EXPECT_FALSE(normalized.empty());
}

TEST_F(CertOpsTest, NormalizeDn_FormatIndependent) {
    // Same DN in both formats should normalize to same string
    std::string slash = normalizeDnForComparison("/C=KR/O=Gov/CN=Test");
    std::string comma = normalizeDnForComparison("CN=Test,O=Gov,C=KR");
    EXPECT_EQ(slash, comma);
}

TEST_F(CertOpsTest, NormalizeDn_CaseInsensitive) {
    std::string lower = normalizeDnForComparison("/C=kr/O=gov/CN=test");
    std::string upper = normalizeDnForComparison("/C=KR/O=GOV/CN=TEST");
    EXPECT_EQ(lower, upper);
}

TEST_F(CertOpsTest, NormalizeDn_EmptyReturnsEmpty) {
    EXPECT_EQ(normalizeDnForComparison(""), "");
}

// ============================================================================
// extractDnAttribute
// ============================================================================

TEST_F(CertOpsTest, ExtractAttr_CountryFromSlash) {
    EXPECT_EQ(extractDnAttribute("/C=KR/O=Gov/CN=Test", "C"), "kr");
}

TEST_F(CertOpsTest, ExtractAttr_CnFromComma) {
    EXPECT_EQ(extractDnAttribute("CN=Test CSCA,O=Gov,C=KR", "CN"), "test csca");
}

TEST_F(CertOpsTest, ExtractAttr_NotFound) {
    EXPECT_EQ(extractDnAttribute("/C=KR/CN=Test", "OU"), "");
}

TEST_F(CertOpsTest, ExtractAttr_CaseInsensitiveKey) {
    EXPECT_EQ(extractDnAttribute("/C=KR/cn=test", "CN"), "test");
}

TEST_F(CertOpsTest, ExtractAttr_EmptyDn) {
    EXPECT_EQ(extractDnAttribute("", "C"), "");
}

// ============================================================================
// asn1TimeToIso8601
// ============================================================================

TEST_F(CertOpsTest, Asn1Time_ValidTime) {
    const ASN1_TIME* notBefore = X509_get0_notBefore(rootCa_.get());
    std::string iso = asn1TimeToIso8601(notBefore);
    EXPECT_FALSE(iso.empty());
    EXPECT_NE(iso.find("T"), std::string::npos);   // Has time separator
    EXPECT_NE(iso.find("Z"), std::string::npos);    // Has UTC marker
}

TEST_F(CertOpsTest, Asn1Time_NullReturnsEmpty) {
    EXPECT_EQ(asn1TimeToIso8601(nullptr), "");
}

// ============================================================================
// IDEMPOTENCY TESTS — same input, same output, 100 iterations
// ============================================================================

TEST_F(CertOpsTest, Idempotency_VerifySignature) {
    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(verifyCertificateSignature(dsc_.get(), rootCa_.get()))
            << "Failed at iteration " << i;
    }
}

TEST_F(CertOpsTest, Idempotency_IsSelfSigned) {
    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(isSelfSigned(rootCa_.get())) << "Failed at iteration " << i;
        EXPECT_FALSE(isSelfSigned(dsc_.get())) << "Failed at iteration " << i;
    }
}

TEST_F(CertOpsTest, Idempotency_Fingerprint) {
    std::string first = getCertificateFingerprint(rootCa_.get());
    for (int i = 0; i < 100; i++) {
        EXPECT_EQ(getCertificateFingerprint(rootCa_.get()), first)
            << "Fingerprint changed at iteration " << i;
    }
}

TEST_F(CertOpsTest, Idempotency_SubjectDn) {
    std::string first = getSubjectDn(rootCa_.get());
    for (int i = 0; i < 100; i++) {
        EXPECT_EQ(getSubjectDn(rootCa_.get()), first)
            << "SubjectDn changed at iteration " << i;
    }
}

TEST_F(CertOpsTest, Idempotency_NormalizeDn) {
    std::string first = normalizeDnForComparison("/C=KR/O=Gov/CN=Test CSCA");
    for (int i = 0; i < 100; i++) {
        EXPECT_EQ(normalizeDnForComparison("/C=KR/O=Gov/CN=Test CSCA"), first)
            << "NormalizeDn changed at iteration " << i;
    }
}

TEST_F(CertOpsTest, Idempotency_ExtractDnAttribute) {
    std::string first = extractDnAttribute("/C=KR/O=Gov/CN=Test", "C");
    for (int i = 0; i < 100; i++) {
        EXPECT_EQ(extractDnAttribute("/C=KR/O=Gov/CN=Test", "C"), first)
            << "ExtractDnAttribute changed at iteration " << i;
    }
}

TEST_F(CertOpsTest, Idempotency_Expired) {
    for (int i = 0; i < 100; i++) {
        EXPECT_FALSE(isCertificateExpired(rootCa_.get())) << "Failed at iteration " << i;
    }
}
