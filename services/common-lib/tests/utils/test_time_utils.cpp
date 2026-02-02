/**
 * @file test_time_utils.cpp
 * @brief Unit tests for time utility functions
 */

#include <gtest/gtest.h>
#include <icao/utils/time_utils.h>
#include <icao/x509/certificate_parser.h>
#include <openssl/x509.h>

using namespace icao::utils;
using namespace icao::x509;

class TimeUtilsTest : public ::testing::Test {
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

TEST_F(TimeUtilsTest, Asn1TimeToIso8601_NotBefore) {
    const ASN1_TIME* notBefore = X509_get0_notBefore(testCert);
    ASSERT_NE(notBefore, nullptr);
    
    std::string iso = asn1TimeToIso8601(notBefore);
    
    // Expected: 2022-06-14T15:15:09Z
    EXPECT_FALSE(iso.empty());
    EXPECT_TRUE(iso.find("2022-06-14T") != std::string::npos);
    EXPECT_TRUE(iso.find("Z") != std::string::npos);
}

TEST_F(TimeUtilsTest, Asn1TimeToIso8601_NotAfter) {
    const ASN1_TIME* notAfter = X509_get0_notAfter(testCert);
    ASSERT_NE(notAfter, nullptr);
    
    std::string iso = asn1TimeToIso8601(notAfter);
    
    // Expected: 2032-06-14T15:45:09Z
    EXPECT_FALSE(iso.empty());
    EXPECT_TRUE(iso.find("2032-06-14T") != std::string::npos);
    EXPECT_TRUE(iso.find("Z") != std::string::npos);
}

TEST_F(TimeUtilsTest, Asn1TimeToIso8601_Null) {
    std::string iso = asn1TimeToIso8601(nullptr);
    EXPECT_EQ(iso, "");
}

TEST_F(TimeUtilsTest, Asn1TimeToTimePoint_NotBefore) {
    const ASN1_TIME* notBefore = X509_get0_notBefore(testCert);
    ASSERT_NE(notBefore, nullptr);
    
    auto timePoint = asn1TimeToTimePoint(notBefore);
    
    // Verify it's not zero
    auto epoch = std::chrono::system_clock::time_point{};
    EXPECT_NE(timePoint, epoch);
    
    // Verify it's in the past (certificate issued in 2022)
    auto now = std::chrono::system_clock::now();
    EXPECT_LT(timePoint, now);
}

TEST_F(TimeUtilsTest, Asn1TimeToTimePoint_NotAfter) {
    const ASN1_TIME* notAfter = X509_get0_notAfter(testCert);
    ASSERT_NE(notAfter, nullptr);
    
    auto timePoint = asn1TimeToTimePoint(notAfter);
    
    // Verify it's not zero
    auto epoch = std::chrono::system_clock::time_point{};
    EXPECT_NE(timePoint, epoch);
    
    // Verify it's in the future (certificate expires in 2032)
    auto now = std::chrono::system_clock::now();
    EXPECT_GT(timePoint, now);
}

TEST_F(TimeUtilsTest, Asn1TimeToTimePoint_Null) {
    auto timePoint = asn1TimeToTimePoint(nullptr);
    auto epoch = std::chrono::system_clock::time_point{};
    EXPECT_EQ(timePoint, epoch);
}

TEST_F(TimeUtilsTest, Asn1TimeToTimePoint_ValidityPeriod) {
    const ASN1_TIME* notBefore = X509_get0_notBefore(testCert);
    const ASN1_TIME* notAfter = X509_get0_notAfter(testCert);
    
    auto beforeTp = asn1TimeToTimePoint(notBefore);
    auto afterTp = asn1TimeToTimePoint(notAfter);
    
    // notAfter must be after notBefore
    EXPECT_GT(afterTp, beforeTp);
    
    // Calculate duration (should be approximately 10 years)
    auto duration = afterTp - beforeTp;
    auto years = std::chrono::duration_cast<std::chrono::hours>(duration).count() / 24 / 365;
    
    EXPECT_GE(years, 9);  // At least 9 years
    EXPECT_LE(years, 11); // At most 11 years
}

TEST_F(TimeUtilsTest, Asn1IntegerToHex_SerialNumber) {
    const ASN1_INTEGER* serial = X509_get0_serialNumber(testCert);
    ASSERT_NE(serial, nullptr);

    std::string serialHex = asn1IntegerToHex(serial);

    // Expected: 5996e258 (actual UN CSCA certificate serial number)
    EXPECT_FALSE(serialHex.empty());
    EXPECT_EQ(serialHex, "5996e258");
}

TEST_F(TimeUtilsTest, Asn1IntegerToHex_Null) {
    std::string hex = asn1IntegerToHex(nullptr);
    EXPECT_EQ(hex, "");
}

TEST_F(TimeUtilsTest, Asn1IntegerToHex_Lowercase) {
    const ASN1_INTEGER* serial = X509_get0_serialNumber(testCert);
    ASSERT_NE(serial, nullptr);
    
    std::string serialHex = asn1IntegerToHex(serial);
    
    // Verify all characters are lowercase
    for (char c : serialHex) {
        if (std::isalpha(c)) {
            EXPECT_TRUE(std::islower(c));
        }
    }
}

// Round-trip test: ISO8601 string format consistency
TEST_F(TimeUtilsTest, Iso8601Format_Consistency) {
    const ASN1_TIME* notBefore = X509_get0_notBefore(testCert);
    
    std::string iso1 = asn1TimeToIso8601(notBefore);
    std::string iso2 = asn1TimeToIso8601(notBefore);
    
    EXPECT_EQ(iso1, iso2);
}

// Verify time_point conversion consistency
TEST_F(TimeUtilsTest, TimePoint_Consistency) {
    const ASN1_TIME* notBefore = X509_get0_notBefore(testCert);
    
    auto tp1 = asn1TimeToTimePoint(notBefore);
    auto tp2 = asn1TimeToTimePoint(notBefore);
    
    EXPECT_EQ(tp1, tp2);
}
