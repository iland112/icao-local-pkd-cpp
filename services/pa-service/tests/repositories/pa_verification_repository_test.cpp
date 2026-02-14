/**
 * @file pa_verification_repository_test.cpp
 * @brief Unit tests for PaVerificationRepository
 *
 * Tests CRUD operations and parameterized query execution
 *
 * @author SmartCore Inc.
 * @date 2026-02-02
 */

#include <gtest/gtest.h>
#include <libpq-fe.h>
#include <memory>
#include "repositories/pa_verification_repository.h"
#include "domain/models/pa_verification.h"

namespace {

/**
 * @brief Test fixture for PaVerificationRepository
 *
 * Sets up a test database connection and cleans up test data after each test
 */
class PaVerificationRepositoryTest : public ::testing::Test {
protected:
    PGconn* conn_;
    std::unique_ptr<repositories::PaVerificationRepository> repository_;

    void SetUp() override {
        // Connect to test database
        const char* connStr = "host=postgres port=5432 dbname=localpkd user=pkd password=pkd_test_password_123";
        conn_ = PQconnectdb(connStr);

        if (PQstatus(conn_) != CONNECTION_OK) {
            FAIL() << "Database connection failed: " << PQerrorMessage(conn_);
        }

        repository_ = std::make_unique<repositories::PaVerificationRepository>(conn_);
    }

    void TearDown() override {
        // Clean up test data
        const char* cleanup = "DELETE FROM pa_verification WHERE mrz_document_number LIKE 'TEST%'";
        PGresult* res = PQexec(conn_, cleanup);
        PQclear(res);

        PQfinish(conn_);
    }

    /**
     * @brief Create a test PaVerification object
     */
    domain::models::PaVerification createTestVerification(const std::string& docNumber) {
        domain::models::PaVerification verification;
        verification.mrzDocumentNumber = docNumber;
        verification.mrzIssuingCountry = "KR";
        verification.mrzNationality = "KOR";
        verification.mrzDateOfBirth = "900101";
        verification.mrzGender = "M";
        verification.mrzExpiryDate = "301231";
        verification.overallStatus = "VALID";
        verification.aaStatus = "NOT_CHECKED";
        verification.createdAt = "2026-02-02T10:00:00Z";

        return verification;
    }
};

// --- INSERT Tests ---

TEST_F(PaVerificationRepositoryTest, InsertValidVerification) {
    // Arrange
    auto verification = createTestVerification("TEST001");

    // Act
    std::string id = repository_->insert(verification);

    // Assert
    EXPECT_FALSE(id.empty());
    EXPECT_EQ(id.length(), 36);  // UUID length

    // Verify in database
    auto retrieved = repository_->findById(id);
    EXPECT_FALSE(retrieved.isNull());
    EXPECT_EQ(retrieved["mrzDocumentNumber"].asString(), "TEST001");
    EXPECT_EQ(retrieved["mrzIssuingCountry"].asString(), "KR");
    EXPECT_EQ(retrieved["overallStatus"].asString(), "VALID");
}

TEST_F(PaVerificationRepositoryTest, InsertWithOptionalFields) {
    // Arrange
    auto verification = createTestVerification("TEST002");
    verification.sodHashAlgorithm = "SHA-256";
    verification.dataGroupsValid = true;
    verification.signatureValid = true;

    // Act
    std::string id = repository_->insert(verification);

    // Assert
    EXPECT_FALSE(id.empty());

    // Verify optional fields
    auto retrieved = repository_->findById(id);
    EXPECT_EQ(retrieved["sodHashAlgorithm"].asString(), "SHA-256");
    EXPECT_TRUE(retrieved["dataGroupsValid"].asBool());
    EXPECT_TRUE(retrieved["signatureValid"].asBool());
}

// --- FIND BY ID Tests ---

TEST_F(PaVerificationRepositoryTest, FindByIdExists) {
    // Arrange
    auto verification = createTestVerification("TEST003");
    std::string id = repository_->insert(verification);

    // Act
    auto result = repository_->findById(id);

    // Assert
    EXPECT_FALSE(result.isNull());
    EXPECT_EQ(result["id"].asString(), id);
    EXPECT_EQ(result["mrzDocumentNumber"].asString(), "TEST003");
}

TEST_F(PaVerificationRepositoryTest, FindByIdNotExists) {
    // Act
    auto result = repository_->findById("00000000-0000-0000-0000-000000000000");

    // Assert
    EXPECT_TRUE(result.isNull());
}

// --- FIND BY MRZ Tests ---

TEST_F(PaVerificationRepositoryTest, FindByMrzExists) {
    // Arrange
    auto verification = createTestVerification("TEST004");
    repository_->insert(verification);

    // Act
    auto result = repository_->findByMrz("TEST004", "900101", "301231");

    // Assert
    EXPECT_FALSE(result.isNull());
    EXPECT_EQ(result["mrzDocumentNumber"].asString(), "TEST004");
    EXPECT_EQ(result["mrzDateOfBirth"].asString(), "900101");
    EXPECT_EQ(result["mrzExpiryDate"].asString(), "301231");
}

TEST_F(PaVerificationRepositoryTest, FindByMrzNotExists) {
    // Act
    auto result = repository_->findByMrz("NONEXIST", "900101", "301231");

    // Assert
    EXPECT_TRUE(result.isNull());
}

// --- UPDATE STATUS Tests ---

TEST_F(PaVerificationRepositoryTest, UpdateStatusSuccess) {
    // Arrange
    auto verification = createTestVerification("TEST005");
    std::string id = repository_->insert(verification);

    // Act
    bool updated = repository_->updateStatus(id, "INVALID", "Data group hash mismatch");

    // Assert
    EXPECT_TRUE(updated);

    // Verify update
    auto result = repository_->findById(id);
    EXPECT_EQ(result["overallStatus"].asString(), "INVALID");
    EXPECT_EQ(result["failureReason"].asString(), "Data group hash mismatch");
}

TEST_F(PaVerificationRepositoryTest, UpdateStatusNotFound) {
    // Act
    bool updated = repository_->updateStatus("00000000-0000-0000-0000-000000000000", "INVALID", "");

    // Assert
    EXPECT_FALSE(updated);
}

// --- PAGINATION Tests ---

TEST_F(PaVerificationRepositoryTest, FindAllWithPagination) {
    // Arrange - Insert 5 test records
    for (int i = 0; i < 5; i++) {
        auto verification = createTestVerification("TESTPAGE" + std::to_string(i));
        repository_->insert(verification);
    }

    // Act - Get first 3 records
    auto result = repository_->findAll(3, 0);

    // Assert
    EXPECT_TRUE(result.isArray());
    EXPECT_EQ(result.size(), 3);
}

TEST_F(PaVerificationRepositoryTest, FindAllWithOffset) {
    // Arrange - Insert 5 test records
    for (int i = 0; i < 5; i++) {
        auto verification = createTestVerification("TESTOFF" + std::to_string(i));
        repository_->insert(verification);
    }

    // Act - Get records with offset
    auto page1 = repository_->findAll(2, 0);
    auto page2 = repository_->findAll(2, 2);

    // Assert
    EXPECT_EQ(page1.size(), 2);
    EXPECT_EQ(page2.size(), 2);
    // Ensure different records
    EXPECT_NE(page1[0]["id"].asString(), page2[0]["id"].asString());
}

// --- PARAMETERIZED QUERY SECURITY Tests ---

TEST_F(PaVerificationRepositoryTest, SqlInjectionPrevention) {
    // Arrange - Try SQL injection in document number
    auto verification = createTestVerification("TEST'; DROP TABLE pa_verification; --");

    // Act
    std::string id = repository_->insert(verification);

    // Assert - Should succeed without executing injection
    EXPECT_FALSE(id.empty());

    // Verify table still exists
    const char* query = "SELECT COUNT(*) FROM pa_verification";
    PGresult* res = PQexec(conn_, query);
    EXPECT_EQ(PQresultStatus(res), PGRES_TUPLES_OK);
    PQclear(res);
}

} // namespace
