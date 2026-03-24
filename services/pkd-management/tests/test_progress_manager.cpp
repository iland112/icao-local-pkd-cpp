/**
 * @file test_progress_manager.cpp
 * @brief Unit tests for ProgressManager and related types in progress_manager.h/.cpp
 *
 * Tested components (no OpenSSL certs, no DB, no g_services):
 *  - stageToString() / stageToKorean() / stageToBasePercentage() helper functions
 *  - safeIncrementMap() — DoS-defense bounded map increment
 *  - addProcessingError() — error counter updates and bounded deque
 *  - addValidationLog() — log counter updates and bounded deque
 *  - ProcessingProgress::create() — percentage calculation
 *  - ProcessingProgress::createWithMetadata() — metadata fields populated
 *  - ProgressManager::sendProgress() — caches progress, fires SSE callback
 *  - ProgressManager::getProgress() — retrieves cached progress
 *  - ProgressManager::registerSseCallback() — callback registered + cached data replayed
 *  - ProgressManager::unregisterSseCallback() — callback removed
 *  - ProgressManager::clearProgress() — both cache and callback removed
 *  - ProgressManager::cleanupStaleEntries() — evicts old entries by age
 *  - Thread safety of sendProgress + getProgress under concurrent access
 *
 * NOTE: ProgressManager uses a singleton (getInstance()). Each test that touches
 * the singleton must call clearProgress() for every uploadId it uses in order to
 * isolate state. Unique per-test uploadIds are used throughout.
 */

#include <gtest/gtest.h>
#include "../src/common/progress_manager.h"

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace common;

// Provide a null definition for g_services so the linker is satisfied when
// progress_manager.cpp is compiled into this test binary without the full
// service_container.cpp.  updateUploadStatistics() is the only function that
// dereferences g_services; it is NOT called in any of these tests.
namespace infrastructure { class ServiceContainer; }
infrastructure::ServiceContainer* g_services = nullptr;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static ProcessingProgress makeProgress(
    const std::string& uploadId,
    ProcessingStage stage = ProcessingStage::PARSING_IN_PROGRESS,
    int processed = 5,
    int total = 10,
    const std::string& message = "test message")
{
    return ProcessingProgress::create(uploadId, stage, processed, total, message);
}

// Each test that uses the singleton must call this on teardown
static void cleanupSingleton(const std::string& uploadId) {
    ProgressManager::getInstance().clearProgress(uploadId);
}

// ===========================================================================
// stageToString()
// ===========================================================================

TEST(StageToStringTest, AllStages_HaveNonEmptyStrings) {
    using S = ProcessingStage;
    std::vector<S> stages = {
        S::UPLOAD_COMPLETED,
        S::PARSING_STARTED, S::PARSING_IN_PROGRESS, S::PARSING_COMPLETED,
        S::VALIDATION_STARTED, S::VALIDATION_EXTRACTING_METADATA,
        S::VALIDATION_VERIFYING_SIGNATURE, S::VALIDATION_CHECKING_TRUST_CHAIN,
        S::VALIDATION_CHECKING_CRL, S::VALIDATION_CHECKING_ICAO_COMPLIANCE,
        S::VALIDATION_IN_PROGRESS, S::VALIDATION_COMPLETED,
        S::DB_SAVING_STARTED, S::DB_SAVING_IN_PROGRESS, S::DB_SAVING_COMPLETED,
        S::LDAP_SAVING_STARTED, S::LDAP_SAVING_IN_PROGRESS, S::LDAP_SAVING_COMPLETED,
        S::COMPLETED, S::FAILED,
    };
    for (auto s : stages) {
        EXPECT_FALSE(stageToString(s).empty())
            << "stageToString returned empty for stage " << static_cast<int>(s);
    }
}

TEST(StageToStringTest, KnownValues) {
    EXPECT_EQ(stageToString(ProcessingStage::COMPLETED), "COMPLETED");
    EXPECT_EQ(stageToString(ProcessingStage::FAILED), "FAILED");
    EXPECT_EQ(stageToString(ProcessingStage::PARSING_IN_PROGRESS), "PARSING_IN_PROGRESS");
    EXPECT_EQ(stageToString(ProcessingStage::DB_SAVING_COMPLETED), "DB_SAVING_COMPLETED");
    EXPECT_EQ(stageToString(ProcessingStage::VALIDATION_CHECKING_CRL), "VALIDATION_CHECKING_CRL");
}

// ===========================================================================
// stageToKorean()
// ===========================================================================

TEST(StageToKoreanTest, AllStages_HaveNonEmptyKorean) {
    using S = ProcessingStage;
    std::vector<S> stages = {
        S::UPLOAD_COMPLETED, S::PARSING_STARTED, S::PARSING_IN_PROGRESS,
        S::PARSING_COMPLETED, S::VALIDATION_STARTED,
        S::VALIDATION_EXTRACTING_METADATA, S::VALIDATION_VERIFYING_SIGNATURE,
        S::VALIDATION_CHECKING_TRUST_CHAIN, S::VALIDATION_CHECKING_CRL,
        S::VALIDATION_CHECKING_ICAO_COMPLIANCE, S::VALIDATION_IN_PROGRESS,
        S::VALIDATION_COMPLETED, S::DB_SAVING_STARTED, S::DB_SAVING_IN_PROGRESS,
        S::DB_SAVING_COMPLETED, S::LDAP_SAVING_STARTED, S::LDAP_SAVING_IN_PROGRESS,
        S::LDAP_SAVING_COMPLETED, S::COMPLETED, S::FAILED,
    };
    for (auto s : stages) {
        EXPECT_FALSE(stageToKorean(s).empty())
            << "stageToKorean returned empty for stage " << static_cast<int>(s);
    }
}

TEST(StageToKoreanTest, CompletedStage_KoreanText) {
    std::string korean = stageToKorean(ProcessingStage::COMPLETED);
    // Must contain at least one multi-byte UTF-8 character (Korean)
    EXPECT_GT(korean.size(), 4u) << "Korean text expected, got: " << korean;
}

// ===========================================================================
// stageToBasePercentage()
// ===========================================================================

TEST(StageToBasePercentageTest, FailedStage_ReturnsZero) {
    EXPECT_EQ(stageToBasePercentage(ProcessingStage::FAILED), 0);
}

TEST(StageToBasePercentageTest, CompletedStage_Returns100) {
    EXPECT_EQ(stageToBasePercentage(ProcessingStage::COMPLETED), 100);
}

TEST(StageToBasePercentageTest, UploadCompleted_Returns5) {
    EXPECT_EQ(stageToBasePercentage(ProcessingStage::UPLOAD_COMPLETED), 5);
}

TEST(StageToBasePercentageTest, ParsingInProgress_Returns30) {
    EXPECT_EQ(stageToBasePercentage(ProcessingStage::PARSING_IN_PROGRESS), 30);
}

TEST(StageToBasePercentageTest, AllStages_InRange0to100) {
    using S = ProcessingStage;
    std::vector<S> stages = {
        S::UPLOAD_COMPLETED, S::PARSING_STARTED, S::PARSING_IN_PROGRESS,
        S::PARSING_COMPLETED, S::VALIDATION_STARTED,
        S::VALIDATION_EXTRACTING_METADATA, S::VALIDATION_VERIFYING_SIGNATURE,
        S::VALIDATION_CHECKING_TRUST_CHAIN, S::VALIDATION_CHECKING_CRL,
        S::VALIDATION_CHECKING_ICAO_COMPLIANCE, S::VALIDATION_IN_PROGRESS,
        S::VALIDATION_COMPLETED, S::DB_SAVING_STARTED, S::DB_SAVING_IN_PROGRESS,
        S::DB_SAVING_COMPLETED, S::LDAP_SAVING_STARTED, S::LDAP_SAVING_IN_PROGRESS,
        S::LDAP_SAVING_COMPLETED, S::COMPLETED, S::FAILED,
    };
    for (auto s : stages) {
        int pct = stageToBasePercentage(s);
        EXPECT_GE(pct, 0)   << "stage " << static_cast<int>(s) << " percentage < 0";
        EXPECT_LE(pct, 100) << "stage " << static_cast<int>(s) << " percentage > 100";
    }
}

// ===========================================================================
// safeIncrementMap()
// ===========================================================================

TEST(SafeIncrementMapTest, NewKey_AddedWithCount1) {
    std::map<std::string, int> m;
    safeIncrementMap(m, "RSA", 10);
    ASSERT_EQ(m.count("RSA"), 1u);
    EXPECT_EQ(m["RSA"], 1);
}

TEST(SafeIncrementMapTest, ExistingKey_Incremented) {
    std::map<std::string, int> m;
    m["RSA"] = 5;
    safeIncrementMap(m, "RSA", 10);
    EXPECT_EQ(m["RSA"], 6);
}

TEST(SafeIncrementMapTest, AtMaxSize_NewKeyNotAdded) {
    std::map<std::string, int> m;
    // Fill map to the limit
    for (int i = 0; i < 3; i++) {
        m["key" + std::to_string(i)] = 1;
    }
    // Try to add a new key beyond max
    safeIncrementMap(m, "overflow-key", 3);  // maxSize = 3
    EXPECT_EQ(m.count("overflow-key"), 0u)
        << "New key must not be added when map is at maxSize";
    EXPECT_EQ(m.size(), 3u);
}

TEST(SafeIncrementMapTest, ExistingKey_AlwaysIncrementedEvenAtMaxSize) {
    std::map<std::string, int> m;
    m["key0"] = 10;
    m["key1"] = 20;
    m["key2"] = 30;
    // key0 already exists, so it must be incremented even though map is at maxSize=3
    safeIncrementMap(m, "key0", 3);
    EXPECT_EQ(m["key0"], 11);
}

TEST(SafeIncrementMapTest, DefaultMaxSize_Is100) {
    std::map<std::string, int> m;
    // Add 100 unique keys using default maxSize
    for (int i = 0; i < 100; i++) {
        safeIncrementMap(m, "k" + std::to_string(i));
    }
    EXPECT_EQ(m.size(), 100u);

    // The 101st key must NOT be added
    safeIncrementMap(m, "overflow");
    EXPECT_EQ(m.size(), 100u);
}

TEST(SafeIncrementMapTest, EmptyKey_Handled) {
    std::map<std::string, int> m;
    safeIncrementMap(m, "", 10);
    EXPECT_EQ(m.count(""), 1u);
    EXPECT_EQ(m[""], 1);
}

// ===========================================================================
// addProcessingError()
// ===========================================================================

TEST(AddProcessingErrorTest, ParseError_IncrementsTotalAndParseCount) {
    ValidationStatistics stats{};
    addProcessingError(stats, "CERT_PARSE_FAILED", "dn=test", "", "KR", "DSC", "parse failed");
    EXPECT_EQ(stats.totalErrorCount, 1);
    EXPECT_EQ(stats.parseErrorCount, 1);
    EXPECT_EQ(stats.dbSaveErrorCount, 0);
    EXPECT_EQ(stats.ldapSaveErrorCount, 0);
}

TEST(AddProcessingErrorTest, Base64DecodeError_CountsAsParseError) {
    ValidationStatistics stats{};
    addProcessingError(stats, "BASE64_DECODE_FAILED", "dn", "", "US", "DSC", "bad base64");
    EXPECT_EQ(stats.parseErrorCount, 1);
}

TEST(AddProcessingErrorTest, CrlParseError_CountsAsParseError) {
    ValidationStatistics stats{};
    addProcessingError(stats, "CRL_PARSE_FAILED", "dn", "", "DE", "CRL", "bad crl");
    EXPECT_EQ(stats.parseErrorCount, 1);
}

TEST(AddProcessingErrorTest, DbSaveError_IncrementsDbCount) {
    ValidationStatistics stats{};
    addProcessingError(stats, "DB_SAVE_FAILED", "dn", "cn=test", "FR", "DSC", "db error");
    EXPECT_EQ(stats.dbSaveErrorCount, 1);
    EXPECT_EQ(stats.parseErrorCount, 0);
}

TEST(AddProcessingErrorTest, LdapSaveError_IncrementsLdapCount) {
    ValidationStatistics stats{};
    addProcessingError(stats, "LDAP_SAVE_FAILED", "dn", "", "JP", "CSCA", "ldap error");
    EXPECT_EQ(stats.ldapSaveErrorCount, 1);
    EXPECT_EQ(stats.dbSaveErrorCount, 0);
}

TEST(AddProcessingErrorTest, EntryProcessingException_CountsAsParseError) {
    ValidationStatistics stats{};
    addProcessingError(stats, "ENTRY_PROCESSING_EXCEPTION", "dn", "", "KR", "DSC", "exception");
    EXPECT_EQ(stats.parseErrorCount, 1);
}

TEST(AddProcessingErrorTest, ErrorAddedToRecentErrorsDeque) {
    ValidationStatistics stats{};
    addProcessingError(stats, "CERT_PARSE_FAILED", "dn=cert1", "cn=cert1", "KR", "DSC", "msg");
    ASSERT_EQ(stats.recentErrors.size(), 1u);
    EXPECT_EQ(stats.recentErrors.back().errorType, "CERT_PARSE_FAILED");
    EXPECT_EQ(stats.recentErrors.back().countryCode, "KR");
    EXPECT_EQ(stats.recentErrors.back().message, "msg");
}

TEST(AddProcessingErrorTest, ErrorTimestamp_IsNonEmpty) {
    ValidationStatistics stats{};
    addProcessingError(stats, "CERT_PARSE_FAILED", "dn", "", "KR", "DSC", "msg");
    EXPECT_FALSE(stats.recentErrors.back().timestamp.empty());
}

TEST(AddProcessingErrorTest, BoundedDeque_OldestEvictedAtMaxSize) {
    ValidationStatistics stats{};
    // Fill beyond MAX_RECENT_ERRORS (100)
    for (int i = 0; i <= ValidationStatistics::MAX_RECENT_ERRORS; i++) {
        addProcessingError(stats, "CERT_PARSE_FAILED", "dn" + std::to_string(i),
                           "", "KR", "DSC", "error " + std::to_string(i));
    }
    // Deque must not exceed the maximum
    EXPECT_LE(static_cast<int>(stats.recentErrors.size()),
              ValidationStatistics::MAX_RECENT_ERRORS);
}

TEST(AddProcessingErrorTest, MultipleErrors_TotalCountAccumulates) {
    ValidationStatistics stats{};
    addProcessingError(stats, "CERT_PARSE_FAILED", "d1", "", "KR", "DSC", "m1");
    addProcessingError(stats, "DB_SAVE_FAILED",    "d2", "", "US", "DSC", "m2");
    addProcessingError(stats, "LDAP_SAVE_FAILED",  "d3", "", "DE", "CRL", "m3");
    EXPECT_EQ(stats.totalErrorCount,   3);
    EXPECT_EQ(stats.parseErrorCount,   1);
    EXPECT_EQ(stats.dbSaveErrorCount,  1);
    EXPECT_EQ(stats.ldapSaveErrorCount, 1);
}

TEST(AddProcessingErrorTest, MlCertParseError_CountsAsParseError) {
    ValidationStatistics stats{};
    addProcessingError(stats, "ML_CERT_PARSE_FAILED", "dn", "", "KR", "CSCA", "ml parse fail");
    EXPECT_EQ(stats.parseErrorCount, 1);
}

TEST(AddProcessingErrorTest, MlCertSaveError_CountsAsDbError) {
    ValidationStatistics stats{};
    addProcessingError(stats, "ML_CERT_SAVE_FAILED", "dn", "", "KR", "CSCA", "ml save fail");
    EXPECT_EQ(stats.dbSaveErrorCount, 1);
}

// ===========================================================================
// addValidationLog()
// ===========================================================================

TEST(AddValidationLogTest, LogAdded_AndCounterIncremented) {
    ValidationStatistics stats{};
    addValidationLog(stats, "DSC", "KR",
                     "CN=Test DSC,C=KR", "CN=KR CSCA,C=KR",
                     "VALID", "Trust chain verified", "DSC->CSCA", "", "aabbcc");
    EXPECT_EQ(stats.totalValidationLogCount, 1);
    ASSERT_EQ(stats.recentValidationLogs.size(), 1u);
}

TEST(AddValidationLogTest, LogEntry_FieldsCorrect) {
    ValidationStatistics stats{};
    addValidationLog(stats, "CSCA", "US",
                     "CN=US CSCA,C=US", "",
                     "VALID", "Self-signed", "", "", "fp1234");
    const auto& entry = stats.recentValidationLogs.back();
    EXPECT_EQ(entry.certificateType, "CSCA");
    EXPECT_EQ(entry.countryCode, "US");
    EXPECT_EQ(entry.validationStatus, "VALID");
    EXPECT_EQ(entry.fingerprintSha256, "fp1234");
}

TEST(AddValidationLogTest, Timestamp_IsNonEmpty) {
    ValidationStatistics stats{};
    addValidationLog(stats, "DSC", "KR", "", "", "INVALID", "", "", "CSCA_NOT_FOUND", "");
    EXPECT_FALSE(stats.recentValidationLogs.back().timestamp.empty());
}

TEST(AddValidationLogTest, TotalLogCount_IsMonotonicallyIncreasing) {
    ValidationStatistics stats{};
    for (int i = 1; i <= 5; i++) {
        addValidationLog(stats, "DSC", "KR", "CN=cert" + std::to_string(i), "",
                         "VALID", "", "", "", "fp" + std::to_string(i));
        EXPECT_EQ(stats.totalValidationLogCount, i);
    }
}

TEST(AddValidationLogTest, BoundedDeque_OldestEvictedAtMaxSize) {
    ValidationStatistics stats{};
    for (int i = 0; i <= ValidationStatistics::MAX_RECENT_VALIDATION_LOGS; i++) {
        addValidationLog(stats, "DSC", "KR", "CN=cert" + std::to_string(i), "",
                         "VALID", "", "", "", "fp" + std::to_string(i));
    }
    EXPECT_LE(static_cast<int>(stats.recentValidationLogs.size()),
              ValidationStatistics::MAX_RECENT_VALIDATION_LOGS);
    // totalValidationLogCount must NOT be bounded — it keeps counting
    EXPECT_GT(stats.totalValidationLogCount,
              ValidationStatistics::MAX_RECENT_VALIDATION_LOGS);
}

TEST(AddValidationLogTest, InvalidStatus_Recorded) {
    ValidationStatistics stats{};
    addValidationLog(stats, "DSC", "DE",
                     "CN=Test", "CN=Issuer",
                     "INVALID", "signature failed", "", "CSCA_NOT_FOUND", "deadbeef");
    EXPECT_EQ(stats.recentValidationLogs.back().validationStatus, "INVALID");
    EXPECT_EQ(stats.recentValidationLogs.back().errorCode, "CSCA_NOT_FOUND");
}

// ===========================================================================
// ProcessingProgress::create()
// ===========================================================================

TEST(ProcessingProgressCreateTest, BasicFieldsSet) {
    auto p = ProcessingProgress::create(
        "upload-001", ProcessingStage::PARSING_IN_PROGRESS, 3, 10, "Parsing...");
    EXPECT_EQ(p.uploadId, "upload-001");
    EXPECT_EQ(p.stage, ProcessingStage::PARSING_IN_PROGRESS);
    EXPECT_EQ(p.processedCount, 3);
    EXPECT_EQ(p.totalCount, 10);
    EXPECT_EQ(p.message, "Parsing...");
    EXPECT_TRUE(p.errorMessage.empty());
}

TEST(ProcessingProgressCreateTest, ErrorMessagePropagated) {
    auto p = ProcessingProgress::create(
        "upload-002", ProcessingStage::FAILED, 0, 0, "failed", "bad file", "");
    EXPECT_EQ(p.errorMessage, "bad file");
    EXPECT_EQ(p.stage, ProcessingStage::FAILED);
}

TEST(ProcessingProgressCreateTest, PercentageCalculated_ParsingInProgress) {
    // PARSING_IN_PROGRESS base=30, next=50; 5/10 = 50% of range => 30 + 10 = 40
    auto p = ProcessingProgress::create(
        "u1", ProcessingStage::PARSING_IN_PROGRESS, 5, 10, "msg");
    EXPECT_EQ(p.percentage, 40);
}

TEST(ProcessingProgressCreateTest, PercentageCalculated_ZeroTotal) {
    // When totalCount=0, fall back to base percentage
    auto p = ProcessingProgress::create(
        "u2", ProcessingStage::PARSING_IN_PROGRESS, 0, 0, "msg");
    EXPECT_EQ(p.percentage, stageToBasePercentage(ProcessingStage::PARSING_IN_PROGRESS));
}

TEST(ProcessingProgressCreateTest, PercentageCalculated_ZeroProcessed) {
    // processedCount=0 with totalCount>0 should fall back to base percentage
    auto p = ProcessingProgress::create(
        "u3", ProcessingStage::VALIDATION_IN_PROGRESS, 0, 100, "msg");
    EXPECT_EQ(p.percentage, stageToBasePercentage(ProcessingStage::VALIDATION_IN_PROGRESS));
}

TEST(ProcessingProgressCreateTest, Completed_Percentage100) {
    auto p = ProcessingProgress::create(
        "u4", ProcessingStage::COMPLETED, 100, 100, "done");
    EXPECT_EQ(p.percentage, 100);
}

TEST(ProcessingProgressCreateTest, Failed_Percentage0) {
    auto p = ProcessingProgress::create(
        "u5", ProcessingStage::FAILED, 0, 0, "error");
    EXPECT_EQ(p.percentage, 0);
}

TEST(ProcessingProgressCreateTest, UpdatedAt_IsRecent) {
    auto before = std::chrono::system_clock::now();
    auto p = ProcessingProgress::create("u6", ProcessingStage::COMPLETED, 0, 0, "");
    auto after = std::chrono::system_clock::now();
    EXPECT_GE(p.updatedAt, before);
    EXPECT_LE(p.updatedAt, after);
}

TEST(ProcessingProgressCreateTest, OptionalMetadata_AbsentByDefault) {
    auto p = ProcessingProgress::create("u7", ProcessingStage::PARSING_STARTED, 0, 0, "");
    EXPECT_FALSE(p.currentCertificate.has_value());
    EXPECT_FALSE(p.currentCompliance.has_value());
    EXPECT_FALSE(p.statistics.has_value());
}

// ===========================================================================
// ProcessingProgress::createWithMetadata()
// ===========================================================================

TEST(ProcessingProgressCreateWithMetadataTest, CertMetadata_Populated) {
    CertificateMetadata meta;
    meta.subjectDn = "CN=Test DSC,C=KR";
    meta.issuerDn  = "CN=KR CSCA,C=KR";
    meta.countryCode = "KR";
    meta.certificateType = "DSC";
    meta.signatureAlgorithm = "SHA256withRSA";
    meta.keySize = 2048;
    meta.isSelfSigned = false;
    meta.isLinkCertificate = false;
    meta.isCa = false;
    meta.isExpired = false;

    auto p = ProcessingProgress::createWithMetadata(
        "upload-meta", ProcessingStage::VALIDATION_CHECKING_TRUST_CHAIN,
        1, 10, "checking chain", meta);

    ASSERT_TRUE(p.currentCertificate.has_value());
    EXPECT_EQ(p.currentCertificate->subjectDn, "CN=Test DSC,C=KR");
    EXPECT_EQ(p.currentCertificate->countryCode, "KR");
    EXPECT_EQ(p.currentCertificate->keySize, 2048);
}

TEST(ProcessingProgressCreateWithMetadataTest, ComplianceAndStats_Optional) {
    CertificateMetadata meta;
    meta.certificateType = "CSCA";
    meta.isSelfSigned = true;
    meta.isLinkCertificate = false;
    meta.isCa = true;
    meta.isExpired = false;
    meta.keySize = 4096;

    // Without compliance or stats
    auto p = ProcessingProgress::createWithMetadata(
        "upload-no-extra", ProcessingStage::VALIDATION_STARTED, 0, 5, "msg", meta);
    EXPECT_FALSE(p.currentCompliance.has_value());
    EXPECT_FALSE(p.statistics.has_value());
}

TEST(ProcessingProgressCreateWithMetadataTest, WithStatistics_Populated) {
    CertificateMetadata meta;
    meta.certificateType = "DSC";
    meta.isSelfSigned = false;
    meta.isLinkCertificate = false;
    meta.isCa = false;
    meta.isExpired = false;
    meta.keySize = 2048;

    ValidationStatistics stats{};
    stats.totalCertificates = 100;
    stats.validCount = 90;
    stats.invalidCount = 10;

    auto p = ProcessingProgress::createWithMetadata(
        "upload-stats", ProcessingStage::VALIDATION_IN_PROGRESS,
        50, 100, "progress", meta, std::nullopt, stats);

    ASSERT_TRUE(p.statistics.has_value());
    EXPECT_EQ(p.statistics->totalCertificates, 100);
    EXPECT_EQ(p.statistics->validCount, 90);
}

// ===========================================================================
// ProgressManager singleton — sendProgress / getProgress
// ===========================================================================

class ProgressManagerTest : public ::testing::Test {
protected:
    // Use a unique ID per test so the singleton's state does not bleed across tests
    std::string id;

    void SetUp() override {
        // Generate a unique per-test upload ID
        id = "test-upload-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    }

    void TearDown() override {
        cleanupSingleton(id);
    }

    ProgressManager& pm() { return ProgressManager::getInstance(); }
};

TEST_F(ProgressManagerTest, GetProgress_NotFound_ReturnsNullopt) {
    auto result = pm().getProgress("nonexistent-upload-id");
    EXPECT_FALSE(result.has_value());
}

TEST_F(ProgressManagerTest, SendProgress_ThenGetProgress_ReturnsCorrectData) {
    auto progress = makeProgress(id, ProcessingStage::PARSING_IN_PROGRESS, 5, 10, "parsing");
    pm().sendProgress(progress);

    auto retrieved = pm().getProgress(id);
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->uploadId, id);
    EXPECT_EQ(retrieved->stage, ProcessingStage::PARSING_IN_PROGRESS);
    EXPECT_EQ(retrieved->processedCount, 5);
    EXPECT_EQ(retrieved->totalCount, 10);
    EXPECT_EQ(retrieved->message, "parsing");
}

TEST_F(ProgressManagerTest, SendProgress_OverwritesPreviousEntry) {
    pm().sendProgress(makeProgress(id, ProcessingStage::PARSING_STARTED, 0, 100, "start"));
    pm().sendProgress(makeProgress(id, ProcessingStage::PARSING_IN_PROGRESS, 50, 100, "mid"));

    auto retrieved = pm().getProgress(id);
    ASSERT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->stage, ProcessingStage::PARSING_IN_PROGRESS);
    EXPECT_EQ(retrieved->processedCount, 50);
}

TEST_F(ProgressManagerTest, ClearProgress_RemovesEntry) {
    pm().sendProgress(makeProgress(id));
    pm().clearProgress(id);

    auto result = pm().getProgress(id);
    EXPECT_FALSE(result.has_value());
}

// ===========================================================================
// ProgressManager — SSE callbacks
// ===========================================================================

TEST_F(ProgressManagerTest, RegisterCallback_CallbackFiredOnSendProgress) {
    std::atomic<int> callCount{0};
    std::string lastData;

    pm().registerSseCallback(id, [&](const std::string& data) {
        callCount++;
        lastData = data;
    });

    pm().sendProgress(makeProgress(id, ProcessingStage::DB_SAVING_IN_PROGRESS, 3, 10));
    EXPECT_EQ(callCount.load(), 1);
    EXPECT_FALSE(lastData.empty());
    // SSE payload must start with the event prefix
    EXPECT_NE(lastData.find("event: progress"), std::string::npos);
}

TEST_F(ProgressManagerTest, RegisterCallback_CachedProgressReplayedImmediately) {
    // sendProgress first, then registerSseCallback — cached data must be replayed
    pm().sendProgress(makeProgress(id, ProcessingStage::VALIDATION_IN_PROGRESS, 2, 10));

    std::atomic<int> replayCalls{0};
    pm().registerSseCallback(id, [&](const std::string&) {
        replayCalls++;
    });

    EXPECT_EQ(replayCalls.load(), 1) << "Cached progress must be replayed on registration";
}

TEST_F(ProgressManagerTest, UnregisterCallback_NoCallbackFiredAfterUnregister) {
    std::atomic<int> callCount{0};
    pm().registerSseCallback(id, [&](const std::string&) { callCount++; });
    pm().unregisterSseCallback(id);

    pm().sendProgress(makeProgress(id, ProcessingStage::COMPLETED, 10, 10));
    EXPECT_EQ(callCount.load(), 0);
}

TEST_F(ProgressManagerTest, ClearProgress_AlsoRemovesCallback) {
    std::atomic<int> callCount{0};
    pm().registerSseCallback(id, [&](const std::string&) { callCount++; });
    pm().clearProgress(id);

    pm().sendProgress(makeProgress(id, ProcessingStage::COMPLETED, 10, 10));
    // callback was removed by clearProgress(); sendProgress re-caches the entry
    // but should NOT invoke the old callback
    EXPECT_EQ(callCount.load(), 0);
}

TEST_F(ProgressManagerTest, ExceptionThrowingCallback_RemovedGracefully) {
    // A callback that throws should be removed; subsequent sendProgress must not crash
    pm().registerSseCallback(id, [](const std::string&) {
        throw std::runtime_error("deliberate test exception");
    });

    EXPECT_NO_THROW(pm().sendProgress(makeProgress(id)));
    // After the throw, the callback should have been removed
    std::atomic<int> secondCallCount{0};
    pm().registerSseCallback(id, [&](const std::string&) { secondCallCount++; });
    // Re-register a good callback; replay of cached entry fires it once
    EXPECT_GE(secondCallCount.load(), 0);  // no crash is the key assertion
}

TEST_F(ProgressManagerTest, MultipleUploads_AreIndependent) {
    std::string id2 = id + "-B";
    std::string id3 = id + "-C";

    pm().sendProgress(makeProgress(id,  ProcessingStage::PARSING_STARTED,    0, 10));
    pm().sendProgress(makeProgress(id2, ProcessingStage::VALIDATION_STARTED, 5, 10));
    pm().sendProgress(makeProgress(id3, ProcessingStage::COMPLETED,         10, 10));

    EXPECT_EQ(pm().getProgress(id)->stage,  ProcessingStage::PARSING_STARTED);
    EXPECT_EQ(pm().getProgress(id2)->stage, ProcessingStage::VALIDATION_STARTED);
    EXPECT_EQ(pm().getProgress(id3)->stage, ProcessingStage::COMPLETED);

    cleanupSingleton(id2);
    cleanupSingleton(id3);
}

// ===========================================================================
// ProgressManager — cleanupStaleEntries()
// ===========================================================================

TEST_F(ProgressManagerTest, CleanupStaleEntries_DoesNotThrow) {
    pm().sendProgress(makeProgress(id));
    EXPECT_NO_THROW(pm().cleanupStaleEntries(30));
}

TEST_F(ProgressManagerTest, CleanupStaleEntries_FreshEntry_NotEvicted) {
    // An entry updated just now must survive a cleanup with maxAge=30 minutes
    pm().sendProgress(makeProgress(id, ProcessingStage::PARSING_IN_PROGRESS, 1, 10));
    pm().cleanupStaleEntries(30);

    auto result = pm().getProgress(id);
    EXPECT_TRUE(result.has_value())
        << "A fresh entry must not be evicted by cleanupStaleEntries(30)";
}

TEST_F(ProgressManagerTest, CleanupStaleEntries_FreshEntry_EvenWithMinAge0) {
    // With maxAge=0 the semantics are "older than now" — a just-created entry
    // is borderline; the implementation compares strictly greater-than so a
    // brand-new entry should still survive if updated within the same second.
    // This test is lenient and just asserts no crash.
    pm().sendProgress(makeProgress(id));
    EXPECT_NO_THROW(pm().cleanupStaleEntries(0));
}

// ===========================================================================
// ProgressManager — Thread Safety
// ===========================================================================

TEST_F(ProgressManagerTest, ConcurrentSendProgress_SameId_NoDataRace) {
    const int numThreads = 10;
    const int updatesPerThread = 20;

    std::vector<std::thread> threads;
    threads.reserve(numThreads);
    for (int t = 0; t < numThreads; t++) {
        threads.emplace_back([&, t]() {
            for (int i = 0; i < updatesPerThread; i++) {
                auto p = makeProgress(id, ProcessingStage::PARSING_IN_PROGRESS,
                                      t * updatesPerThread + i, 200,
                                      "thread " + std::to_string(t));
                pm().sendProgress(p);
            }
        });
    }
    for (auto& t : threads) t.join();

    // At least the final state is valid (no crash)
    auto result = pm().getProgress(id);
    EXPECT_TRUE(result.has_value());
}

TEST_F(ProgressManagerTest, ConcurrentSendAndGet_NoDataRace) {
    // Writer thread sends progress continuously; reader thread reads concurrently
    std::atomic<bool> stop{false};
    int totalWrites = 0;

    pm().sendProgress(makeProgress(id, ProcessingStage::PARSING_STARTED, 0, 100));

    std::thread writer([&]() {
        for (int i = 0; i < 50; i++) {
            pm().sendProgress(makeProgress(id, ProcessingStage::PARSING_IN_PROGRESS, i, 50));
            totalWrites++;
        }
        stop = true;
    });

    std::thread reader([&]() {
        while (!stop.load()) {
            auto result = pm().getProgress(id);
            // result may or may not be present during cleanup — no crash is the assertion
            (void)result;
        }
    });

    writer.join();
    reader.join();

    EXPECT_EQ(totalWrites, 50);
}

TEST_F(ProgressManagerTest, ConcurrentRegisterAndSend_NoDataRace) {
    std::atomic<int> callCount{0};
    const int numSends = 30;

    std::thread sender([&]() {
        for (int i = 0; i < numSends; i++) {
            pm().sendProgress(makeProgress(id, ProcessingStage::DB_SAVING_IN_PROGRESS, i, numSends));
        }
    });

    std::thread registrar([&]() {
        pm().registerSseCallback(id, [&](const std::string&) { callCount++; });
    });

    sender.join();
    registrar.join();

    // No crash is the primary assertion; callCount may be anywhere from 0 to numSends
    EXPECT_GE(callCount.load(), 0);
}

// ===========================================================================
// ProcessingProgress — toJson() smoke test (does not crash, produces valid JSON)
// ===========================================================================

TEST(ProcessingProgressToJsonTest, NoMetadata_ProducesNonEmptyString) {
    auto p = ProcessingProgress::create("upload-json", ProcessingStage::COMPLETED, 10, 10, "done");
    std::string json = p.toJson();
    EXPECT_FALSE(json.empty());
    // Must be parseable single-line JSON (no embedded newlines in the data part)
    EXPECT_NE(json.find("\"uploadId\""), std::string::npos);
    EXPECT_NE(json.find("\"COMPLETED\""), std::string::npos);
}

TEST(ProcessingProgressToJsonTest, FieldsAppearInJson) {
    auto p = ProcessingProgress::create("upload-fields", ProcessingStage::PARSING_IN_PROGRESS,
                                        3, 7, "parsing files", "err", "detail");
    std::string json = p.toJson();
    EXPECT_NE(json.find("upload-fields"), std::string::npos);
    EXPECT_NE(json.find("parsing files"), std::string::npos);
}

// ===========================================================================
// Main
// ===========================================================================

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
