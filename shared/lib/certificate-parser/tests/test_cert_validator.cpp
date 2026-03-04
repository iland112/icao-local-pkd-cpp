/**
 * @file test_cert_validator.cpp
 * @brief Unit tests for X.509 certificate validator (cert_validator.h / .cpp)
 */

#include <gtest/gtest.h>
#include "cert_validator.h"
#include "test_helpers.h"
#include <vector>
#include <string>
#include <algorithm>

using namespace icao::certificate_parser;

// ---------------------------------------------------------------------------
// Fixture: creates CSCA + DSC pair for validation tests
// ---------------------------------------------------------------------------
class CertValidatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        cscaKey_.reset(test_helpers::generateRsaKey(2048));
        ASSERT_NE(cscaKey_.get(), nullptr);

        csca_.reset(test_helpers::createSelfSignedCert("US", "Validator CSCA", 3650, cscaKey_.get()));
        ASSERT_NE(csca_.get(), nullptr);

        dsc_.reset(test_helpers::createDscCert(csca_.get(), cscaKey_.get(), "US", "Validator DSC", 365));
        ASSERT_NE(dsc_.get(), nullptr);
    }

    test_helpers::EvpPkeyPtr cscaKey_;
    test_helpers::X509Ptr csca_;
    test_helpers::X509Ptr dsc_;
};

// ---------------------------------------------------------------------------
// validate(X509* cert) -- single cert, self-signed checks
// ---------------------------------------------------------------------------

TEST_F(CertValidatorTest, Validate_ValidSelfSignedCSCA) {
    ValidationResult result = CertValidator::validate(csca_.get());

    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.status, ValidationStatus::VALID);
    EXPECT_FALSE(result.isExpired);
    EXPECT_FALSE(result.isNotYetValid);
    EXPECT_TRUE(result.signatureVerified);
    EXPECT_FALSE(result.signatureAlgorithm.empty());
}

TEST_F(CertValidatorTest, Validate_NullCert) {
    ValidationResult result = CertValidator::validate(static_cast<X509*>(nullptr));

    EXPECT_FALSE(result.isValid);
    EXPECT_EQ(result.status, ValidationStatus::UNKNOWN_ERROR);
    EXPECT_FALSE(result.errorMessage.empty());
}

TEST_F(CertValidatorTest, Validate_ExpiredCert) {
    test_helpers::X509Ptr expired(test_helpers::createExpiredCert());
    ASSERT_NE(expired.get(), nullptr);

    ValidationResult result = CertValidator::validate(expired.get());

    EXPECT_FALSE(result.isValid);
    EXPECT_EQ(result.status, ValidationStatus::EXPIRED);
    EXPECT_TRUE(result.isExpired);
}

TEST_F(CertValidatorTest, Validate_NotYetValidCert) {
    test_helpers::X509Ptr future(test_helpers::createNotYetValidCert());
    ASSERT_NE(future.get(), nullptr);

    ValidationResult result = CertValidator::validate(future.get());

    EXPECT_FALSE(result.isValid);
    EXPECT_EQ(result.status, ValidationStatus::NOT_YET_VALID);
    EXPECT_TRUE(result.isNotYetValid);
}

TEST_F(CertValidatorTest, Validate_NonSelfSignedWithoutIssuer) {
    // DSC is not self-signed; validating without issuer should fail signature check
    ValidationResult result = CertValidator::validate(dsc_.get());

    EXPECT_FALSE(result.isValid);
    EXPECT_EQ(result.status, ValidationStatus::INVALID_SIGNATURE);
    EXPECT_FALSE(result.signatureVerified);
}

// ---------------------------------------------------------------------------
// validate(X509* cert, X509* issuer) -- with issuer
// ---------------------------------------------------------------------------

TEST_F(CertValidatorTest, ValidateWithIssuer_ValidChain) {
    ValidationResult result = CertValidator::validate(dsc_.get(), csca_.get());

    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.status, ValidationStatus::VALID);
    EXPECT_TRUE(result.signatureVerified);
    EXPECT_TRUE(result.trustChainValid);
    EXPECT_EQ(result.trustChainDepth, 1);
    EXPECT_EQ(result.trustChainPath.size(), 2u);
}

TEST_F(CertValidatorTest, ValidateWithIssuer_WrongIssuer) {
    // Create a different CSCA that did NOT sign dsc_
    test_helpers::X509Ptr wrongCsca(test_helpers::createSelfSignedCert("DE", "Wrong CSCA"));
    ASSERT_NE(wrongCsca.get(), nullptr);

    ValidationResult result = CertValidator::validate(dsc_.get(), wrongCsca.get());

    EXPECT_FALSE(result.isValid);
    EXPECT_EQ(result.status, ValidationStatus::INVALID_SIGNATURE);
    EXPECT_FALSE(result.signatureVerified);
}

TEST_F(CertValidatorTest, ValidateWithIssuer_NullIssuer) {
    // Passing nullptr issuer should fall back to self-signed check
    // DSC is not self-signed, so signature verification should fail
    ValidationResult result = CertValidator::validate(dsc_.get(), static_cast<X509*>(nullptr));

    EXPECT_FALSE(result.isValid);
    EXPECT_EQ(result.status, ValidationStatus::INVALID_SIGNATURE);
}

// ---------------------------------------------------------------------------
// validate(X509* cert, vector<X509*>) -- trust chain
// ---------------------------------------------------------------------------

TEST_F(CertValidatorTest, ValidateWithChain_ValidSingleLevel) {
    std::vector<X509*> chain = { csca_.get() };
    ValidationResult result = CertValidator::validate(dsc_.get(), chain);

    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.status, ValidationStatus::VALID);
    EXPECT_TRUE(result.trustChainValid);
    EXPECT_EQ(result.trustChainDepth, 1);
    EXPECT_GE(result.trustChainPath.size(), 2u);
}

TEST_F(CertValidatorTest, ValidateWithChain_EmptyChainFallsBack) {
    std::vector<X509*> emptyChain;
    // Should fall back to validate(cert) -- which does self-signed check
    // DSC fails because it's not self-signed
    ValidationResult result = CertValidator::validate(dsc_.get(), emptyChain);

    EXPECT_FALSE(result.isValid);
}

TEST_F(CertValidatorTest, ValidateWithChain_MultiLevel) {
    // Create a link cert signed by CSCA, then a DSC signed by link
    test_helpers::EvpPkeyPtr linkKey(test_helpers::generateRsaKey(2048));
    ASSERT_NE(linkKey.get(), nullptr);

    // For simplicity, create a link cert (CA=TRUE, keyCertSign) signed by CSCA
    X509* link = test_helpers::createLinkCert(csca_.get(), cscaKey_.get());
    ASSERT_NE(link, nullptr);

    std::vector<X509*> chain = { csca_.get(), link };
    // Validate CSCA itself with the chain (self-signed should pass)
    ValidationResult result = CertValidator::validate(csca_.get(), chain);

    EXPECT_TRUE(result.isValid);
    EXPECT_EQ(result.trustChainPath.size(), 3u);

    X509_free(link);
}

// ---------------------------------------------------------------------------
// isExpired / isNotYetValid
// ---------------------------------------------------------------------------

TEST_F(CertValidatorTest, IsExpired_ValidCert) {
    EXPECT_FALSE(CertValidator::isExpired(csca_.get()));
}

TEST_F(CertValidatorTest, IsExpired_ExpiredCert) {
    test_helpers::X509Ptr expired(test_helpers::createExpiredCert());
    ASSERT_NE(expired.get(), nullptr);
    EXPECT_TRUE(CertValidator::isExpired(expired.get()));
}

TEST_F(CertValidatorTest, IsExpired_NullCert) {
    EXPECT_TRUE(CertValidator::isExpired(nullptr));
}

TEST_F(CertValidatorTest, IsNotYetValid_ValidCert) {
    EXPECT_FALSE(CertValidator::isNotYetValid(csca_.get()));
}

TEST_F(CertValidatorTest, IsNotYetValid_FutureCert) {
    test_helpers::X509Ptr future(test_helpers::createNotYetValidCert());
    ASSERT_NE(future.get(), nullptr);
    EXPECT_TRUE(CertValidator::isNotYetValid(future.get()));
}

TEST_F(CertValidatorTest, IsNotYetValid_NullCert) {
    EXPECT_FALSE(CertValidator::isNotYetValid(nullptr));
}

// ---------------------------------------------------------------------------
// verifySignature
// ---------------------------------------------------------------------------

TEST_F(CertValidatorTest, VerifySignature_SelfSigned) {
    EXPECT_TRUE(CertValidator::verifySignature(csca_.get()));
    EXPECT_TRUE(CertValidator::verifySignature(csca_.get(), nullptr));
}

TEST_F(CertValidatorTest, VerifySignature_WithIssuer) {
    EXPECT_TRUE(CertValidator::verifySignature(dsc_.get(), csca_.get()));
}

TEST_F(CertValidatorTest, VerifySignature_WrongIssuer) {
    test_helpers::X509Ptr wrong(test_helpers::createSelfSignedCert("JP", "Wrong"));
    ASSERT_NE(wrong.get(), nullptr);

    EXPECT_FALSE(CertValidator::verifySignature(dsc_.get(), wrong.get()));
}

TEST_F(CertValidatorTest, VerifySignature_NullCert) {
    EXPECT_FALSE(CertValidator::verifySignature(nullptr));
}

// ---------------------------------------------------------------------------
// getKeyUsages
// ---------------------------------------------------------------------------

TEST_F(CertValidatorTest, GetKeyUsages_CSCA) {
    auto usages = CertValidator::getKeyUsages(csca_.get());

    EXPECT_FALSE(usages.empty());

    // CSCA should have keyCertSign and cRLSign
    EXPECT_NE(std::find(usages.begin(), usages.end(), "keyCertSign"), usages.end());
    EXPECT_NE(std::find(usages.begin(), usages.end(), "cRLSign"), usages.end());
}

TEST_F(CertValidatorTest, GetKeyUsages_DSC) {
    auto usages = CertValidator::getKeyUsages(dsc_.get());

    EXPECT_FALSE(usages.empty());

    // DSC should have digitalSignature
    EXPECT_NE(std::find(usages.begin(), usages.end(), "digitalSignature"), usages.end());

    // DSC should NOT have keyCertSign
    EXPECT_EQ(std::find(usages.begin(), usages.end(), "keyCertSign"), usages.end());
}

TEST_F(CertValidatorTest, GetKeyUsages_NullCert) {
    auto usages = CertValidator::getKeyUsages(nullptr);
    EXPECT_TRUE(usages.empty());
}

// ---------------------------------------------------------------------------
// getExtendedKeyUsages
// ---------------------------------------------------------------------------

TEST_F(CertValidatorTest, GetExtendedKeyUsages_MLSC) {
    test_helpers::X509Ptr mlsc(test_helpers::createMlscCert(csca_.get(), cscaKey_.get()));
    ASSERT_NE(mlsc.get(), nullptr);

    auto ekus = CertValidator::getExtendedKeyUsages(mlsc.get());
    EXPECT_FALSE(ekus.empty());
}

TEST_F(CertValidatorTest, GetExtendedKeyUsages_NoEKU) {
    // CSCA typically has no EKU
    auto ekus = CertValidator::getExtendedKeyUsages(csca_.get());
    EXPECT_TRUE(ekus.empty());
}

TEST_F(CertValidatorTest, GetExtendedKeyUsages_NullCert) {
    auto ekus = CertValidator::getExtendedKeyUsages(nullptr);
    EXPECT_TRUE(ekus.empty());
}

// ---------------------------------------------------------------------------
// getSignatureAlgorithm
// ---------------------------------------------------------------------------

TEST_F(CertValidatorTest, GetSignatureAlgorithm_SHA256WithRSA) {
    std::string alg = CertValidator::getSignatureAlgorithm(csca_.get());
    EXPECT_FALSE(alg.empty());
    // Our test certs are signed with SHA-256 + RSA
    EXPECT_NE(alg.find("sha256"), std::string::npos);
}

TEST_F(CertValidatorTest, GetSignatureAlgorithm_NullCert) {
    std::string alg = CertValidator::getSignatureAlgorithm(nullptr);
    EXPECT_TRUE(alg.empty());
}

// ---------------------------------------------------------------------------
// ValidationResult notBefore/notAfter time points
// ---------------------------------------------------------------------------

TEST_F(CertValidatorTest, NotBeforeNotAfter_Populated) {
    ValidationResult result = CertValidator::validate(csca_.get());

    EXPECT_TRUE(result.notBefore.has_value());
    EXPECT_TRUE(result.notAfter.has_value());

    // notAfter should be after notBefore
    if (result.notBefore.has_value() && result.notAfter.has_value()) {
        EXPECT_GT(*result.notAfter, *result.notBefore);
    }
}

TEST_F(CertValidatorTest, NotBeforeNotAfter_ExpiredCert) {
    test_helpers::X509Ptr expired(test_helpers::createExpiredCert());
    ASSERT_NE(expired.get(), nullptr);

    ValidationResult result = CertValidator::validate(expired.get());

    EXPECT_TRUE(result.notBefore.has_value());
    EXPECT_TRUE(result.notAfter.has_value());

    // notAfter should be in the past
    auto now = std::chrono::system_clock::now();
    if (result.notAfter.has_value()) {
        EXPECT_LT(*result.notAfter, now);
    }
}

// ---------------------------------------------------------------------------
// ValidationResult purpose fields
// ---------------------------------------------------------------------------

TEST_F(CertValidatorTest, PurposeFields_Populated) {
    ValidationResult result = CertValidator::validate(csca_.get());

    EXPECT_TRUE(result.purposeValid);
    EXPECT_FALSE(result.keyUsages.empty());
    // signatureAlgorithm should be populated
    EXPECT_FALSE(result.signatureAlgorithm.empty());
}
