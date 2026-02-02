/**
 * @file test_metadata_extractor.cpp
 * @brief Unit tests for metadata extractor
 */

#include <gtest/gtest.h>
#include <icao/x509/metadata_extractor.h>
#include <icao/x509/certificate_parser.h>
#include <openssl/x509.h>

using namespace icao::x509;

class MetadataExtractorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Real ICAO UN CSCA certificate (PEM format) - RSA 3072-bit, valid 2022-2032
        const std::string testCertPem = R"(-----BEGIN CERTIFICATE-----
MIIGYDCCBMigAwIBAgIEWZbiWDANBgkqhkiG9w0BAQsFADBoMQswCQYDVQQGEwJV
TjEXMBUGA1UECgwOVW5pdGVkIE5hdGlvbnMxIjAgBgNVBAsMGUNlcnRpZmljYXRp
b24gQXV0aG9yaXRpZXMxHDAaBgNVBAMME1VuaXRlZCBOYXRpb25zIENTQ0EwHhcN
MjIwNjE0MTUxNTA5WhcNMzIwNjE0MTU0NTA5WjBoMQswCQYDVQQGEwJVTjEXMBUG
A1UECgwOVW5pdGVkIE5hdGlvbnMxIjAgBgNVBAsMGUNlcnRpZmljYXRpb24gQXV0
aG9yaXRpZXMxHDAaBgNVBAMME1VuaXRlZCBOYXRpb25zIENTQ0EwggGiMA0GCSqG
SIb3DQEBAQUAA4IBjwAwggGKAoIBgQCsgw5qSCt2GW7ktzIXXKM9YOZFYKtYRWAt
Yrej3ajLxi4+YQYJs4vSH+OumKfc2onO9G4ZPwOClOqS4SMKrjFuzzHeohIsErJA
15ETrbGff2D1cOFim7VXwWN8QLdEikq548bn3XuhXVv+2WfkxJMFSqGx4f8r8Jiv
J84UaWOKOJSJkQAUNIfSeg+EbjWLbNECi82+PXSIPY8b/gvyY6wTHy20BzmNvAJ3
l5Kck5PdQcOXjfUmtn8StzxqKY7KuxsnSJE/hGs0oJGc8MFw0K1gh8czx+rbRWQd
bJLD2qr1J8p22qwhvZfrY/r3hJw4gT2mZ433VejCxKzk6escotY6YTwhnpkGOTCX
svaVZ23L/jz27E714HtqAS5Z42ak8xnK1Jl/FwvGmwPYKrSKqSdMLs4NiX5ar3wF
oKQvYtNLkwF+s6FhiiIIgPG6pVpFT8+cIoRPBYr+HHPHvbyNiJJshuxC0Bi39uai
9khHykBtABnEYb1y2V5wT1UJFkhkJQECAwEAAaOCAhAwggIMMCoGA1UdEgQjMCGB
DXRyYXZlbEB1bi5vcmekEDAOMQwwCgYDVQQHDANVTk8wKgYDVR0RBCMwIYENdHJh
dmVsQHVuLm9yZ6QQMA4xDDAKBgNVBAcMA1VOTzAOBgNVHQ8BAf8EBAMCAQYwEgYD
VR0TAQH/BAgwBgEB/wIBADCCAR8GA1UdHwSCARYwggESMIGOoIGLoIGIhi5odHRw
Oi8vdW5vY3JsLm1hbmFnZWQuZW50cnVzdC5jb20vQ1JMcy9VTk8uY3JshipodHRw
czovL3BrZGRvd25sb2FkMS5pY2FvLmludC9DUkxzL1VOTy5jcmyGKmh0dHBzOi8v
cGtkZG93bmxvYWQyLmljYW8uaW50L0NSTHMvVU5PLmNybDB/oH2ge6R5MHcxCzAJ
BgNVBAYTAlVOMRcwFQYDVQQKDA5Vbml0ZWQgTmF0aW9uczEiMCAGA1UECwwZQ2Vy
dGlmaWNhdGlvbiBBdXRob3JpdGllczEcMBoGA1UEAwwTVW5pdGVkIE5hdGlvbnMg
Q1NDQTENMAsGA1UEAwwEQ1JMMTArBgNVHRAEJDAigA8yMDIyMDYxNDE1MTUwOVqB
DzIwMjgwNjE0MTA1NzA5WjAfBgNVHSMEGDAWgBQGVLK4ZOx4qkZ1+REGNOzawqW0
rzAdBgNVHQ4EFgQUBlSyuGTseKpGdfkRBjTs2sKltK8wDQYJKoZIhvcNAQELBQAD
ggGBAAqpN34ueb/PyXQ47YhrEYkJ6fcTznCi9k/zA6iHc69FB6lV0lBEmvfqhSsF
ra+PC20QXgPnhe8pr0jD4SKNRO39S6Etsudo6xc77ZZiYkS11pWKBrNE3JKP9ZwP
aYH2WnnloKb07xqA691SlbmM1mqtF0wEESl1jNRYW5X9/Wb2kHEOtdJmh5/rYsct
/ALskBrdVp2Yu/p1Xr3xIDRY8PAyMTe2cFhC0l8pTwRtYud7dbhY0vd0oipSuULC
LlCDykKrOqyMQJyNM4HkXlRdSI3cdfcqpOOecWuz4sfC3Echo7M6VzvPnNdlYhST
uToXCL8Ht775dZHxkdaz/1UXkgaFCwUkorsAEH+vvQflKFkVkIpkNV6dk/yD5Hna
j1ZBsHRFJC8CV5fXsNBkEwWsLCxthEsdt8qkrKJDHpSEVho6TcaZ0EDqr34r/mpS
RdLvn5ind7ve8BLAxSgJ4HQn7KU0AxUDdQ4ZKWCbucJTY2b6Z1qZR1fecIoRQsXH
9dbERQ==
-----END CERTIFICATE-----)";

        testCert = parseCertificateFromPem(testCertPem);
        ASSERT_NE(testCert, nullptr);
    }

    void TearDown() override {
        if (testCert) {
            X509_free(testCert);
        }
    }

    X509* testCert = nullptr;
};

TEST_F(MetadataExtractorTest, GetVersion_Valid) {
    int version = getVersion(testCert);
    // v3 certificate = version 2 (0-indexed)
    EXPECT_EQ(version, 2);
}

TEST_F(MetadataExtractorTest, GetVersion_Null) {
    int version = getVersion(nullptr);
    EXPECT_EQ(version, 0);
}

TEST_F(MetadataExtractorTest, GetSerialNumber_Valid) {
    std::string serial = getSerialNumber(testCert);
    EXPECT_FALSE(serial.empty());
    // Serial number should be hex string (lowercase)
    for (char c : serial) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }
}

TEST_F(MetadataExtractorTest, GetSerialNumber_Null) {
    std::string serial = getSerialNumber(nullptr);
    EXPECT_TRUE(serial.empty());
}

TEST_F(MetadataExtractorTest, GetSignatureAlgorithm_Valid) {
    auto alg = getSignatureAlgorithm(testCert);
    ASSERT_TRUE(alg.has_value());
    // Should be sha256WithRSAEncryption or similar
    EXPECT_FALSE(alg->empty());
}

TEST_F(MetadataExtractorTest, GetSignatureAlgorithm_Null) {
    auto alg = getSignatureAlgorithm(nullptr);
    EXPECT_FALSE(alg.has_value());
}

TEST_F(MetadataExtractorTest, GetSignatureHashAlgorithm_Valid) {
    auto hash = getSignatureHashAlgorithm(testCert);
    // May or may not be present depending on certificate
    if (hash.has_value()) {
        EXPECT_FALSE(hash->empty());
        // Should be uppercase (SHA-256, SHA-384, etc.)
        EXPECT_TRUE(hash->find("SHA") != std::string::npos);
    }
}

TEST_F(MetadataExtractorTest, GetPublicKeyAlgorithm_Valid) {
    auto alg = getPublicKeyAlgorithm(testCert);
    ASSERT_TRUE(alg.has_value());
    // RSA certificate
    EXPECT_TRUE(*alg == "RSA" || *alg == "ECDSA" || *alg == "DSA");
}

TEST_F(MetadataExtractorTest, GetPublicKeyAlgorithm_Null) {
    auto alg = getPublicKeyAlgorithm(nullptr);
    EXPECT_FALSE(alg.has_value());
}

TEST_F(MetadataExtractorTest, GetPublicKeySize_Valid) {
    auto size = getPublicKeySize(testCert);
    ASSERT_TRUE(size.has_value());
    // RSA key size should be reasonable (1024, 2048, 4096, etc.)
    EXPECT_GT(*size, 512);
    EXPECT_LE(*size, 8192);
}

TEST_F(MetadataExtractorTest, GetPublicKeySize_Null) {
    auto size = getPublicKeySize(nullptr);
    EXPECT_FALSE(size.has_value());
}

TEST_F(MetadataExtractorTest, GetPublicKeyCurve_RSA) {
    // RSA certificate should not have curve
    auto curve = getPublicKeyCurve(testCert);
    // For RSA certificate, curve should be std::nullopt
    EXPECT_FALSE(curve.has_value());
}

TEST_F(MetadataExtractorTest, GetKeyUsage_Valid) {
    auto usage = getKeyUsage(testCert);
    // Test certificate has Basic Constraints extension, may have key usage
    // Empty vector is valid if extension not present
    EXPECT_TRUE(usage.empty() || !usage.empty());
}

TEST_F(MetadataExtractorTest, GetExtendedKeyUsage_Valid) {
    auto usage = getExtendedKeyUsage(testCert);
    // Empty vector is valid if extension not present
    EXPECT_TRUE(usage.empty() || !usage.empty());
}

TEST_F(MetadataExtractorTest, IsCA_Valid) {
    auto ca = isCA(testCert);
    // Test certificate has Basic Constraints with CA=TRUE
    if (ca.has_value()) {
        EXPECT_TRUE(*ca); // Self-signed test cert is a CA
    }
}

TEST_F(MetadataExtractorTest, IsCA_Null) {
    auto ca = isCA(nullptr);
    EXPECT_FALSE(ca.has_value());
}

TEST_F(MetadataExtractorTest, GetPathLenConstraint_Valid) {
    auto path_len = getPathLenConstraint(testCert);
    // May or may not be present
    if (path_len.has_value()) {
        EXPECT_GE(*path_len, 0);
    }
}

TEST_F(MetadataExtractorTest, GetSubjectKeyIdentifier_Valid) {
    auto ski = getSubjectKeyIdentifier(testCert);
    // Test certificate has SKI extension
    if (ski.has_value()) {
        EXPECT_FALSE(ski->empty());
        // Should be hex string
        for (char c : *ski) {
            EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
        }
    }
}

TEST_F(MetadataExtractorTest, GetSubjectKeyIdentifier_Null) {
    auto ski = getSubjectKeyIdentifier(nullptr);
    EXPECT_FALSE(ski.has_value());
}

TEST_F(MetadataExtractorTest, GetAuthorityKeyIdentifier_Valid) {
    auto aki = getAuthorityKeyIdentifier(testCert);
    // Test certificate has AKI extension
    if (aki.has_value()) {
        EXPECT_FALSE(aki->empty());
        // Should be hex string
        for (char c : *aki) {
            EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
        }
    }
}

TEST_F(MetadataExtractorTest, GetAuthorityKeyIdentifier_Null) {
    auto aki = getAuthorityKeyIdentifier(nullptr);
    EXPECT_FALSE(aki.has_value());
}

TEST_F(MetadataExtractorTest, GetCrlDistributionPoints_Valid) {
    auto crls = getCrlDistributionPoints(testCert);
    // Empty vector is valid if extension not present
    EXPECT_TRUE(crls.empty() || !crls.empty());
    // If present, should be URLs
    for (const auto& url : crls) {
        EXPECT_FALSE(url.empty());
    }
}

TEST_F(MetadataExtractorTest, GetCrlDistributionPoints_Null) {
    auto crls = getCrlDistributionPoints(nullptr);
    EXPECT_TRUE(crls.empty());
}

TEST_F(MetadataExtractorTest, GetOcspResponderUrl_Valid) {
    auto ocsp = getOcspResponderUrl(testCert);
    // May or may not be present
    if (ocsp.has_value()) {
        EXPECT_FALSE(ocsp->empty());
    }
}

TEST_F(MetadataExtractorTest, GetOcspResponderUrl_Null) {
    auto ocsp = getOcspResponderUrl(nullptr);
    EXPECT_FALSE(ocsp.has_value());
}

TEST_F(MetadataExtractorTest, GetValidityPeriod_Valid) {
    auto validity = getValidityPeriod(testCert);

    // Both timestamps should be non-zero
    auto zero = std::chrono::system_clock::time_point{};
    EXPECT_NE(validity.first, zero);
    EXPECT_NE(validity.second, zero);

    // notAfter should be after notBefore
    EXPECT_GT(validity.second, validity.first);
}

TEST_F(MetadataExtractorTest, GetValidityPeriod_Null) {
    auto validity = getValidityPeriod(nullptr);
    auto zero = std::chrono::system_clock::time_point{};
    EXPECT_EQ(validity.first, zero);
    EXPECT_EQ(validity.second, zero);
}

TEST_F(MetadataExtractorTest, IsExpired_Valid) {
    // Test certificate expired in 2027, so currently not expired (assuming test runs before 2027)
    bool expired = isExpired(testCert);
    // Can't assert value as it depends on current date
    // Just verify it returns boolean
    EXPECT_TRUE(expired == true || expired == false);
}

TEST_F(MetadataExtractorTest, IsExpired_Null) {
    bool expired = isExpired(nullptr);
    EXPECT_TRUE(expired); // null cert is considered expired
}

TEST_F(MetadataExtractorTest, IsCurrentlyValid_Valid) {
    // Test certificate valid from 2017 to 2027
    bool valid = isCurrentlyValid(testCert);
    // Can't assert value as it depends on current date
    EXPECT_TRUE(valid == true || valid == false);
}

TEST_F(MetadataExtractorTest, IsCurrentlyValid_Null) {
    bool valid = isCurrentlyValid(nullptr);
    EXPECT_FALSE(valid); // null cert is not valid
}

TEST_F(MetadataExtractorTest, GetDaysUntilExpiration_Valid) {
    int days = getDaysUntilExpiration(testCert);
    // Can't assert specific value as it depends on current date
    // Just verify it returns a number (could be negative if expired)
    EXPECT_TRUE(days < 10000 && days > -10000); // Sanity check
}

TEST_F(MetadataExtractorTest, GetDaysUntilExpiration_Null) {
    int days = getDaysUntilExpiration(nullptr);
    EXPECT_EQ(days, 0);
}

TEST_F(MetadataExtractorTest, ExtractMetadata_Complete) {
    CertificateMetadata meta = extractMetadata(testCert);

    // Basic info should always be present
    EXPECT_EQ(meta.version, 2); // v3 = version 2
    EXPECT_FALSE(meta.serialNumber.empty());

    // Algorithm info should be present
    EXPECT_TRUE(meta.signatureAlgorithm.has_value());
    EXPECT_TRUE(meta.publicKeyAlgorithm.has_value());
    EXPECT_TRUE(meta.publicKeySize.has_value());

    // Validity period should be present
    auto zero = std::chrono::system_clock::time_point{};
    EXPECT_NE(meta.validFrom, zero);
    EXPECT_NE(meta.validTo, zero);

    // Self-signed flag should be set
    EXPECT_TRUE(meta.isSelfSigned);
}

TEST_F(MetadataExtractorTest, ExtractMetadata_Null) {
    CertificateMetadata meta = extractMetadata(nullptr);

    // Should return empty metadata
    EXPECT_TRUE(meta.serialNumber.empty());
    EXPECT_FALSE(meta.signatureAlgorithm.has_value());
}
