/**
 * @file test_file_detector.cpp
 * @brief Unit tests for file format detector (file_detector.h / file_detector.cpp)
 */

#include <gtest/gtest.h>
#include "file_detector.h"
#include "test_helpers.h"
#include <vector>
#include <string>
#include <cstdint>

using namespace icao::certificate_parser;

// ---------------------------------------------------------------------------
// Extension-based detection
// ---------------------------------------------------------------------------

class FileDetectorExtensionTest : public ::testing::Test {};

TEST_F(FileDetectorExtensionTest, DetectPem_ByExtension) {
    std::vector<uint8_t> empty;
    EXPECT_EQ(FileDetector::detectFormat("cert.pem", empty), FileFormat::PEM);
    EXPECT_EQ(FileDetector::detectFormat("cert.crt", empty), FileFormat::PEM);
}

TEST_F(FileDetectorExtensionTest, DetectDer_ByExtension) {
    std::vector<uint8_t> empty;
    EXPECT_EQ(FileDetector::detectFormat("cert.der", empty), FileFormat::DER);
}

TEST_F(FileDetectorExtensionTest, DetectCer_ByExtension) {
    std::vector<uint8_t> empty;
    EXPECT_EQ(FileDetector::detectFormat("cert.cer", empty), FileFormat::CER);
}

TEST_F(FileDetectorExtensionTest, DetectBin_ByExtension) {
    std::vector<uint8_t> empty;
    EXPECT_EQ(FileDetector::detectFormat("cert.bin", empty), FileFormat::BIN);
}

TEST_F(FileDetectorExtensionTest, DetectDl_ByExtension) {
    std::vector<uint8_t> empty;
    EXPECT_EQ(FileDetector::detectFormat("deviations.dvl", empty), FileFormat::DL);
    EXPECT_EQ(FileDetector::detectFormat("deviations.dl", empty), FileFormat::DL);
}

TEST_F(FileDetectorExtensionTest, DetectLdif_ByExtension) {
    std::vector<uint8_t> empty;
    EXPECT_EQ(FileDetector::detectFormat("data.ldif", empty), FileFormat::LDIF);
}

TEST_F(FileDetectorExtensionTest, DetectMl_ByExtension) {
    std::vector<uint8_t> empty;
    EXPECT_EQ(FileDetector::detectFormat("masterlist.ml", empty), FileFormat::ML);
}

TEST_F(FileDetectorExtensionTest, DetectP7b_ByExtension) {
    std::vector<uint8_t> empty;
    EXPECT_EQ(FileDetector::detectFormat("bundle.p7b", empty), FileFormat::P7B);
    EXPECT_EQ(FileDetector::detectFormat("bundle.p7c", empty), FileFormat::P7B);
}

TEST_F(FileDetectorExtensionTest, DetectCrl_ByExtension) {
    std::vector<uint8_t> empty;
    EXPECT_EQ(FileDetector::detectFormat("revocations.crl", empty), FileFormat::CRL);
}

TEST_F(FileDetectorExtensionTest, CaseInsensitiveExtension) {
    std::vector<uint8_t> empty;
    EXPECT_EQ(FileDetector::detectFormat("cert.PEM", empty), FileFormat::PEM);
    EXPECT_EQ(FileDetector::detectFormat("cert.Der", empty), FileFormat::DER);
    EXPECT_EQ(FileDetector::detectFormat("data.LDIF", empty), FileFormat::LDIF);
}

TEST_F(FileDetectorExtensionTest, UnknownExtension) {
    std::vector<uint8_t> empty;
    EXPECT_EQ(FileDetector::detectFormat("file.xyz", empty), FileFormat::UNKNOWN);
    EXPECT_EQ(FileDetector::detectFormat("file.txt", empty), FileFormat::UNKNOWN);
}

TEST_F(FileDetectorExtensionTest, NoExtension) {
    std::vector<uint8_t> empty;
    EXPECT_EQ(FileDetector::detectFormat("certificate", empty), FileFormat::UNKNOWN);
}

// ---------------------------------------------------------------------------
// Content-based detection
// ---------------------------------------------------------------------------

class FileDetectorContentTest : public ::testing::Test {
protected:
    void SetUp() override {
        cert_ = test_helpers::createSelfSignedCert("US", "Content Test");
        ASSERT_NE(cert_, nullptr);
    }

    void TearDown() override {
        if (cert_) X509_free(cert_);
    }

    X509* cert_ = nullptr;
};

TEST_F(FileDetectorContentTest, DetectPem_ByContent) {
    std::string pem = test_helpers::certToPem(cert_);
    std::vector<uint8_t> data(pem.begin(), pem.end());

    // Use unknown extension to force content detection
    EXPECT_EQ(FileDetector::detectFormat("file.unknown", data), FileFormat::PEM);
}

TEST_F(FileDetectorContentTest, DetectDer_ByContent) {
    std::vector<uint8_t> der = test_helpers::certToDer(cert_);

    EXPECT_EQ(FileDetector::detectFormat("file.unknown", der), FileFormat::DER);
}

TEST_F(FileDetectorContentTest, DetectLdif_ByContent_Dn) {
    std::string ldif = "dn: cn=test,dc=example,dc=com\nobjectClass: top\n";
    std::vector<uint8_t> data(ldif.begin(), ldif.end());

    EXPECT_EQ(FileDetector::detectFormat("file.unknown", data), FileFormat::LDIF);
}

TEST_F(FileDetectorContentTest, DetectLdif_ByContent_Version) {
    std::string ldif = "version: 1\ndn: cn=test,dc=example,dc=com\n";
    std::vector<uint8_t> data(ldif.begin(), ldif.end());

    EXPECT_EQ(FileDetector::detectFormat("file.unknown", data), FileFormat::LDIF);
}

TEST_F(FileDetectorContentTest, DetectCrlPem_ByContent) {
    std::string crlPem = "-----BEGIN X509 CRL-----\ndata\n-----END X509 CRL-----\n";
    std::vector<uint8_t> data(crlPem.begin(), crlPem.end());

    EXPECT_EQ(FileDetector::detectFormat("file.unknown", data), FileFormat::PEM);
}

TEST_F(FileDetectorContentTest, EmptyContent_UnknownExtension) {
    std::vector<uint8_t> empty;
    EXPECT_EQ(FileDetector::detectFormat("file.unknown", empty), FileFormat::UNKNOWN);
}

// ---------------------------------------------------------------------------
// formatToString / stringToFormat
// ---------------------------------------------------------------------------

class FileDetectorConversionTest : public ::testing::Test {};

TEST_F(FileDetectorConversionTest, FormatToString_AllFormats) {
    EXPECT_EQ(FileDetector::formatToString(FileFormat::PEM), "PEM");
    EXPECT_EQ(FileDetector::formatToString(FileFormat::DER), "DER");
    EXPECT_EQ(FileDetector::formatToString(FileFormat::CER), "CER");
    EXPECT_EQ(FileDetector::formatToString(FileFormat::BIN), "BIN");
    EXPECT_EQ(FileDetector::formatToString(FileFormat::DL), "DL");
    EXPECT_EQ(FileDetector::formatToString(FileFormat::LDIF), "LDIF");
    EXPECT_EQ(FileDetector::formatToString(FileFormat::ML), "ML");
    EXPECT_EQ(FileDetector::formatToString(FileFormat::P7B), "P7B");
    EXPECT_EQ(FileDetector::formatToString(FileFormat::CRL), "CRL");
    EXPECT_EQ(FileDetector::formatToString(FileFormat::UNKNOWN), "UNKNOWN");
}

TEST_F(FileDetectorConversionTest, StringToFormat_AllFormats) {
    EXPECT_EQ(FileDetector::stringToFormat("PEM"), FileFormat::PEM);
    EXPECT_EQ(FileDetector::stringToFormat("DER"), FileFormat::DER);
    EXPECT_EQ(FileDetector::stringToFormat("CER"), FileFormat::CER);
    EXPECT_EQ(FileDetector::stringToFormat("BIN"), FileFormat::BIN);
    EXPECT_EQ(FileDetector::stringToFormat("DL"), FileFormat::DL);
    EXPECT_EQ(FileDetector::stringToFormat("LDIF"), FileFormat::LDIF);
    EXPECT_EQ(FileDetector::stringToFormat("ML"), FileFormat::ML);
    EXPECT_EQ(FileDetector::stringToFormat("P7B"), FileFormat::P7B);
    EXPECT_EQ(FileDetector::stringToFormat("CRL"), FileFormat::CRL);
}

TEST_F(FileDetectorConversionTest, StringToFormat_CaseInsensitive) {
    EXPECT_EQ(FileDetector::stringToFormat("pem"), FileFormat::PEM);
    EXPECT_EQ(FileDetector::stringToFormat("der"), FileFormat::DER);
    EXPECT_EQ(FileDetector::stringToFormat("Ldif"), FileFormat::LDIF);
}

TEST_F(FileDetectorConversionTest, StringToFormat_Unknown) {
    EXPECT_EQ(FileDetector::stringToFormat(""), FileFormat::UNKNOWN);
    EXPECT_EQ(FileDetector::stringToFormat("XYZ"), FileFormat::UNKNOWN);
}

TEST_F(FileDetectorConversionTest, RoundTrip_FormatToStringToFormat) {
    // Every format should survive a round trip
    FileFormat formats[] = {
        FileFormat::PEM, FileFormat::DER, FileFormat::CER,
        FileFormat::BIN, FileFormat::DL, FileFormat::LDIF,
        FileFormat::ML, FileFormat::P7B, FileFormat::CRL,
        FileFormat::UNKNOWN
    };

    for (auto fmt : formats) {
        std::string str = FileDetector::formatToString(fmt);
        FileFormat roundTripped = FileDetector::stringToFormat(str);
        EXPECT_EQ(roundTripped, fmt) << "Round trip failed for format: " << str;
    }
}

// ---------------------------------------------------------------------------
// Extension precedence over content
// ---------------------------------------------------------------------------

TEST_F(FileDetectorContentTest, ExtensionTakesPrecedenceOverContent) {
    // PEM content but .der extension -- extension wins
    std::string pem = test_helpers::certToPem(cert_);
    std::vector<uint8_t> data(pem.begin(), pem.end());

    EXPECT_EQ(FileDetector::detectFormat("cert.der", data), FileFormat::DER);
}
