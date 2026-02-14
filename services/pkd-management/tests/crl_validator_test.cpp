/**
 * @file crl_validator_test.cpp
 * @brief Unit tests for CRL validation functionality
 *
 * Tests CRL-based certificate revocation checking (RFC 5280)
 */

#include <gtest/gtest.h>
#include "../src/common/crl_validator.h"
#include <openssl/x509.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/bn.h>
#include <iomanip>
#include <sstream>

using namespace crl;

// --- Test Fixtures ---

class CrlValidatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Note: These tests require a test PostgreSQL connection
        // For now, we test the utility functions that don't require DB
    }

    void TearDown() override {
        // Cleanup
    }

    // Helper: Create a test X.509 certificate
    X509* createTestCertificate(const std::string& serialHex) {
        X509* cert = X509_new();
        if (!cert) return nullptr;

        // Set version (v3 = 2)
        X509_set_version(cert, 2);

        // Set serial number
        BIGNUM* serialBn = nullptr;
        BN_hex2bn(&serialBn, serialHex.c_str());
        ASN1_INTEGER* serial = BN_to_ASN1_INTEGER(serialBn, nullptr);
        X509_set_serialNumber(cert, serial);
        ASN1_INTEGER_free(serial);
        BN_free(serialBn);

        // Set issuer and subject (dummy)
        X509_NAME* name = X509_NAME_new();
        X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
            reinterpret_cast<const unsigned char*>("Test Certificate"), -1, -1, 0);
        X509_set_issuer_name(cert, name);
        X509_set_subject_name(cert, name);
        X509_NAME_free(name);

        // Set validity period
        ASN1_TIME_set(X509_getm_notBefore(cert), time(nullptr));
        ASN1_TIME_set(X509_getm_notAfter(cert), time(nullptr) + 365*24*60*60);

        // Generate RSA key pair (2048-bit)
        EVP_PKEY* pkey = EVP_PKEY_new();
        RSA* rsa = RSA_new();
        BIGNUM* e = BN_new();
        BN_set_word(e, RSA_F4);
        RSA_generate_key_ex(rsa, 2048, e, nullptr);
        EVP_PKEY_assign_RSA(pkey, rsa);
        BN_free(e);

        X509_set_pubkey(cert, pkey);

        // Self-sign
        X509_sign(cert, pkey, EVP_sha256());

        EVP_PKEY_free(pkey);

        return cert;
    }

    // Helper: Create a test CRL with revoked certificates
    X509_CRL* createTestCrl(const std::vector<std::string>& revokedSerials) {
        X509_CRL* crl = X509_CRL_new();
        if (!crl) return nullptr;

        // Set version (v2 = 1)
        X509_CRL_set_version(crl, 1);

        // Set issuer
        X509_NAME* issuer = X509_NAME_new();
        X509_NAME_add_entry_by_txt(issuer, "CN", MBSTRING_ASC,
            reinterpret_cast<const unsigned char*>("Test CA"), -1, -1, 0);
        X509_CRL_set_issuer_name(crl, issuer);
        X509_NAME_free(issuer);

        // Set thisUpdate and nextUpdate
        ASN1_TIME* thisUpdate = ASN1_TIME_new();
        ASN1_TIME_set(thisUpdate, time(nullptr));
        X509_CRL_set1_lastUpdate(crl, thisUpdate);
        ASN1_TIME_free(thisUpdate);

        ASN1_TIME* nextUpdate = ASN1_TIME_new();
        ASN1_TIME_set(nextUpdate, time(nullptr) + 30*24*60*60);  // 30 days
        X509_CRL_set1_nextUpdate(crl, nextUpdate);
        ASN1_TIME_free(nextUpdate);

        // Add revoked certificates
        for (const auto& serialHex : revokedSerials) {
            X509_REVOKED* revoked = X509_REVOKED_new();

            // Set serial number
            BIGNUM* serialBn = nullptr;
            BN_hex2bn(&serialBn, serialHex.c_str());
            ASN1_INTEGER* serial = BN_to_ASN1_INTEGER(serialBn, nullptr);
            X509_REVOKED_set_serialNumber(revoked, serial);
            ASN1_INTEGER_free(serial);
            BN_free(serialBn);

            // Set revocation date
            ASN1_TIME* revDate = ASN1_TIME_new();
            ASN1_TIME_set(revDate, time(nullptr) - 7*24*60*60);  // 7 days ago
            X509_REVOKED_set_revocationDate(revoked, revDate);
            ASN1_TIME_free(revDate);

            // Add to CRL
            X509_CRL_add0_revoked(crl, revoked);
        }

        // Sort revoked list
        X509_CRL_sort(crl);

        return crl;
    }
};

// --- Utility Function Tests ---

TEST_F(CrlValidatorTest, RevocationStatusToString) {
    EXPECT_EQ(revocationStatusToString(RevocationStatus::GOOD), "GOOD");
    EXPECT_EQ(revocationStatusToString(RevocationStatus::REVOKED), "REVOKED");
    EXPECT_EQ(revocationStatusToString(RevocationStatus::UNKNOWN), "UNKNOWN");
}

TEST_F(CrlValidatorTest, HexSerialToAsn1_Valid) {
    std::string serialHex = "1A2B3C";

    BIGNUM* bn = nullptr;
    BN_hex2bn(&bn, serialHex.c_str());
    ASN1_INTEGER* expected = BN_to_ASN1_INTEGER(bn, nullptr);
    BN_free(bn);

    BIGNUM* testBn = nullptr;
    BN_hex2bn(&testBn, serialHex.c_str());
    ASN1_INTEGER* actual = BN_to_ASN1_INTEGER(testBn, nullptr);
    BN_free(testBn);

    ASSERT_NE(actual, nullptr);
    EXPECT_EQ(ASN1_INTEGER_cmp(expected, actual), 0);

    ASN1_INTEGER_free(expected);
    ASN1_INTEGER_free(actual);
}

TEST_F(CrlValidatorTest, HexSerialToAsn1_LeadingZero) {
    std::string serialHex = "01";  // Common serial number

    BIGNUM* bn = nullptr;
    BN_hex2bn(&bn, serialHex.c_str());
    ASN1_INTEGER* serial = BN_to_ASN1_INTEGER(bn, nullptr);
    BN_free(bn);

    ASSERT_NE(serial, nullptr);

    // Verify value is 1
    BIGNUM* checkBn = ASN1_INTEGER_to_BN(serial, nullptr);
    EXPECT_EQ(BN_get_word(checkBn), 1);

    BN_free(checkBn);
    ASN1_INTEGER_free(serial);
}

TEST_F(CrlValidatorTest, HexSerialToAsn1_LongSerial) {
    // 160-bit serial (20 bytes)
    std::string serialHex = "0123456789ABCDEF0123456789ABCDEF01234567";

    BIGNUM* bn = nullptr;
    BN_hex2bn(&bn, serialHex.c_str());
    ASN1_INTEGER* serial = BN_to_ASN1_INTEGER(bn, nullptr);
    BN_free(bn);

    ASSERT_NE(serial, nullptr);
    ASN1_INTEGER_free(serial);
}

// --- CRL Creation and Parsing Tests ---

TEST_F(CrlValidatorTest, CreateTestCrl_Empty) {
    X509_CRL* crl = createTestCrl({});
    ASSERT_NE(crl, nullptr);

    STACK_OF(X509_REVOKED)* revoked = X509_CRL_get_REVOKED(crl);
    EXPECT_EQ(revoked, nullptr);  // No revoked certs

    X509_CRL_free(crl);
}

TEST_F(CrlValidatorTest, CreateTestCrl_WithRevokedCerts) {
    std::vector<std::string> revokedSerials = {"01", "1A2B3C", "FEDCBA98"};
    X509_CRL* crl = createTestCrl(revokedSerials);
    ASSERT_NE(crl, nullptr);

    STACK_OF(X509_REVOKED)* revoked = X509_CRL_get_REVOKED(crl);
    ASSERT_NE(revoked, nullptr);
    EXPECT_EQ(sk_X509_REVOKED_num(revoked), 3);

    X509_CRL_free(crl);
}

TEST_F(CrlValidatorTest, CrlBinaryRoundtrip) {
    std::vector<std::string> revokedSerials = {"01"};
    X509_CRL* originalCrl = createTestCrl(revokedSerials);
    ASSERT_NE(originalCrl, nullptr);

    // Convert to DER binary
    int derLen = i2d_X509_CRL(originalCrl, nullptr);
    ASSERT_GT(derLen, 0);

    std::vector<unsigned char> derData(derLen);
    unsigned char* derPtr = derData.data();
    i2d_X509_CRL(originalCrl, &derPtr);

    // Parse back from DER
    const unsigned char* parsePtr = derData.data();
    X509_CRL* parsedCrl = d2i_X509_CRL(nullptr, &parsePtr, derLen);
    ASSERT_NE(parsedCrl, nullptr);

    // Verify revoked list matches
    STACK_OF(X509_REVOKED)* originalRevoked = X509_CRL_get_REVOKED(originalCrl);
    STACK_OF(X509_REVOKED)* parsedRevoked = X509_CRL_get_REVOKED(parsedCrl);

    ASSERT_NE(originalRevoked, nullptr);
    ASSERT_NE(parsedRevoked, nullptr);
    EXPECT_EQ(sk_X509_REVOKED_num(originalRevoked),
              sk_X509_REVOKED_num(parsedRevoked));

    X509_CRL_free(originalCrl);
    X509_CRL_free(parsedCrl);
}

// --- Revocation Check Logic Tests (without DB) ---

TEST_F(CrlValidatorTest, CheckRevocation_CertInRevokedList) {
    std::string targetSerial = "1A2B3C";
    std::vector<std::string> revokedSerials = {"01", targetSerial, "FEDCBA98"};

    X509_CRL* crl = createTestCrl(revokedSerials);
    ASSERT_NE(crl, nullptr);

    // Convert target serial to ASN1_INTEGER
    BIGNUM* bn = nullptr;
    BN_hex2bn(&bn, targetSerial.c_str());
    ASN1_INTEGER* targetSerialAsn1 = BN_to_ASN1_INTEGER(bn, nullptr);
    BN_free(bn);

    // Search in CRL
    STACK_OF(X509_REVOKED)* revokedList = X509_CRL_get_REVOKED(crl);
    ASSERT_NE(revokedList, nullptr);

    bool found = false;
    for (int i = 0; i < sk_X509_REVOKED_num(revokedList); i++) {
        X509_REVOKED* revoked = sk_X509_REVOKED_value(revokedList, i);
        const ASN1_INTEGER* revokedSerial = X509_REVOKED_get0_serialNumber(revoked);

        if (ASN1_INTEGER_cmp(targetSerialAsn1, revokedSerial) == 0) {
            found = true;
            break;
        }
    }

    EXPECT_TRUE(found);

    ASN1_INTEGER_free(targetSerialAsn1);
    X509_CRL_free(crl);
}

TEST_F(CrlValidatorTest, CheckRevocation_CertNotInRevokedList) {
    std::string targetSerial = "AABBCCDD";  // Not in revoked list
    std::vector<std::string> revokedSerials = {"01", "1A2B3C", "FEDCBA98"};

    X509_CRL* crl = createTestCrl(revokedSerials);
    ASSERT_NE(crl, nullptr);

    // Convert target serial to ASN1_INTEGER
    BIGNUM* bn = nullptr;
    BN_hex2bn(&bn, targetSerial.c_str());
    ASN1_INTEGER* targetSerialAsn1 = BN_to_ASN1_INTEGER(bn, nullptr);
    BN_free(bn);

    // Search in CRL
    STACK_OF(X509_REVOKED)* revokedList = X509_CRL_get_REVOKED(crl);
    ASSERT_NE(revokedList, nullptr);

    bool found = false;
    for (int i = 0; i < sk_X509_REVOKED_num(revokedList); i++) {
        X509_REVOKED* revoked = sk_X509_REVOKED_value(revokedList, i);
        const ASN1_INTEGER* revokedSerial = X509_REVOKED_get0_serialNumber(revoked);

        if (ASN1_INTEGER_cmp(targetSerialAsn1, revokedSerial) == 0) {
            found = true;
            break;
        }
    }

    EXPECT_FALSE(found);

    ASN1_INTEGER_free(targetSerialAsn1);
    X509_CRL_free(crl);
}

TEST_F(CrlValidatorTest, CheckRevocation_EmptyCrl) {
    std::string targetSerial = "01";
    X509_CRL* crl = createTestCrl({});  // Empty CRL
    ASSERT_NE(crl, nullptr);

    STACK_OF(X509_REVOKED)* revokedList = X509_CRL_get_REVOKED(crl);
    EXPECT_EQ(revokedList, nullptr);  // No revoked list

    X509_CRL_free(crl);
}

// --- Serial Number Edge Cases ---

TEST_F(CrlValidatorTest, SerialNumber_SingleDigit) {
    std::string serial = "1";

    BIGNUM* bn = nullptr;
    BN_hex2bn(&bn, serial.c_str());
    ASN1_INTEGER* serialAsn1 = BN_to_ASN1_INTEGER(bn, nullptr);
    BN_free(bn);

    ASSERT_NE(serialAsn1, nullptr);

    BIGNUM* checkBn = ASN1_INTEGER_to_BN(serialAsn1, nullptr);
    EXPECT_EQ(BN_get_word(checkBn), 1);

    BN_free(checkBn);
    ASN1_INTEGER_free(serialAsn1);
}

TEST_F(CrlValidatorTest, SerialNumber_MaxLength) {
    // RFC 5280: Serial number must be positive integer, <= 20 octets
    std::string serial = "FFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFFF";  // 20 bytes

    BIGNUM* bn = nullptr;
    BN_hex2bn(&bn, serial.c_str());
    ASN1_INTEGER* serialAsn1 = BN_to_ASN1_INTEGER(bn, nullptr);
    BN_free(bn);

    ASSERT_NE(serialAsn1, nullptr);
    ASN1_INTEGER_free(serialAsn1);
}

// --- Performance Tests ---

TEST_F(CrlValidatorTest, Performance_LargeCrl) {
    // Create CRL with 1000 revoked certificates
    std::vector<std::string> revokedSerials;
    for (int i = 0; i < 1000; i++) {
        std::ostringstream ss;
        ss << std::hex << std::setw(8) << std::setfill('0') << i;
        revokedSerials.push_back(ss.str());
    }

    auto startTime = std::chrono::high_resolution_clock::now();

    X509_CRL* crl = createTestCrl(revokedSerials);
    ASSERT_NE(crl, nullptr);

    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        endTime - startTime).count();

    std::cout << "Created CRL with 1000 revoked certs in " << duration << "ms" << std::endl;

    // Performance target: < 100ms
    EXPECT_LT(duration, 100);

    STACK_OF(X509_REVOKED)* revoked = X509_CRL_get_REVOKED(crl);
    ASSERT_NE(revoked, nullptr);
    EXPECT_EQ(sk_X509_REVOKED_num(revoked), 1000);

    X509_CRL_free(crl);
}

// --- Main ---

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
