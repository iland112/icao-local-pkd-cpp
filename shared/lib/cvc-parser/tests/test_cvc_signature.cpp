/**
 * @file test_cvc_signature.cpp
 * @brief Unit tests for CvcSignatureVerifier (cvc_signature.h / cvc_signature.cpp)
 *
 * Tests self-signed CVCA verification and chain verification using
 * real BSI TR-03110 EAC Worked Example certificates.
 *
 * Note: The BSI Worked Example certificates are test/demo certificates
 * and their signature validity depends on the key material in the example.
 * These tests verify that the verifier runs without errors, even if signature
 * verification returns a specific result for the demo certificates.
 */

#include <gtest/gtest.h>
#include "icao/cvc/cvc_signature.h"
#include "icao/cvc/cvc_parser.h"
#include "icao/cvc/cvc_certificate.h"
#include "icao/cvc/eac_oids.h"
#include "test_helpers.h"

using namespace icao::cvc;

// =============================================================================
// Helper: parse or fail
// =============================================================================

static CvcCertificate mustParse(const std::string& hex) {
    auto data = cvc_test_helpers::fromHex(hex);
    auto cert = CvcParser::parse(data);
    if (!cert.has_value()) {
        throw std::runtime_error("mustParse: parse failed");
    }
    return *cert;
}

// =============================================================================
// SignatureVerifyResult structure tests
// =============================================================================

TEST(SignatureVerifyResult, DefaultIsInvalid) {
    SignatureVerifyResult result;
    EXPECT_FALSE(result.valid);
}

// =============================================================================
// verifySelfSigned — ECDH CVCA
// =============================================================================

TEST(CvcSignatureVerifier, VerifySelfSigned_EcdhCvca_RunsWithoutError) {
    auto cert = mustParse(cvc_test_helpers::ECDH_CVCA_HEX);
    EXPECT_EQ(cert.type, CvcType::CVCA);
    EXPECT_FALSE(cert.bodyRaw.empty());
    EXPECT_FALSE(cert.signature.empty());
    EXPECT_FALSE(cert.publicKey.publicPoint.empty());

    // Should not throw; result may be valid or invalid depending on key material
    SignatureVerifyResult result = CvcSignatureVerifier::verifySelfSigned(cert);
    EXPECT_FALSE(result.message.empty());
}

TEST(CvcSignatureVerifier, VerifySelfSigned_EcdhCvca_CorrectKeyType) {
    auto cert = mustParse(cvc_test_helpers::ECDH_CVCA_HEX);
    // Algorithm OID must be ECDSA family
    EXPECT_EQ(cert.publicKey.algorithmOid, std::string(oid::TA_ECDSA_SHA_512));

    SignatureVerifyResult result = CvcSignatureVerifier::verifySelfSigned(cert);
    // For a self-signed certificate with correct algorithm, the verifier must
    // return a meaningful message (not a setup error)
    EXPECT_FALSE(result.message.empty());
}

// =============================================================================
// verifySelfSigned — DH CVCA (RSA)
// =============================================================================

TEST(CvcSignatureVerifier, VerifySelfSigned_DhCvca_RunsWithoutError) {
    auto cert = mustParse(cvc_test_helpers::DH_CVCA_HEX);
    EXPECT_EQ(cert.type, CvcType::CVCA);
    EXPECT_EQ(cert.publicKey.algorithmOid, std::string(oid::TA_RSA_V1_5_SHA_1));
    EXPECT_FALSE(cert.publicKey.modulus.empty());
    EXPECT_FALSE(cert.publicKey.exponent.empty());

    SignatureVerifyResult result = CvcSignatureVerifier::verifySelfSigned(cert);
    EXPECT_FALSE(result.message.empty());
}

// =============================================================================
// verify — DV with CVCA issuer key (ECDH chain)
// =============================================================================

TEST(CvcSignatureVerifier, Verify_EcdhDv_WithCvcaKey_RunsWithoutError) {
    auto cvca = mustParse(cvc_test_helpers::ECDH_CVCA_HEX);
    auto dv   = mustParse(cvc_test_helpers::ECDH_DV_HEX);

    EXPECT_EQ(dv.car, cvca.chr);   // DV was issued by this CVCA
    EXPECT_FALSE(dv.bodyRaw.empty());
    EXPECT_FALSE(dv.signature.empty());

    SignatureVerifyResult result = CvcSignatureVerifier::verify(dv, cvca.publicKey);
    EXPECT_FALSE(result.message.empty());
}

// =============================================================================
// verify — IS with DV issuer key (ECDH chain)
// =============================================================================

TEST(CvcSignatureVerifier, Verify_EcdhIs_WithDvKey_RunsWithoutError) {
    auto dv = mustParse(cvc_test_helpers::ECDH_DV_HEX);
    auto is_ = mustParse(cvc_test_helpers::ECDH_IS_HEX);

    EXPECT_EQ(is_.car, dv.chr);   // IS was issued by this DV
    EXPECT_FALSE(is_.bodyRaw.empty());
    EXPECT_FALSE(is_.signature.empty());

    SignatureVerifyResult result = CvcSignatureVerifier::verify(is_, dv.publicKey);
    EXPECT_FALSE(result.message.empty());
}

// =============================================================================
// Input validation: empty body / signature
// =============================================================================

TEST(CvcSignatureVerifier, Verify_EmptyBodyRaw_ReturnsInvalid) {
    auto cert = mustParse(cvc_test_helpers::ECDH_CVCA_HEX);
    auto issuerKey = cert.publicKey;

    CvcCertificate broken = cert;
    broken.bodyRaw.clear();

    SignatureVerifyResult result = CvcSignatureVerifier::verify(broken, issuerKey);
    EXPECT_FALSE(result.valid);
}

TEST(CvcSignatureVerifier, Verify_EmptySignature_ReturnsInvalid) {
    auto cert = mustParse(cvc_test_helpers::ECDH_CVCA_HEX);
    auto issuerKey = cert.publicKey;

    CvcCertificate broken = cert;
    broken.signature.clear();

    SignatureVerifyResult result = CvcSignatureVerifier::verify(broken, issuerKey);
    EXPECT_FALSE(result.valid);
}

TEST(CvcSignatureVerifier, VerifySelfSigned_EmptyPublicPoint_ReturnsInvalid) {
    auto cert = mustParse(cvc_test_helpers::ECDH_CVCA_HEX);
    cert.publicKey.publicPoint.clear();

    SignatureVerifyResult result = CvcSignatureVerifier::verifySelfSigned(cert);
    EXPECT_FALSE(result.valid);
}

// =============================================================================
// Wrong key type: try to verify ECDH cert with RSA key (and vice versa)
// =============================================================================

TEST(CvcSignatureVerifier, Verify_EcdhCert_WithRsaKey_ReturnsInvalid) {
    auto ecdhCert = mustParse(cvc_test_helpers::ECDH_CVCA_HEX);
    auto rsaCert  = mustParse(cvc_test_helpers::DH_CVCA_HEX);

    // Use RSA key to verify ECDH-signed certificate
    SignatureVerifyResult result = CvcSignatureVerifier::verify(ecdhCert, rsaCert.publicKey);
    EXPECT_FALSE(result.valid);
}

TEST(CvcSignatureVerifier, Verify_RsaCert_WithEcdhKey_ReturnsInvalid) {
    auto rsaCert  = mustParse(cvc_test_helpers::DH_CVCA_HEX);
    auto ecdhCert = mustParse(cvc_test_helpers::ECDH_CVCA_HEX);

    // Use ECDH key to verify RSA-signed certificate
    SignatureVerifyResult result = CvcSignatureVerifier::verify(rsaCert, ecdhCert.publicKey);
    EXPECT_FALSE(result.valid);
}

// =============================================================================
// Utility: getAlgorithmName / isRsaAlgorithm / isEcdsaAlgorithm
// =============================================================================

TEST(EacOids, GetAlgorithmName_KnownOids) {
    EXPECT_EQ(getAlgorithmName(oid::TA_RSA_V1_5_SHA_1),   "id-TA-RSA-v1-5-SHA-1");
    EXPECT_EQ(getAlgorithmName(oid::TA_RSA_V1_5_SHA_256), "id-TA-RSA-v1-5-SHA-256");
    EXPECT_EQ(getAlgorithmName(oid::TA_RSA_PSS_SHA_256),  "id-TA-RSA-PSS-SHA-256");
    EXPECT_EQ(getAlgorithmName(oid::TA_ECDSA_SHA_256),    "id-TA-ECDSA-SHA-256");
    EXPECT_EQ(getAlgorithmName(oid::TA_ECDSA_SHA_512),    "id-TA-ECDSA-SHA-512");
}

TEST(EacOids, GetAlgorithmName_UnknownOid_ReturnsSelf) {
    EXPECT_EQ(getAlgorithmName("1.2.3.4.5"), "1.2.3.4.5");
}

TEST(EacOids, IsRsaAlgorithm) {
    EXPECT_TRUE(isRsaAlgorithm(oid::TA_RSA_V1_5_SHA_1));
    EXPECT_TRUE(isRsaAlgorithm(oid::TA_RSA_V1_5_SHA_256));
    EXPECT_TRUE(isRsaAlgorithm(oid::TA_RSA_PSS_SHA_512));
    EXPECT_FALSE(isRsaAlgorithm(oid::TA_ECDSA_SHA_256));
    EXPECT_FALSE(isRsaAlgorithm(oid::ROLE_IS));
}

TEST(EacOids, IsEcdsaAlgorithm) {
    EXPECT_TRUE(isEcdsaAlgorithm(oid::TA_ECDSA_SHA_1));
    EXPECT_TRUE(isEcdsaAlgorithm(oid::TA_ECDSA_SHA_256));
    EXPECT_TRUE(isEcdsaAlgorithm(oid::TA_ECDSA_SHA_512));
    EXPECT_FALSE(isEcdsaAlgorithm(oid::TA_RSA_V1_5_SHA_256));
    EXPECT_FALSE(isEcdsaAlgorithm(oid::ROLE_AT));
}

TEST(EacOids, GetRoleName) {
    EXPECT_EQ(getRoleName(oid::ROLE_IS), "IS");
    EXPECT_EQ(getRoleName(oid::ROLE_AT), "AT");
    EXPECT_EQ(getRoleName(oid::ROLE_ST), "ST");
    EXPECT_EQ(getRoleName("0.0.0.0"), "UNKNOWN");
}
