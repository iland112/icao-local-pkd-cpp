/**
 * @file test_certificate_parser.cpp
 * @brief Unit tests for certificate parser
 */

#include <gtest/gtest.h>
#include <icao/x509/certificate_parser.h>
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <fstream>

using namespace icao::x509;

class CertificateParserTest : public ::testing::Test {
protected:
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
};

TEST_F(CertificateParserTest, DetectCertificateFormat_PEM) {
    std::vector<uint8_t> data(testCertPem.begin(), testCertPem.end());
    auto result = detectCertificateFormat(data);

    EXPECT_EQ(result.format, CertificateFormat::PEM);
    EXPECT_EQ(result.formatName, "PEM");
    EXPECT_FALSE(result.isBinary);
    EXPECT_FALSE(result.error.has_value());
}

TEST_F(CertificateParserTest, DetectCertificateFormat_DER) {
    // Create DER data (starts with 0x30 - SEQUENCE tag)
    std::vector<uint8_t> der_data = {0x30, 0x82, 0x03, 0x5d};
    auto result = detectCertificateFormat(der_data);

    EXPECT_EQ(result.format, CertificateFormat::DER);
    EXPECT_EQ(result.formatName, "DER");
    EXPECT_TRUE(result.isBinary);
    EXPECT_FALSE(result.error.has_value());
}

TEST_F(CertificateParserTest, DetectCertificateFormat_Empty) {
    std::vector<uint8_t> empty_data;
    auto result = detectCertificateFormat(empty_data);

    EXPECT_EQ(result.format, CertificateFormat::UNKNOWN);
    EXPECT_TRUE(result.error.has_value());
    EXPECT_EQ(*result.error, "Empty data");
}

TEST_F(CertificateParserTest, ParseCertificateFromPem_Valid) {
    X509* cert = parseCertificateFromPem(testCertPem);
    ASSERT_NE(cert, nullptr);

    // Verify certificate has subject and issuer
    X509_NAME* subject = X509_get_subject_name(cert);
    EXPECT_NE(subject, nullptr);

    X509_NAME* issuer = X509_get_issuer_name(cert);
    EXPECT_NE(issuer, nullptr);

    X509_free(cert);
}

TEST_F(CertificateParserTest, ParseCertificateFromPem_Empty) {
    X509* cert = parseCertificateFromPem("");
    EXPECT_EQ(cert, nullptr);
}

TEST_F(CertificateParserTest, ParseCertificateFromPem_Invalid) {
    std::string invalid_pem = "-----BEGIN CERTIFICATE-----\nInvalidData\n-----END CERTIFICATE-----";
    X509* cert = parseCertificateFromPem(invalid_pem);
    EXPECT_EQ(cert, nullptr);
}

TEST_F(CertificateParserTest, CertificateToPem_Valid) {
    // Parse certificate first
    X509* cert = parseCertificateFromPem(testCertPem);
    ASSERT_NE(cert, nullptr);

    // Convert back to PEM
    auto pem_result = certificateToPem(cert);
    ASSERT_TRUE(pem_result.has_value());

    // Result should contain PEM markers
    EXPECT_TRUE(pem_result->find("-----BEGIN CERTIFICATE-----") != std::string::npos);
    EXPECT_TRUE(pem_result->find("-----END CERTIFICATE-----") != std::string::npos);

    X509_free(cert);
}

TEST_F(CertificateParserTest, CertificateToPem_Null) {
    auto result = certificateToPem(nullptr);
    EXPECT_FALSE(result.has_value());
}

TEST_F(CertificateParserTest, CertificateToDer_Valid) {
    // Parse certificate first
    X509* cert = parseCertificateFromPem(testCertPem);
    ASSERT_NE(cert, nullptr);

    // Convert to DER
    auto der_result = certificateToDer(cert);
    EXPECT_FALSE(der_result.empty());

    // DER should start with SEQUENCE tag (0x30)
    EXPECT_EQ(der_result[0], 0x30);

    X509_free(cert);
}

TEST_F(CertificateParserTest, CertificateToDer_Null) {
    auto result = certificateToDer(nullptr);
    EXPECT_TRUE(result.empty());
}

TEST_F(CertificateParserTest, ComputeFingerprint_Valid) {
    // Parse certificate first
    X509* cert = parseCertificateFromPem(testCertPem);
    ASSERT_NE(cert, nullptr);

    // Compute fingerprint
    auto fingerprint = computeFingerprint(cert);
    ASSERT_TRUE(fingerprint.has_value());

    // SHA-256 fingerprint should be 64 hex characters
    EXPECT_EQ(fingerprint->length(), 64);

    // Should only contain hex characters (0-9, a-f)
    for (char c : *fingerprint) {
        EXPECT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'));
    }

    X509_free(cert);
}

TEST_F(CertificateParserTest, ComputeFingerprint_Null) {
    auto result = computeFingerprint(nullptr);
    EXPECT_FALSE(result.has_value());
}

TEST_F(CertificateParserTest, ValidateCertificateStructure_Valid) {
    X509* cert = parseCertificateFromPem(testCertPem);
    ASSERT_NE(cert, nullptr);

    EXPECT_TRUE(validateCertificateStructure(cert));

    X509_free(cert);
}

TEST_F(CertificateParserTest, ValidateCertificateStructure_Null) {
    EXPECT_FALSE(validateCertificateStructure(nullptr));
}

TEST_F(CertificateParserTest, CertificatePtr_RAII) {
    // Test RAII wrapper
    {
        X509* raw_cert = parseCertificateFromPem(testCertPem);
        CertificatePtr cert_ptr(raw_cert);

        EXPECT_TRUE(cert_ptr);
        EXPECT_NE(cert_ptr.get(), nullptr);

        // Certificate should be automatically freed when cert_ptr goes out of scope
    }

    // Test release
    {
        X509* raw_cert = parseCertificateFromPem(testCertPem);
        CertificatePtr cert_ptr(raw_cert);

        X509* released = cert_ptr.release();
        EXPECT_NE(released, nullptr);
        EXPECT_FALSE(cert_ptr);

        // Manual cleanup required after release
        X509_free(released);
    }

    // Test move semantics
    {
        X509* raw_cert = parseCertificateFromPem(testCertPem);
        CertificatePtr cert_ptr1(raw_cert);

        CertificatePtr cert_ptr2(std::move(cert_ptr1));
        EXPECT_FALSE(cert_ptr1);
        EXPECT_TRUE(cert_ptr2);
    }
}

TEST_F(CertificateParserTest, ParseCertificate_AutoDetect_PEM) {
    std::vector<uint8_t> data(testCertPem.begin(), testCertPem.end());
    X509* cert = parseCertificate(data);

    ASSERT_NE(cert, nullptr);
    EXPECT_TRUE(validateCertificateStructure(cert));

    X509_free(cert);
}

TEST_F(CertificateParserTest, ParseCertificateFromDer_Valid) {
    // First convert PEM to DER
    X509* cert_pem = parseCertificateFromPem(testCertPem);
    ASSERT_NE(cert_pem, nullptr);

    auto der_data = certificateToDer(cert_pem);
    X509_free(cert_pem);

    ASSERT_FALSE(der_data.empty());

    // Parse from DER
    X509* cert_der = parseCertificateFromDer(der_data);
    ASSERT_NE(cert_der, nullptr);

    EXPECT_TRUE(validateCertificateStructure(cert_der));

    X509_free(cert_der);
}

TEST_F(CertificateParserTest, ParseCertificateFromDer_Empty) {
    std::vector<uint8_t> empty_data;
    X509* cert = parseCertificateFromDer(empty_data);
    EXPECT_EQ(cert, nullptr);
}
