/**
 * @file test_extension_validator.cpp
 * @brief Unit tests for extension_validator — ICAO 9303 Part 12 Section 4.6
 */

#include <gtest/gtest.h>
#include <icao/validation/extension_validator.h>
#include "test_helpers.h"

using namespace icao::validation;
using namespace test_helpers;

class ExtensionValidatorTest : public ::testing::Test {
protected:
    UniqueKey caKey_;
    UniqueCert rootCa_;
    UniqueCert dsc_;

    void SetUp() override {
        caKey_ = generateRsaKey(2048);
        rootCa_ = createRootCa(caKey_.get(), "Test CSCA");
        auto dscKey = generateRsaKey(2048);
        dsc_ = createDsc(dscKey.get(), caKey_.get(), rootCa_.get(), "Test DSC");
    }
};

// ============================================================================
// DSC Extension Validation
// ============================================================================

TEST_F(ExtensionValidatorTest, Dsc_ValidExtensions) {
    auto result = validateExtensions(dsc_.get(), "DSC");
    EXPECT_TRUE(result.valid);
    EXPECT_TRUE(result.warnings.empty());
}

TEST_F(ExtensionValidatorTest, Dsc_NoKeyUsage_NoWarning) {
    // Create a minimal cert without Key Usage extension
    auto key = generateRsaKey(2048);
    X509* cert = X509_new();
    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 500);
    X509_NAME* name = X509_NAME_new();
    X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("No KU DSC"), -1, -1, 0);
    X509_set_subject_name(cert, name);
    X509_set_issuer_name(cert, name);
    X509_NAME_free(name);
    ASN1_TIME_set(X509_getm_notBefore(cert), time(nullptr));
    ASN1_TIME_set(X509_getm_notAfter(cert), time(nullptr) + 365 * 86400L);
    X509_set_pubkey(cert, key.get());
    X509_sign(cert, key.get(), EVP_sha256());

    auto result = validateExtensions(cert, "DSC");
    // No Key Usage for DSC is unusual but not an error per spec
    EXPECT_TRUE(result.valid);
    X509_free(cert);
}

// ============================================================================
// CSCA Extension Validation
// ============================================================================

TEST_F(ExtensionValidatorTest, Csca_ValidExtensions) {
    auto result = validateExtensions(rootCa_.get(), "CSCA");
    EXPECT_TRUE(result.valid);
    EXPECT_TRUE(result.warnings.empty());
}

// ============================================================================
// Null Certificate
// ============================================================================

TEST_F(ExtensionValidatorTest, NullCert_Invalid) {
    auto result = validateExtensions(nullptr, "DSC");
    EXPECT_FALSE(result.valid);
    EXPECT_FALSE(result.warnings.empty());
}

// ============================================================================
// warningsAsString
// ============================================================================

TEST_F(ExtensionValidatorTest, WarningsAsString_Empty) {
    ExtensionValidationResult result;
    EXPECT_EQ(result.warningsAsString(), "");
}

TEST_F(ExtensionValidatorTest, WarningsAsString_Multiple) {
    ExtensionValidationResult result;
    result.warnings.push_back("warning1");
    result.warnings.push_back("warning2");
    EXPECT_EQ(result.warningsAsString(), "warning1; warning2");
}

// ============================================================================
// IDEMPOTENCY
// ============================================================================

TEST_F(ExtensionValidatorTest, Idempotency_DscValidation) {
    auto first = validateExtensions(dsc_.get(), "DSC");
    for (int i = 0; i < 100; i++) {
        auto result = validateExtensions(dsc_.get(), "DSC");
        EXPECT_EQ(result.valid, first.valid) << "Changed at iteration " << i;
        EXPECT_EQ(result.warnings.size(), first.warnings.size()) << "Changed at iteration " << i;
    }
}

TEST_F(ExtensionValidatorTest, Idempotency_CscaValidation) {
    auto first = validateExtensions(rootCa_.get(), "CSCA");
    for (int i = 0; i < 100; i++) {
        auto result = validateExtensions(rootCa_.get(), "CSCA");
        EXPECT_EQ(result.valid, first.valid) << "Changed at iteration " << i;
        EXPECT_EQ(result.warnings.size(), first.warnings.size()) << "Changed at iteration " << i;
    }
}
