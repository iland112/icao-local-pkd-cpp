/**
 * @file test_ldif_structure_service.cpp
 * @brief Unit tests for LdifStructureService
 *
 * Tests the LDIF structure visualization service layer:
 * - Constructor null-guard validation
 * - validateMaxEntries() clamping behavior (range [1, 10000])
 * - getLdifStructure() happy path: success response format
 * - getLdifStructure() error path: repository exception → error response
 * - createSuccessResponse() JSON shape
 * - createErrorResponse() JSON shape
 * - Idempotency of repeated calls
 *
 * Framework: Google Test (GTest)
 */

#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <map>
#include <vector>

// ── Headers under test ────────────────────────────────────────────────────────
#include "../src/services/ldif_structure_service.h"
#include "../src/repositories/ldif_structure_repository.h"
#include "../src/common/ldif_parser.h"

// ─────────────────────────────────────────────────────────────────────────────
// Mock LdifStructureRepository
//
// Does NOT touch the file system or the upload repository.
// Callers configure it before each test with either:
//   mock.setReturnData(data)   — next call returns data
//   mock.setThrowMessage(msg)  — next call throws std::runtime_error(msg)
// ─────────────────────────────────────────────────────────────────────────────

class MockLdifStructureRepository : public repositories::LdifStructureRepository {
public:
    // Use protected default constructor — no UploadRepository dependency needed
    // since getLdifStructure() is overridden and never touches uploadRepository_.
    MockLdifStructureRepository()
        : repositories::LdifStructureRepository() {}

    // ── Configuration ─────────────────────────────────────────────────────────

    void setReturnData(const repositories::LdifStructureData& data) {
        returnData_     = data;
        shouldThrow_    = false;
        throwMessage_   = "";
    }

    void setThrowMessage(const std::string& msg) {
        shouldThrow_  = true;
        throwMessage_ = msg;
    }

    // ── Observation ───────────────────────────────────────────────────────────

    int callCount() const { return callCount_; }
    std::string lastUploadId() const { return lastUploadId_; }
    int lastMaxEntries() const { return lastMaxEntries_; }

    // ── Override ──────────────────────────────────────────────────────────────

    repositories::LdifStructureData getLdifStructure(
        const std::string& uploadId,
        int maxEntries = 100
    ) override {
        ++callCount_;
        lastUploadId_   = uploadId;
        lastMaxEntries_ = maxEntries;

        if (shouldThrow_) {
            throw std::runtime_error(throwMessage_);
        }
        return returnData_;
    }

private:
    repositories::LdifStructureData returnData_{};
    bool        shouldThrow_    = false;
    std::string throwMessage_;
    int         callCount_      = 0;
    std::string lastUploadId_;
    int         lastMaxEntries_ = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Helper — build a minimal LdifStructureData with realistic fields
// ─────────────────────────────────────────────────────────────────────────────

static repositories::LdifStructureData makeSampleData(
    int totalEntries = 5017,
    int displayedEntries = 100,
    bool truncated = true
) {
    repositories::LdifStructureData d;
    d.totalEntries    = totalEntries;
    d.displayedEntries = displayedEntries;
    d.totalAttributes  = displayedEntries * 3;
    d.truncated        = truncated;
    d.objectClassCounts["pkdCertificate"] = displayedEntries - 2;
    d.objectClassCounts["pkdMasterList"]  = 2;

    // Build one representative entry
    icao::ldif::LdifEntryStructure entry;
    entry.dn          = "cn=CSCA-FRANCE,o=csca,c=FR,dc=data,dc=download,"
                        "dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";
    entry.objectClass = "pkdCertificate";
    entry.lineNumber  = 15;

    icao::ldif::LdifAttribute attrCn;
    attrCn.name       = "cn";
    attrCn.value      = "CSCA-FRANCE";
    attrCn.isBinary   = false;
    attrCn.binarySize = 0;

    icao::ldif::LdifAttribute attrCert;
    attrCert.name       = "userCertificate;binary";
    attrCert.value      = "[Binary Certificate: 1234 bytes]";
    attrCert.isBinary   = true;
    attrCert.binarySize = 1234;

    entry.attributes.push_back(attrCn);
    entry.attributes.push_back(attrCert);
    d.entries.push_back(entry);

    return d;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture
// ─────────────────────────────────────────────────────────────────────────────

class LdifStructureServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        mockRepo_ = std::make_unique<MockLdifStructureRepository>();
        service_  = std::make_unique<services::LdifStructureService>(mockRepo_.get());
    }

    std::unique_ptr<MockLdifStructureRepository> mockRepo_;
    std::unique_ptr<services::LdifStructureService> service_;
};

// ═════════════════════════════════════════════════════════════════════════════
// 1. Constructor validation
// ═════════════════════════════════════════════════════════════════════════════

TEST(LdifStructureServiceConstructorTest, NullRepository_ThrowsInvalidArgument) {
    EXPECT_THROW(
        services::LdifStructureService(nullptr),
        std::invalid_argument
    );
}

TEST(LdifStructureServiceConstructorTest, ValidRepository_DoesNotThrow) {
    MockLdifStructureRepository repo;
    EXPECT_NO_THROW({ services::LdifStructureService svc(&repo); });
}

TEST(LdifStructureServiceConstructorTest, NullRepository_ExceptionMessage) {
    try {
        services::LdifStructureService(nullptr);
        FAIL() << "Expected std::invalid_argument";
    } catch (const std::invalid_argument& ex) {
        std::string msg(ex.what());
        EXPECT_FALSE(msg.empty());
        // Message should mention the repository or null
        EXPECT_TRUE(
            msg.find("null") != std::string::npos ||
            msg.find("Null") != std::string::npos ||
            msg.find("NULL") != std::string::npos ||
            msg.find("Repository") != std::string::npos ||
            msg.find("repository") != std::string::npos
        ) << "Unexpected message: " << msg;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. validateMaxEntries() — clamping behaviour
//    (Tested indirectly through getLdifStructure; the validated value reaches
//     the repository via lastMaxEntries().)
// ═════════════════════════════════════════════════════════════════════════════

class LdifStructureServiceMaxEntriesTest : public ::testing::Test {
protected:
    void SetUp() override {
        mockRepo_ = std::make_unique<MockLdifStructureRepository>();
        mockRepo_->setReturnData(makeSampleData());
        service_  = std::make_unique<services::LdifStructureService>(mockRepo_.get());
    }

    std::unique_ptr<MockLdifStructureRepository> mockRepo_;
    std::unique_ptr<services::LdifStructureService> service_;
};

TEST_F(LdifStructureServiceMaxEntriesTest, Zero_ClampedToOne) {
    service_->getLdifStructure("upload-1", 0);
    EXPECT_EQ(mockRepo_->lastMaxEntries(), 1);
}

TEST_F(LdifStructureServiceMaxEntriesTest, NegativeValue_ClampedToOne) {
    service_->getLdifStructure("upload-1", -1);
    EXPECT_EQ(mockRepo_->lastMaxEntries(), 1);
}

TEST_F(LdifStructureServiceMaxEntriesTest, VeryNegative_ClampedToOne) {
    service_->getLdifStructure("upload-1", -99999);
    EXPECT_EQ(mockRepo_->lastMaxEntries(), 1);
}

TEST_F(LdifStructureServiceMaxEntriesTest, BoundaryMin_OnePassesThrough) {
    service_->getLdifStructure("upload-1", 1);
    EXPECT_EQ(mockRepo_->lastMaxEntries(), 1);
}

TEST_F(LdifStructureServiceMaxEntriesTest, BoundaryMax_TenThousandPassesThrough) {
    service_->getLdifStructure("upload-1", 10000);
    EXPECT_EQ(mockRepo_->lastMaxEntries(), 10000);
}

TEST_F(LdifStructureServiceMaxEntriesTest, AboveMax_ClampedToTenThousand) {
    service_->getLdifStructure("upload-1", 10001);
    EXPECT_EQ(mockRepo_->lastMaxEntries(), 10000);
}

TEST_F(LdifStructureServiceMaxEntriesTest, WayAboveMax_ClampedToTenThousand) {
    service_->getLdifStructure("upload-1", 1000000);
    EXPECT_EQ(mockRepo_->lastMaxEntries(), 10000);
}

TEST_F(LdifStructureServiceMaxEntriesTest, MidRange_PassesThrough) {
    service_->getLdifStructure("upload-1", 500);
    EXPECT_EQ(mockRepo_->lastMaxEntries(), 500);
}

TEST_F(LdifStructureServiceMaxEntriesTest, DefaultParameter_PassesThroughAsDefault) {
    // When called without maxEntries the service default (100) is used and
    // must arrive at the repository unchanged.
    service_->getLdifStructure("upload-1");
    EXPECT_EQ(mockRepo_->lastMaxEntries(), 100);
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. getLdifStructure() — success path
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LdifStructureServiceTest, GetLdifStructure_Success_ReturnsSuccessTrue) {
    mockRepo_->setReturnData(makeSampleData());
    Json::Value result = service_->getLdifStructure("upload-abc", 100);
    ASSERT_TRUE(result.isMember("success"));
    EXPECT_TRUE(result["success"].asBool());
}

TEST_F(LdifStructureServiceTest, GetLdifStructure_Success_HasDataField) {
    mockRepo_->setReturnData(makeSampleData());
    Json::Value result = service_->getLdifStructure("upload-abc", 100);
    EXPECT_TRUE(result.isMember("data"));
    EXPECT_FALSE(result.isMember("error"));
}

TEST_F(LdifStructureServiceTest, GetLdifStructure_Success_DataContainsTotalEntries) {
    repositories::LdifStructureData data = makeSampleData(5017, 100, true);
    mockRepo_->setReturnData(data);

    Json::Value result = service_->getLdifStructure("upload-abc", 100);
    ASSERT_TRUE(result["data"].isMember("totalEntries"));
    EXPECT_EQ(result["data"]["totalEntries"].asInt(), 5017);
}

TEST_F(LdifStructureServiceTest, GetLdifStructure_Success_DataContainsDisplayedEntries) {
    repositories::LdifStructureData data = makeSampleData(5017, 100, true);
    mockRepo_->setReturnData(data);

    Json::Value result = service_->getLdifStructure("upload-abc", 100);
    ASSERT_TRUE(result["data"].isMember("displayedEntries"));
    EXPECT_EQ(result["data"]["displayedEntries"].asInt(), 100);
}

TEST_F(LdifStructureServiceTest, GetLdifStructure_Success_DataContainsTruncated) {
    repositories::LdifStructureData data = makeSampleData(5017, 100, true);
    mockRepo_->setReturnData(data);

    Json::Value result = service_->getLdifStructure("upload-abc", 100);
    ASSERT_TRUE(result["data"].isMember("truncated"));
    EXPECT_TRUE(result["data"]["truncated"].asBool());
}

TEST_F(LdifStructureServiceTest, GetLdifStructure_Success_TruncatedFalseWhenNotTruncated) {
    repositories::LdifStructureData data = makeSampleData(5, 5, false);
    mockRepo_->setReturnData(data);

    Json::Value result = service_->getLdifStructure("upload-abc", 100);
    EXPECT_FALSE(result["data"]["truncated"].asBool());
}

TEST_F(LdifStructureServiceTest, GetLdifStructure_Success_DataContainsObjectClassCounts) {
    repositories::LdifStructureData data = makeSampleData();
    mockRepo_->setReturnData(data);

    Json::Value result = service_->getLdifStructure("upload-abc", 100);
    ASSERT_TRUE(result["data"].isMember("objectClassCounts"));
    EXPECT_TRUE(result["data"]["objectClassCounts"].isObject());
}

TEST_F(LdifStructureServiceTest, GetLdifStructure_Success_RepositoryCalledOnce) {
    mockRepo_->setReturnData(makeSampleData());
    service_->getLdifStructure("upload-abc", 100);
    EXPECT_EQ(mockRepo_->callCount(), 1);
}

TEST_F(LdifStructureServiceTest, GetLdifStructure_Success_UploadIdForwardedToRepository) {
    mockRepo_->setReturnData(makeSampleData());
    service_->getLdifStructure("my-upload-id-123", 100);
    EXPECT_EQ(mockRepo_->lastUploadId(), "my-upload-id-123");
}

TEST_F(LdifStructureServiceTest, GetLdifStructure_Success_MaxEntriesForwardedToRepository) {
    mockRepo_->setReturnData(makeSampleData());
    service_->getLdifStructure("upload-abc", 250);
    EXPECT_EQ(mockRepo_->lastMaxEntries(), 250);
}

TEST_F(LdifStructureServiceTest, GetLdifStructure_Success_EntriesArrayPresent) {
    repositories::LdifStructureData data = makeSampleData();
    mockRepo_->setReturnData(data);

    Json::Value result = service_->getLdifStructure("upload-abc", 100);
    ASSERT_TRUE(result["data"].isMember("entries"));
    EXPECT_TRUE(result["data"]["entries"].isArray());
}

TEST_F(LdifStructureServiceTest, GetLdifStructure_Success_EmptyEntries) {
    repositories::LdifStructureData data;
    data.totalEntries     = 0;
    data.displayedEntries = 0;
    data.totalAttributes  = 0;
    data.truncated        = false;
    mockRepo_->setReturnData(data);

    Json::Value result = service_->getLdifStructure("empty-upload", 100);
    EXPECT_TRUE(result["success"].asBool());
    EXPECT_EQ(result["data"]["totalEntries"].asInt(), 0);
    EXPECT_EQ(result["data"]["entries"].size(), 0u);
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. getLdifStructure() — error path
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LdifStructureServiceTest, GetLdifStructure_RepositoryThrows_ReturnsSuccessFalse) {
    mockRepo_->setThrowMessage("Upload not found");
    Json::Value result = service_->getLdifStructure("bad-id", 100);
    ASSERT_TRUE(result.isMember("success"));
    EXPECT_FALSE(result["success"].asBool());
}

TEST_F(LdifStructureServiceTest, GetLdifStructure_RepositoryThrows_HasErrorField) {
    mockRepo_->setThrowMessage("Upload not found");
    Json::Value result = service_->getLdifStructure("bad-id", 100);
    EXPECT_TRUE(result.isMember("error"));
    EXPECT_FALSE(result.isMember("data"));
}

TEST_F(LdifStructureServiceTest, GetLdifStructure_RepositoryThrows_ErrorMessagePreserved) {
    mockRepo_->setThrowMessage("Upload not found: bad-id");
    Json::Value result = service_->getLdifStructure("bad-id", 100);
    EXPECT_EQ(result["error"].asString(), "Upload not found: bad-id");
}

TEST_F(LdifStructureServiceTest, GetLdifStructure_NotLdifError_ErrorMessagePreserved) {
    mockRepo_->setThrowMessage("Upload is not LDIF format");
    Json::Value result = service_->getLdifStructure("csv-upload", 100);
    EXPECT_EQ(result["error"].asString(), "Upload is not LDIF format");
}

TEST_F(LdifStructureServiceTest, GetLdifStructure_FileNotFoundError_ErrorMessagePreserved) {
    mockRepo_->setThrowMessage("LDIF file not found: /uploads/abc.ldif");
    Json::Value result = service_->getLdifStructure("abc", 100);
    std::string errorMsg = result["error"].asString();
    EXPECT_FALSE(errorMsg.empty());
    EXPECT_NE(errorMsg.find("not found"), std::string::npos);
}

TEST_F(LdifStructureServiceTest, GetLdifStructure_KoreanErrorMessage_PreservedUtf8) {
    // Korean characters must pass through unchanged (UTF-8 safety)
    const std::string koreanMsg = "업로드를 찾을 수 없습니다";
    mockRepo_->setThrowMessage(koreanMsg);
    Json::Value result = service_->getLdifStructure("upload-kr", 100);
    EXPECT_EQ(result["error"].asString(), koreanMsg);
}

TEST_F(LdifStructureServiceTest, GetLdifStructure_RepositoryThrows_DoesNotRethrow) {
    mockRepo_->setThrowMessage("Something went wrong");
    // getLdifStructure must not propagate — it catches and wraps
    EXPECT_NO_THROW(service_->getLdifStructure("any-id", 100));
}

TEST_F(LdifStructureServiceTest, GetLdifStructure_RepositoryThrows_RepositoryCalledOnce) {
    mockRepo_->setThrowMessage("error");
    service_->getLdifStructure("x", 100);
    EXPECT_EQ(mockRepo_->callCount(), 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. Response JSON shape — direct verification
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LdifStructureServiceTest, SuccessResponse_DoesNotContainErrorField) {
    mockRepo_->setReturnData(makeSampleData());
    Json::Value result = service_->getLdifStructure("upload-1", 100);
    EXPECT_FALSE(result.isMember("error"));
}

TEST_F(LdifStructureServiceTest, ErrorResponse_DoesNotContainDataField) {
    mockRepo_->setThrowMessage("fail");
    Json::Value result = service_->getLdifStructure("upload-1", 100);
    EXPECT_FALSE(result.isMember("data"));
}

TEST_F(LdifStructureServiceTest, SuccessResponse_SuccessFieldIsBoolean) {
    mockRepo_->setReturnData(makeSampleData());
    Json::Value result = service_->getLdifStructure("upload-1", 100);
    EXPECT_TRUE(result["success"].isBool());
}

TEST_F(LdifStructureServiceTest, ErrorResponse_SuccessFieldIsBoolean) {
    mockRepo_->setThrowMessage("fail");
    Json::Value result = service_->getLdifStructure("upload-1", 100);
    EXPECT_TRUE(result["success"].isBool());
}

TEST_F(LdifStructureServiceTest, ErrorResponse_ErrorFieldIsString) {
    mockRepo_->setThrowMessage("some error");
    Json::Value result = service_->getLdifStructure("upload-1", 100);
    EXPECT_TRUE(result["error"].isString());
}

TEST_F(LdifStructureServiceTest, SuccessResponse_EmptyUploadId_StillCallsRepository) {
    mockRepo_->setReturnData(makeSampleData());
    Json::Value result = service_->getLdifStructure("", 100);
    EXPECT_EQ(mockRepo_->callCount(), 1);
    EXPECT_EQ(mockRepo_->lastUploadId(), "");
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. Idempotency
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LdifStructureServiceTest, Idempotency_TwoCallsSameInput_SameSuccessResult) {
    mockRepo_->setReturnData(makeSampleData(200, 50, false));

    Json::Value r1 = service_->getLdifStructure("upload-idem", 50);
    Json::Value r2 = service_->getLdifStructure("upload-idem", 50);

    EXPECT_EQ(r1["success"].asBool(), r2["success"].asBool());
    EXPECT_EQ(r1["data"]["totalEntries"].asInt(),
              r2["data"]["totalEntries"].asInt());
    EXPECT_EQ(r1["data"]["displayedEntries"].asInt(),
              r2["data"]["displayedEntries"].asInt());
    EXPECT_EQ(mockRepo_->callCount(), 2);
}

TEST_F(LdifStructureServiceTest, Idempotency_TwoCallsSameInput_SameErrorResult) {
    mockRepo_->setThrowMessage("not found");

    Json::Value r1 = service_->getLdifStructure("upload-err", 100);
    Json::Value r2 = service_->getLdifStructure("upload-err", 100);

    EXPECT_EQ(r1["success"].asBool(), r2["success"].asBool());
    EXPECT_EQ(r1["error"].asString(), r2["error"].asString());
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. Edge cases for uploadId
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LdifStructureServiceTest, UuidStyleUploadId_ForwardedCorrectly) {
    const std::string uuid = "550e8400-e29b-41d4-a716-446655440000";
    mockRepo_->setReturnData(makeSampleData());
    service_->getLdifStructure(uuid, 100);
    EXPECT_EQ(mockRepo_->lastUploadId(), uuid);
}

TEST_F(LdifStructureServiceTest, LongUploadId_ForwardedCorrectly) {
    const std::string longId(256, 'a');
    mockRepo_->setReturnData(makeSampleData());
    service_->getLdifStructure(longId, 100);
    EXPECT_EQ(mockRepo_->lastUploadId(), longId);
}

// ═════════════════════════════════════════════════════════════════════════════
// 8. Boundary maxEntries — clamped value reaches repository
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LdifStructureServiceTest, BoundaryMaxEntries_ExactlyOne_ForwardedAsOne) {
    mockRepo_->setReturnData(makeSampleData());
    service_->getLdifStructure("x", 1);
    EXPECT_EQ(mockRepo_->lastMaxEntries(), 1);
}

TEST_F(LdifStructureServiceTest, BoundaryMaxEntries_ExactlyTenThousand_ForwardedAsTenThousand) {
    mockRepo_->setReturnData(makeSampleData());
    service_->getLdifStructure("x", 10000);
    EXPECT_EQ(mockRepo_->lastMaxEntries(), 10000);
}

TEST_F(LdifStructureServiceTest, BoundaryMaxEntries_NegativeLarge_ClampedToOne) {
    mockRepo_->setReturnData(makeSampleData());
    service_->getLdifStructure("x", std::numeric_limits<int>::min());
    EXPECT_EQ(mockRepo_->lastMaxEntries(), 1);
}

TEST_F(LdifStructureServiceTest, BoundaryMaxEntries_PositiveLarge_ClampedToTenThousand) {
    mockRepo_->setReturnData(makeSampleData());
    service_->getLdifStructure("x", std::numeric_limits<int>::max());
    EXPECT_EQ(mockRepo_->lastMaxEntries(), 10000);
}

// ═════════════════════════════════════════════════════════════════════════════
// 9. Data integrity — entry attributes pass through toJson()
// ═════════════════════════════════════════════════════════════════════════════

TEST_F(LdifStructureServiceTest, DataIntegrity_EntryDnPreservedInJson) {
    repositories::LdifStructureData data = makeSampleData();
    const std::string expectedDn = data.entries[0].dn;
    mockRepo_->setReturnData(data);

    Json::Value result = service_->getLdifStructure("upload-1", 100);
    ASSERT_TRUE(result["data"]["entries"].isArray());
    ASSERT_GE(result["data"]["entries"].size(), 1u);
    EXPECT_EQ(result["data"]["entries"][0]["dn"].asString(), expectedDn);
}

TEST_F(LdifStructureServiceTest, DataIntegrity_ObjectClassCountsPreserved) {
    repositories::LdifStructureData data = makeSampleData();
    mockRepo_->setReturnData(data);

    Json::Value result = service_->getLdifStructure("upload-1", 100);
    Json::Value counts = result["data"]["objectClassCounts"];
    EXPECT_TRUE(counts.isMember("pkdCertificate"));
    EXPECT_TRUE(counts.isMember("pkdMasterList"));
}

TEST_F(LdifStructureServiceTest, DataIntegrity_TotalAttributesPreserved) {
    repositories::LdifStructureData data = makeSampleData(100, 10, false);
    data.totalAttributes = 30;
    mockRepo_->setReturnData(data);

    Json::Value result = service_->getLdifStructure("upload-1", 100);
    EXPECT_EQ(result["data"]["totalAttributes"].asInt(), 30);
}

// ═════════════════════════════════════════════════════════════════════════════
// 10. Multiple service instances are independent
// ═════════════════════════════════════════════════════════════════════════════

TEST(LdifStructureServiceIndependenceTest, TwoInstancesDifferentRepos_Independent) {
    MockLdifStructureRepository repoA, repoB;
    repoA.setReturnData(makeSampleData(100, 10, false));
    repoB.setThrowMessage("repo B always fails");

    services::LdifStructureService svcA(&repoA);
    services::LdifStructureService svcB(&repoB);

    Json::Value ra = svcA.getLdifStructure("upload-A", 10);
    Json::Value rb = svcB.getLdifStructure("upload-B", 10);

    EXPECT_TRUE(ra["success"].asBool());
    EXPECT_FALSE(rb["success"].asBool());

    EXPECT_EQ(repoA.callCount(), 1);
    EXPECT_EQ(repoB.callCount(), 1);
}
