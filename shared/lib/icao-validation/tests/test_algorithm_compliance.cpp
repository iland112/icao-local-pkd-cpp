/**
 * @file test_algorithm_compliance.cpp
 * @brief Unit tests for algorithm_compliance — ICAO 9303 Part 12 Appendix A
 */

#include <gtest/gtest.h>
#include <icao/validation/algorithm_compliance.h>
#include "test_helpers.h"

using namespace icao::validation;
using namespace test_helpers;

class AlgorithmComplianceTest : public ::testing::Test {
protected:
    UniqueKey rsaKey2048_;
    UniqueKey rsaKey1024_;
    UniqueKey ecKey_;

    void SetUp() override {
        rsaKey2048_ = generateRsaKey(2048);
        rsaKey1024_ = generateRsaKey(1024);
        ecKey_ = generateEcKey();
    }
};

// ============================================================================
// SHA-256 + RSA (Approved)
// ============================================================================

TEST_F(AlgorithmComplianceTest, Sha256Rsa_Compliant) {
    auto cert = createRootCa(rsaKey2048_.get(), "SHA256-RSA CA", "KR", 365, EVP_sha256());
    auto result = validateAlgorithmCompliance(cert.get());
    EXPECT_TRUE(result.compliant);
    EXPECT_TRUE(result.warning.empty());
    EXPECT_GE(result.keyBits, 2048);
}

// ============================================================================
// SHA-384 + RSA (Approved)
// ============================================================================

TEST_F(AlgorithmComplianceTest, Sha384Rsa_Compliant) {
    auto cert = createRootCa(rsaKey2048_.get(), "SHA384-RSA CA", "KR", 365, EVP_sha384());
    auto result = validateAlgorithmCompliance(cert.get());
    EXPECT_TRUE(result.compliant);
    EXPECT_TRUE(result.warning.empty());
}

// ============================================================================
// SHA-512 + RSA (Approved)
// ============================================================================

TEST_F(AlgorithmComplianceTest, Sha512Rsa_Compliant) {
    auto cert = createRootCa(rsaKey2048_.get(), "SHA512-RSA CA", "KR", 365, EVP_sha512());
    auto result = validateAlgorithmCompliance(cert.get());
    EXPECT_TRUE(result.compliant);
    EXPECT_TRUE(result.warning.empty());
}

// ============================================================================
// SHA-256 + ECDSA (Approved)
// ============================================================================

TEST_F(AlgorithmComplianceTest, Sha256Ecdsa_Compliant) {
    auto cert = createRootCa(ecKey_.get(), "SHA256-ECDSA CA", "KR", 365, EVP_sha256());
    auto result = validateAlgorithmCompliance(cert.get());
    EXPECT_TRUE(result.compliant);
    EXPECT_TRUE(result.warning.empty());
}

// ============================================================================
// SHA-1 + RSA (Deprecated — compliant with warning)
// ============================================================================

TEST_F(AlgorithmComplianceTest, Sha1Rsa_DeprecatedWarning) {
    auto cert = createRootCa(rsaKey2048_.get(), "SHA1-RSA CA", "KR", 365, EVP_sha1());
    auto result = validateAlgorithmCompliance(cert.get());
    EXPECT_TRUE(result.compliant);  // Still compliant but deprecated
    EXPECT_FALSE(result.warning.empty());
    EXPECT_NE(result.warning.find("SHA-1"), std::string::npos);
}

// ============================================================================
// RSA < 2048 bits (Warning)
// ============================================================================

TEST_F(AlgorithmComplianceTest, SmallRsaKey_Warning) {
    auto cert = createRootCa(rsaKey1024_.get(), "Small RSA CA", "KR", 365, EVP_sha256());
    auto result = validateAlgorithmCompliance(cert.get());
    EXPECT_TRUE(result.compliant);  // Algorithm is fine
    EXPECT_FALSE(result.warning.empty());  // Key size warning
    EXPECT_NE(result.warning.find("2048"), std::string::npos);
    EXPECT_LT(result.keyBits, 2048);
}

// ============================================================================
// Key Bits Extraction
// ============================================================================

TEST_F(AlgorithmComplianceTest, KeyBits_Rsa2048) {
    auto cert = createRootCa(rsaKey2048_.get(), "RSA-2048 CA");
    auto result = validateAlgorithmCompliance(cert.get());
    EXPECT_EQ(result.keyBits, 2048);
}

TEST_F(AlgorithmComplianceTest, KeyBits_Ec256) {
    auto cert = createRootCa(ecKey_.get(), "EC-256 CA");
    auto result = validateAlgorithmCompliance(cert.get());
    EXPECT_GT(result.keyBits, 0);
}

// ============================================================================
// Algorithm Name
// ============================================================================

TEST_F(AlgorithmComplianceTest, AlgorithmName_NotEmpty) {
    auto cert = createRootCa(rsaKey2048_.get(), "Test CA");
    auto result = validateAlgorithmCompliance(cert.get());
    EXPECT_FALSE(result.algorithm.empty());
}

// ============================================================================
// Null Certificate
// ============================================================================

TEST_F(AlgorithmComplianceTest, NullCert_NotCompliant) {
    auto result = validateAlgorithmCompliance(nullptr);
    EXPECT_FALSE(result.compliant);
}

// ============================================================================
// IDEMPOTENCY
// ============================================================================

TEST_F(AlgorithmComplianceTest, Idempotency_Sha256Rsa) {
    auto cert = createRootCa(rsaKey2048_.get(), "Idempotent CA");
    auto first = validateAlgorithmCompliance(cert.get());
    for (int i = 0; i < 100; i++) {
        auto result = validateAlgorithmCompliance(cert.get());
        EXPECT_EQ(result.compliant, first.compliant) << "Changed at iteration " << i;
        EXPECT_EQ(result.algorithm, first.algorithm) << "Changed at iteration " << i;
        EXPECT_EQ(result.warning, first.warning) << "Changed at iteration " << i;
        EXPECT_EQ(result.keyBits, first.keyBits) << "Changed at iteration " << i;
    }
}

TEST_F(AlgorithmComplianceTest, Idempotency_Sha1Deprecated) {
    auto cert = createRootCa(rsaKey2048_.get(), "SHA1 CA", "KR", 365, EVP_sha1());
    auto first = validateAlgorithmCompliance(cert.get());
    for (int i = 0; i < 100; i++) {
        auto result = validateAlgorithmCompliance(cert.get());
        EXPECT_EQ(result.compliant, first.compliant) << "Changed at iteration " << i;
        EXPECT_EQ(result.warning, first.warning) << "Changed at iteration " << i;
    }
}
