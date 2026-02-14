/**
 * @file data_group_repository_test.cpp
 * @brief Unit tests for DataGroupRepository
 *
 * Tests data group CRUD operations and binary data handling
 *
 * @author SmartCore Inc.
 * @date 2026-02-02
 */

#include <gtest/gtest.h>
#include <libpq-fe.h>
#include <memory>
#include <vector>
#include "repositories/data_group_repository.h"
#include "domain/models/data_group.h"

namespace {

/**
 * @brief Test fixture for DataGroupRepository
 */
class DataGroupRepositoryTest : public ::testing::Test {
protected:
    PGconn* conn_;
    std::unique_ptr<repositories::DataGroupRepository> repository_;
    std::string testVerificationId_;

    void SetUp() override {
        // Connect to test database
        const char* connStr = "host=postgres port=5432 dbname=localpkd user=pkd password=pkd_test_password_123";
        conn_ = PQconnectdb(connStr);

        if (PQstatus(conn_) != CONNECTION_OK) {
            FAIL() << "Database connection failed: " << PQerrorMessage(conn_);
        }

        repository_ = std::make_unique<repositories::DataGroupRepository>(conn_);

        // Create a test verification ID
        testVerificationId_ = createTestVerification();
    }

    void TearDown() override {
        // Clean up test data
        const char* cleanup1 = "DELETE FROM pa_data_group WHERE verification_id = $1";
        const char* params1[] = { testVerificationId_.c_str() };
        PGresult* res1 = PQexecParams(conn_, cleanup1, 1, nullptr, params1, nullptr, nullptr, 0);
        PQclear(res1);

        const char* cleanup2 = "DELETE FROM pa_verification WHERE id = $1";
        const char* params2[] = { testVerificationId_.c_str() };
        PGresult* res2 = PQexecParams(conn_, cleanup2, 1, nullptr, params2, nullptr, nullptr, 0);
        PQclear(res2);

        PQfinish(conn_);
    }

    /**
     * @brief Create a test verification record
     */
    std::string createTestVerification() {
        const char* query = R"SQL(
            INSERT INTO pa_verification (
                mrz_document_number, mrz_issuing_country, mrz_nationality,
                mrz_date_of_birth, mrz_gender, mrz_expiry_date, overall_status, aa_status
            ) VALUES (
                'TESTDG001', 'KR', 'KOR', '900101', 'M', '301231', 'VALID', 'NOT_CHECKED'
            ) RETURNING id
        )SQL";

        PGresult* res = PQexec(conn_, query);
        if (PQresultStatus(res) != PGRES_TUPLES_OK) {
            PQclear(res);
            return "";
        }

        std::string id = PQgetvalue(res, 0, 0);
        PQclear(res);
        return id;
    }

    /**
     * @brief Create a test DataGroup
     */
    domain::models::DataGroup createTestDataGroup(const std::string& dgNumber) {
        domain::models::DataGroup dg;
        dg.dgNumber = dgNumber;
        dg.expectedHash = "0123456789abcdef0123456789abcdef01234567";
        dg.actualHash = "0123456789abcdef0123456789abcdef01234567";
        dg.hashAlgorithm = "SHA-1";
        dg.hashValid = true;

        // Add some binary data
        std::vector<uint8_t> rawData = {0x30, 0x82, 0x01, 0x02, 0x06, 0x09};
        dg.rawData = rawData;

        return dg;
    }
};

// --- INSERT Tests ---

TEST_F(DataGroupRepositoryTest, InsertValidDataGroup) {
    // Arrange
    auto dg = createTestDataGroup("DG1");

    // Act
    std::string id = repository_->insert(dg, testVerificationId_);

    // Assert
    EXPECT_FALSE(id.empty());
    EXPECT_EQ(id.length(), 36);  // UUID length

    // Verify in database
    auto retrieved = repository_->findById(id);
    EXPECT_FALSE(retrieved.isNull());
    EXPECT_EQ(retrieved["dgNumber"].asInt(), 1);  // DG1 -> 1
    EXPECT_EQ(retrieved["hashAlgorithm"].asString(), "SHA-1");
    EXPECT_TRUE(retrieved["hashValid"].asBool());
}

TEST_F(DataGroupRepositoryTest, InsertWithBinaryData) {
    // Arrange
    auto dg = createTestDataGroup("DG2");
    std::vector<uint8_t> largeData(1024, 0xAB);  // 1KB of data
    dg.rawData = largeData;

    // Act
    std::string id = repository_->insert(dg, testVerificationId_);

    // Assert
    EXPECT_FALSE(id.empty());

    // Verify binary data size
    auto retrieved = repository_->findById(id);
    EXPECT_EQ(retrieved["dataSize"].asInt(), 1024);
}

TEST_F(DataGroupRepositoryTest, InsertWithoutBinaryData) {
    // Arrange
    auto dg = createTestDataGroup("DG3");
    dg.rawData = std::nullopt;  // No binary data

    // Act
    std::string id = repository_->insert(dg, testVerificationId_);

    // Assert
    EXPECT_FALSE(id.empty());

    // Verify no binary data
    auto retrieved = repository_->findById(id);
    EXPECT_EQ(retrieved["dataSize"].asInt(), 0);
}

// --- FIND BY VERIFICATION ID Tests ---

TEST_F(DataGroupRepositoryTest, FindByVerificationIdMultipleGroups) {
    // Arrange - Insert 3 data groups
    repository_->insert(createTestDataGroup("DG1"), testVerificationId_);
    repository_->insert(createTestDataGroup("DG2"), testVerificationId_);
    repository_->insert(createTestDataGroup("DG3"), testVerificationId_);

    // Act
    auto result = repository_->findByVerificationId(testVerificationId_);

    // Assert
    EXPECT_TRUE(result.isArray());
    EXPECT_EQ(result.size(), 3);

    // Verify ordering (by DG number)
    EXPECT_EQ(result[0]["dgNumber"].asInt(), 1);
    EXPECT_EQ(result[1]["dgNumber"].asInt(), 2);
    EXPECT_EQ(result[2]["dgNumber"].asInt(), 3);
}

TEST_F(DataGroupRepositoryTest, FindByVerificationIdNoGroups) {
    // Act
    auto result = repository_->findByVerificationId("00000000-0000-0000-0000-000000000000");

    // Assert
    EXPECT_TRUE(result.isArray());
    EXPECT_EQ(result.size(), 0);
}

// --- FIND BY ID Tests ---

TEST_F(DataGroupRepositoryTest, FindByIdExists) {
    // Arrange
    auto dg = createTestDataGroup("DG14");
    std::string id = repository_->insert(dg, testVerificationId_);

    // Act
    auto result = repository_->findById(id);

    // Assert
    EXPECT_FALSE(result.isNull());
    EXPECT_EQ(result["id"].asString(), id);
    EXPECT_EQ(result["dgNumber"].asInt(), 14);
}

TEST_F(DataGroupRepositoryTest, FindByIdNotExists) {
    // Act
    auto result = repository_->findById("00000000-0000-0000-0000-000000000000");

    // Assert
    EXPECT_TRUE(result.isNull());
}

// --- DELETE Tests ---

TEST_F(DataGroupRepositoryTest, DeleteByVerificationIdSuccess) {
    // Arrange - Insert 3 data groups
    repository_->insert(createTestDataGroup("DG1"), testVerificationId_);
    repository_->insert(createTestDataGroup("DG2"), testVerificationId_);
    repository_->insert(createTestDataGroup("DG3"), testVerificationId_);

    // Act
    int deletedCount = repository_->deleteByVerificationId(testVerificationId_);

    // Assert
    EXPECT_EQ(deletedCount, 3);

    // Verify deletion
    auto result = repository_->findByVerificationId(testVerificationId_);
    EXPECT_EQ(result.size(), 0);
}

TEST_F(DataGroupRepositoryTest, DeleteByVerificationIdNoGroups) {
    // Act
    int deletedCount = repository_->deleteByVerificationId("00000000-0000-0000-0000-000000000000");

    // Assert
    EXPECT_EQ(deletedCount, 0);
}

// --- HASH VALIDATION Tests ---

TEST_F(DataGroupRepositoryTest, InsertInvalidHash) {
    // Arrange
    auto dg = createTestDataGroup("DG15");
    dg.expectedHash = "0123456789abcdef0123456789abcdef01234567";
    dg.actualHash = "fedcba9876543210fedcba9876543210fedcba98";  // Different hash
    dg.hashValid = false;

    // Act
    std::string id = repository_->insert(dg, testVerificationId_);

    // Assert
    auto retrieved = repository_->findById(id);
    EXPECT_FALSE(retrieved["hashValid"].asBool());
    EXPECT_NE(retrieved["expectedHash"].asString(), retrieved["actualHash"].asString());
}

// --- DG NUMBER PARSING Tests ---

TEST_F(DataGroupRepositoryTest, DgNumberParsing) {
    // Test all DG numbers 1-16
    for (int i = 1; i <= 16; i++) {
        auto dg = createTestDataGroup("DG" + std::to_string(i));
        std::string id = repository_->insert(dg, testVerificationId_);

        auto retrieved = repository_->findById(id);
        EXPECT_EQ(retrieved["dgNumber"].asInt(), i);
    }
}

} // namespace
