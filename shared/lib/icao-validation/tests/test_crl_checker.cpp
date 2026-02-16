/**
 * @file test_crl_checker.cpp
 * @brief Unit tests for CrlChecker — RFC 5280 CRL revocation checking
 *
 * Uses MockCrlProvider to test revocation checking without DB/LDAP.
 */

#include <gtest/gtest.h>
#include <icao/validation/crl_checker.h>
#include <icao/validation/cert_ops.h>
#include "test_helpers.h"

using namespace icao::validation;
using namespace test_helpers;

// ============================================================================
// Mock CRL Provider
// ============================================================================

class MockCrlProvider : public ICrlProvider {
public:
    X509_CRL* crl_ = nullptr;  // Non-owning, test fixture owns it

    X509_CRL* findCrlByCountry(const std::string& /*countryCode*/) override {
        if (!crl_) return nullptr;
        return X509_CRL_dup(crl_);  // Return a copy (caller frees)
    }
};

class EmptyCrlProvider : public ICrlProvider {
public:
    X509_CRL* findCrlByCountry(const std::string&) override { return nullptr; }
};

// ============================================================================
// Test Fixture
// ============================================================================

class CrlCheckerTest : public ::testing::Test {
protected:
    UniqueKey caKey_;
    UniqueKey dscKey_;
    UniqueCert rootCa_;
    UniqueCert dsc_;

    void SetUp() override {
        caKey_ = generateRsaKey(2048);
        dscKey_ = generateRsaKey(2048);
        rootCa_ = createRootCa(caKey_.get(), "CRL Test CSCA");
        dsc_ = createDsc(dscKey_.get(), caKey_.get(), rootCa_.get(), "CRL Test DSC");
    }
};

// ============================================================================
// Constructor Validation
// ============================================================================

TEST_F(CrlCheckerTest, Constructor_NullProviderThrows) {
    EXPECT_THROW(CrlChecker(nullptr), std::invalid_argument);
}

// ============================================================================
// Certificate Not Revoked
// ============================================================================

TEST_F(CrlCheckerTest, NotRevoked_EmptyCrl) {
    auto crl = createCrl(caKey_.get(), rootCa_.get(), {});  // No revoked certs

    MockCrlProvider provider;
    provider.crl_ = crl.get();

    CrlChecker checker(&provider);
    auto result = checker.check(dsc_.get(), "KR");

    EXPECT_EQ(result.status, CrlCheckStatus::VALID);
    EXPECT_FALSE(result.thisUpdate.empty());
    EXPECT_FALSE(result.nextUpdate.empty());
}

TEST_F(CrlCheckerTest, NotRevoked_OtherSerialsRevoked) {
    // DSC serial is 100 (from createDsc), revoke different serials
    auto crl = createCrl(caKey_.get(), rootCa_.get(), {1, 2, 3, 999});

    MockCrlProvider provider;
    provider.crl_ = crl.get();

    CrlChecker checker(&provider);
    auto result = checker.check(dsc_.get(), "KR");

    EXPECT_EQ(result.status, CrlCheckStatus::VALID);
}

// ============================================================================
// Certificate Revoked
// ============================================================================

TEST_F(CrlCheckerTest, Revoked_MatchingSerial) {
    // DSC serial is 100, revoke it
    auto crl = createCrl(caKey_.get(), rootCa_.get(), {50, 100, 200});

    MockCrlProvider provider;
    provider.crl_ = crl.get();

    CrlChecker checker(&provider);
    auto result = checker.check(dsc_.get(), "KR");

    EXPECT_EQ(result.status, CrlCheckStatus::REVOKED);
    EXPECT_NE(result.message.find("revoked"), std::string::npos);
}

// ============================================================================
// CRL Unavailable
// ============================================================================

TEST_F(CrlCheckerTest, CrlUnavailable_NoProvider) {
    EmptyCrlProvider provider;
    CrlChecker checker(&provider);

    auto result = checker.check(dsc_.get(), "KR");
    EXPECT_EQ(result.status, CrlCheckStatus::CRL_UNAVAILABLE);
}

// ============================================================================
// CRL Expired
// ============================================================================

TEST_F(CrlCheckerTest, CrlExpired) {
    auto crl = createCrl(caKey_.get(), rootCa_.get(), {}, 30, true);  // expired=true

    MockCrlProvider provider;
    provider.crl_ = crl.get();

    CrlChecker checker(&provider);
    auto result = checker.check(dsc_.get(), "KR");

    EXPECT_EQ(result.status, CrlCheckStatus::CRL_EXPIRED);
}

// ============================================================================
// Edge Cases
// ============================================================================

TEST_F(CrlCheckerTest, NullCert_NotChecked) {
    MockCrlProvider provider;
    CrlChecker checker(&provider);

    auto result = checker.check(nullptr, "KR");
    EXPECT_EQ(result.status, CrlCheckStatus::NOT_CHECKED);
}

TEST_F(CrlCheckerTest, EmptyCountryCode_NotChecked) {
    MockCrlProvider provider;
    CrlChecker checker(&provider);

    auto result = checker.check(dsc_.get(), "");
    EXPECT_EQ(result.status, CrlCheckStatus::NOT_CHECKED);
}

// ============================================================================
// CRL Dates Extraction
// ============================================================================

TEST_F(CrlCheckerTest, CrlDates_Populated) {
    auto crl = createCrl(caKey_.get(), rootCa_.get(), {});

    MockCrlProvider provider;
    provider.crl_ = crl.get();

    CrlChecker checker(&provider);
    auto result = checker.check(dsc_.get(), "KR");

    EXPECT_FALSE(result.thisUpdate.empty());
    EXPECT_FALSE(result.nextUpdate.empty());
    EXPECT_NE(result.thisUpdate.find("T"), std::string::npos);  // ISO 8601
    EXPECT_NE(result.nextUpdate.find("T"), std::string::npos);
}

// ============================================================================
// Status String Conversions
// ============================================================================

TEST_F(CrlCheckerTest, StatusToString) {
    EXPECT_EQ(crlCheckStatusToString(CrlCheckStatus::VALID), "VALID");
    EXPECT_EQ(crlCheckStatusToString(CrlCheckStatus::REVOKED), "REVOKED");
    EXPECT_EQ(crlCheckStatusToString(CrlCheckStatus::CRL_UNAVAILABLE), "CRL_UNAVAILABLE");
    EXPECT_EQ(crlCheckStatusToString(CrlCheckStatus::CRL_EXPIRED), "CRL_EXPIRED");
    EXPECT_EQ(crlCheckStatusToString(CrlCheckStatus::CRL_INVALID), "CRL_INVALID");
    EXPECT_EQ(crlCheckStatusToString(CrlCheckStatus::NOT_CHECKED), "NOT_CHECKED");
}

TEST_F(CrlCheckerTest, ValidationStatusToString) {
    EXPECT_EQ(validationStatusToString(ValidationStatus::VALID), "VALID");
    EXPECT_EQ(validationStatusToString(ValidationStatus::EXPIRED_VALID), "EXPIRED_VALID");
    EXPECT_EQ(validationStatusToString(ValidationStatus::INVALID), "INVALID");
    EXPECT_EQ(validationStatusToString(ValidationStatus::PENDING), "PENDING");
    EXPECT_EQ(validationStatusToString(ValidationStatus::ERROR), "ERROR");
}

// ============================================================================
// IDEMPOTENCY
// ============================================================================

TEST_F(CrlCheckerTest, Idempotency_NotRevoked) {
    auto crl = createCrl(caKey_.get(), rootCa_.get(), {1, 2, 3});

    MockCrlProvider provider;
    provider.crl_ = crl.get();

    CrlChecker checker(&provider);
    auto first = checker.check(dsc_.get(), "KR");

    for (int i = 0; i < 100; i++) {
        auto result = checker.check(dsc_.get(), "KR");
        EXPECT_EQ(result.status, first.status) << "Changed at iteration " << i;
        EXPECT_EQ(result.thisUpdate, first.thisUpdate) << "Changed at iteration " << i;
        EXPECT_EQ(result.nextUpdate, first.nextUpdate) << "Changed at iteration " << i;
        EXPECT_EQ(result.revocationReason, first.revocationReason) << "Changed at iteration " << i;
    }
}

TEST_F(CrlCheckerTest, Idempotency_Revoked) {
    auto crl = createCrl(caKey_.get(), rootCa_.get(), {100});  // DSC serial = 100

    MockCrlProvider provider;
    provider.crl_ = crl.get();

    CrlChecker checker(&provider);
    auto first = checker.check(dsc_.get(), "KR");

    for (int i = 0; i < 100; i++) {
        auto result = checker.check(dsc_.get(), "KR");
        EXPECT_EQ(result.status, first.status) << "Changed at iteration " << i;
        EXPECT_EQ(result.revocationReason, first.revocationReason) << "Changed at iteration " << i;
    }
}

TEST_F(CrlCheckerTest, Idempotency_CrlUnavailable) {
    EmptyCrlProvider provider;
    CrlChecker checker(&provider);
    auto first = checker.check(dsc_.get(), "KR");

    for (int i = 0; i < 100; i++) {
        auto result = checker.check(dsc_.get(), "KR");
        EXPECT_EQ(result.status, first.status) << "Changed at iteration " << i;
    }
}
