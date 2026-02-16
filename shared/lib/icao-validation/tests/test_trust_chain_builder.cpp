/**
 * @file test_trust_chain_builder.cpp
 * @brief Unit tests for TrustChainBuilder — ICAO 9303 Part 12 trust chain
 *
 * Uses MockCscaProvider to test chain building without DB/LDAP.
 */

#include <gtest/gtest.h>
#include <icao/validation/trust_chain_builder.h>
#include <icao/validation/cert_ops.h>
#include "test_helpers.h"

using namespace icao::validation;
using namespace test_helpers;

// ============================================================================
// Mock CSCA Provider
// ============================================================================

class MockCscaProvider : public ICscaProvider {
public:
    // Stores CSCAs — does NOT own the pointers (test fixture owns them)
    std::vector<X509*> cscas;

    std::vector<X509*> findAllCscasByIssuerDn(const std::string& /*issuerDn*/) override {
        // Return duplicates (TrustChainBuilder will free them)
        std::vector<X509*> result;
        for (X509* csca : cscas) {
            result.push_back(X509_dup(csca));
        }
        return result;
    }

    X509* findCscaByIssuerDn(const std::string& /*issuerDn*/, const std::string& /*cc*/) override {
        if (cscas.empty()) return nullptr;
        return X509_dup(cscas[0]);
    }
};

// Empty provider — no CSCAs available
class EmptyCscaProvider : public ICscaProvider {
public:
    std::vector<X509*> findAllCscasByIssuerDn(const std::string&) override { return {}; }
    X509* findCscaByIssuerDn(const std::string&, const std::string&) override { return nullptr; }
};

// ============================================================================
// Test Fixture
// ============================================================================

class TrustChainBuilderTest : public ::testing::Test {
protected:
    UniqueKey caKey_;
    UniqueKey dscKey_;
    UniqueCert rootCa_;
    UniqueCert dsc_;

    void SetUp() override {
        caKey_ = generateRsaKey(2048);
        dscKey_ = generateRsaKey(2048);
        rootCa_ = createRootCa(caKey_.get(), "Test Root CSCA");
        dsc_ = createDsc(dscKey_.get(), caKey_.get(), rootCa_.get(), "Test DSC");
    }
};

// ============================================================================
// Constructor Validation
// ============================================================================

TEST_F(TrustChainBuilderTest, Constructor_NullProviderThrows) {
    EXPECT_THROW(TrustChainBuilder(nullptr), std::invalid_argument);
}

// ============================================================================
// Simple Chain: DSC -> Root
// ============================================================================

TEST_F(TrustChainBuilderTest, SimpleChain_DscToRoot) {
    MockCscaProvider provider;
    provider.cscas.push_back(rootCa_.get());

    TrustChainBuilder builder(&provider);
    auto result = builder.build(dsc_.get());

    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.depth, 2);  // DSC + Root
    EXPECT_NE(result.path.find("DSC"), std::string::npos);
    EXPECT_NE(result.path.find("Root"), std::string::npos);
    EXPECT_FALSE(result.cscaSubjectDn.empty());
    EXPECT_FALSE(result.cscaFingerprint.empty());
}

// ============================================================================
// No CSCA Found — Chain Broken
// ============================================================================

TEST_F(TrustChainBuilderTest, NoCsca_ChainBroken) {
    EmptyCscaProvider provider;
    TrustChainBuilder builder(&provider);

    auto result = builder.build(dsc_.get());
    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.message.find("No CSCA"), std::string::npos);
}

// ============================================================================
// Null Leaf Certificate
// ============================================================================

TEST_F(TrustChainBuilderTest, NullLeaf_Invalid) {
    MockCscaProvider provider;
    TrustChainBuilder builder(&provider);

    auto result = builder.build(nullptr);
    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.message.find("null"), std::string::npos);
}

// ============================================================================
// Self-Signed Certificate (Root only)
// ============================================================================

TEST_F(TrustChainBuilderTest, SelfSigned_RootOnlyChain) {
    // build() always queries CSCA provider first, so root must be in the store
    MockCscaProvider provider;
    provider.cscas.push_back(rootCa_.get());
    TrustChainBuilder builder(&provider);

    // Root CA passed as leaf — detected as self-signed in first loop iteration
    auto result = builder.build(rootCa_.get());
    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.depth, 1);  // Just the root itself
}

// ============================================================================
// Wrong CSCA Key — Signature Mismatch
// ============================================================================

TEST_F(TrustChainBuilderTest, WrongCsca_SignatureFails) {
    auto otherKey = generateRsaKey(2048);
    auto otherCa = createRootCa(otherKey.get(), "Other Root CSCA");

    MockCscaProvider provider;
    provider.cscas.push_back(otherCa.get());

    TrustChainBuilder builder(&provider);
    auto result = builder.build(dsc_.get());

    // The chain should still be built via DN match fallback, but signature verification
    // in the validation step should fail
    // Note: behavior depends on whether DN matches
    // With different CN, DN won't match, so "Chain broken"
    EXPECT_FALSE(result.valid);
}

// ============================================================================
// DSC Expiration — Informational Only (Hybrid Model)
// ============================================================================

TEST_F(TrustChainBuilderTest, ExpiredDsc_StillValid) {
    // Create an expired DSC signed by valid CSCA
    auto expiredDscKey = generateRsaKey(2048);
    X509* cert = X509_new();
    X509_set_version(cert, 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert), 999);

    X509_NAME* subject = X509_NAME_new();
    X509_NAME_add_entry_by_txt(subject, "C", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("KR"), -1, -1, 0);
    X509_NAME_add_entry_by_txt(subject, "CN", MBSTRING_ASC,
        reinterpret_cast<const unsigned char*>("Expired DSC"), -1, -1, 0);
    X509_set_subject_name(cert, subject);
    X509_NAME_free(subject);
    X509_set_issuer_name(cert, X509_get_subject_name(rootCa_.get()));

    // Expired: both dates in the past
    ASN1_TIME_set(X509_getm_notBefore(cert), time(nullptr) - 730 * 86400L);
    ASN1_TIME_set(X509_getm_notAfter(cert), time(nullptr) - 1 * 86400L);

    X509_set_pubkey(cert, expiredDscKey.get());
    X509_sign(cert, caKey_.get(), EVP_sha256());

    MockCscaProvider provider;
    provider.cscas.push_back(rootCa_.get());

    TrustChainBuilder builder(&provider);
    auto result = builder.build(cert);

    EXPECT_TRUE(result.valid);     // ICAO hybrid model: signature OK = VALID
    EXPECT_TRUE(result.dscExpired); // Expiration is informational

    X509_free(cert);
}

// ============================================================================
// Multi-CSCA Key Rollover — Select by Signature
// ============================================================================

TEST_F(TrustChainBuilderTest, MultiCsca_SelectBySignature) {
    // Create two CSCAs with same DN but different keys
    auto oldKey = generateRsaKey(2048);
    auto newKey = generateRsaKey(2048);

    // Both have same subject DN structure
    auto oldCa = createRootCa(oldKey.get(), "Test Root CSCA");
    auto newCa = createRootCa(newKey.get(), "Test Root CSCA");

    // DSC signed by newKey
    auto dscKey2 = generateRsaKey(2048);
    auto dsc2 = createDsc(dscKey2.get(), newKey.get(), newCa.get(), "DSC for new key");

    MockCscaProvider provider;
    provider.cscas.push_back(oldCa.get());
    provider.cscas.push_back(newCa.get());

    TrustChainBuilder builder(&provider);
    auto result = builder.build(dsc2.get());

    EXPECT_TRUE(result.valid);
    EXPECT_EQ(result.depth, 2);
}

// ============================================================================
// IDEMPOTENCY
// ============================================================================

TEST_F(TrustChainBuilderTest, Idempotency_SimpleChain) {
    MockCscaProvider provider;
    provider.cscas.push_back(rootCa_.get());
    TrustChainBuilder builder(&provider);

    auto first = builder.build(dsc_.get());
    for (int i = 0; i < 50; i++) {
        auto result = builder.build(dsc_.get());
        EXPECT_EQ(result.valid, first.valid) << "Changed at iteration " << i;
        EXPECT_EQ(result.depth, first.depth) << "Changed at iteration " << i;
        EXPECT_EQ(result.path, first.path) << "Changed at iteration " << i;
        EXPECT_EQ(result.dscExpired, first.dscExpired) << "Changed at iteration " << i;
        EXPECT_EQ(result.cscaExpired, first.cscaExpired) << "Changed at iteration " << i;
        EXPECT_EQ(result.cscaSubjectDn, first.cscaSubjectDn) << "Changed at iteration " << i;
        EXPECT_EQ(result.cscaFingerprint, first.cscaFingerprint) << "Changed at iteration " << i;
    }
}

TEST_F(TrustChainBuilderTest, Idempotency_NoCsca) {
    EmptyCscaProvider provider;
    TrustChainBuilder builder(&provider);

    auto first = builder.build(dsc_.get());
    for (int i = 0; i < 50; i++) {
        auto result = builder.build(dsc_.get());
        EXPECT_EQ(result.valid, first.valid) << "Changed at iteration " << i;
        EXPECT_EQ(result.message, first.message) << "Changed at iteration " << i;
    }
}
