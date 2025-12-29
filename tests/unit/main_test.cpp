/**
 * @file main_test.cpp
 * @brief Main test file for Catch2
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_session.hpp>

// Shared kernel tests
#include "shared/domain/ValueObject.hpp"
#include "shared/exception/DomainException.hpp"

using namespace shared::domain;
using namespace shared::exception;

// =============================================================================
// Value Object Tests
// =============================================================================

class TestStringValue : public StringValueObject {
public:
    static TestStringValue of(const std::string& value) {
        return TestStringValue(value);
    }

private:
    explicit TestStringValue(std::string value)
        : StringValueObject(std::move(value)) {
        validate();
    }

    void validate() const override {
        if (value_.empty()) {
            throw DomainException("INVALID_VALUE", "Value cannot be empty");
        }
    }
};

TEST_CASE("StringValueObject equality", "[domain][valueobject]") {
    auto vo1 = TestStringValue::of("test");
    auto vo2 = TestStringValue::of("test");
    auto vo3 = TestStringValue::of("other");

    REQUIRE(vo1 == vo2);
    REQUIRE(vo1 != vo3);
}

TEST_CASE("StringValueObject getValue", "[domain][valueobject]") {
    auto vo = TestStringValue::of("hello");
    REQUIRE(vo.getValue() == "hello");
}

TEST_CASE("StringValueObject isEmpty", "[domain][valueobject]") {
    auto vo = TestStringValue::of("hello");
    REQUIRE_FALSE(vo.isEmpty());
}

TEST_CASE("StringValueObject validation throws", "[domain][valueobject]") {
    REQUIRE_THROWS_AS(TestStringValue::of(""), DomainException);
}

// =============================================================================
// Domain Exception Tests
// =============================================================================

TEST_CASE("DomainException code and message", "[exception]") {
    DomainException ex("TEST_CODE", "Test message");

    REQUIRE(ex.getCode() == "TEST_CODE");
    REQUIRE(ex.getMessage() == "Test message");
    REQUIRE(std::string(ex.what()) == "Test message");
}

TEST_CASE("DomainException can be caught as std::exception", "[exception]") {
    bool caught = false;

    try {
        throw DomainException("CODE", "Message");
    } catch (const std::exception& e) {
        caught = true;
        REQUIRE(std::string(e.what()) == "Message");
    }

    REQUIRE(caught);
}

// =============================================================================
// Passive Authentication Tests
// =============================================================================

#include "passiveauthentication/domain/model/DataGroupNumber.hpp"
#include "passiveauthentication/domain/model/PassiveAuthenticationStatus.hpp"
#include "passiveauthentication/domain/model/DataGroupHash.hpp"
#include "passiveauthentication/domain/model/PassportDataId.hpp"
#include "passiveauthentication/domain/model/SecurityObjectDocument.hpp"
#include "passiveauthentication/domain/model/DataGroup.hpp"
#include "passiveauthentication/domain/model/CrlCheckStatus.hpp"

using namespace pa::domain::model;

TEST_CASE("DataGroupNumber conversion", "[pa][domain]") {
    // toInt and dataGroupNumberFromInt
    REQUIRE(toInt(DataGroupNumber::DG1) == 1);
    REQUIRE(toInt(DataGroupNumber::DG2) == 2);
    REQUIRE(toInt(DataGroupNumber::DG14) == 14);

    REQUIRE(dataGroupNumberFromInt(1) == DataGroupNumber::DG1);
    REQUIRE(dataGroupNumberFromInt(16) == DataGroupNumber::DG16);
    REQUIRE_THROWS(dataGroupNumberFromInt(0));
    REQUIRE_THROWS(dataGroupNumberFromInt(17));
}

TEST_CASE("DataGroupNumber string conversion", "[pa][domain]") {
    // toString and dataGroupNumberFromString
    REQUIRE(toString(DataGroupNumber::DG1) == "DG1");
    REQUIRE(toString(DataGroupNumber::DG2) == "DG2");

    REQUIRE(dataGroupNumberFromString("DG1") == DataGroupNumber::DG1);
    REQUIRE(dataGroupNumberFromString("DG16") == DataGroupNumber::DG16);
    REQUIRE_THROWS(dataGroupNumberFromString("INVALID"));
}

TEST_CASE("PassiveAuthenticationStatus enum", "[pa][domain]") {
    REQUIRE(toString(PassiveAuthenticationStatus::VALID) == "VALID");
    REQUIRE(toString(PassiveAuthenticationStatus::INVALID) == "INVALID");
    REQUIRE(toString(PassiveAuthenticationStatus::ERROR) == "ERROR");
}

TEST_CASE("DataGroupHash creation from bytes", "[pa][domain]") {
    // SHA-256 hash is 32 bytes = 64 hex chars
    std::vector<uint8_t> hashData(32, 0xAB);  // 32 bytes of 0xAB
    auto hash = DataGroupHash::of(hashData);

    REQUIRE(hash.getValue().length() == 64);  // 32 bytes = 64 hex chars
    REQUIRE(hash.getBytes() == hashData);
}

TEST_CASE("PassportDataId generation", "[pa][domain]") {
    auto id1 = PassportDataId::generate();
    auto id2 = PassportDataId::generate();

    REQUIRE_FALSE(id1.getId().empty());
    REQUIRE(id1 != id2);  // UUIDs should be unique
}

TEST_CASE("CrlCheckStatus enum values", "[pa][domain]") {
    REQUIRE(toString(CrlCheckStatus::VALID) == "VALID");
    REQUIRE(toString(CrlCheckStatus::REVOKED) == "REVOKED");
    REQUIRE(toString(CrlCheckStatus::CRL_UNAVAILABLE) == "CRL_UNAVAILABLE");
    REQUIRE(toString(CrlCheckStatus::CRL_EXPIRED) == "CRL_EXPIRED");
}

TEST_CASE("SecurityObjectDocument Tag detection", "[pa][domain]") {
    // Tag 0x30 (SEQUENCE) - valid SOD start
    std::vector<uint8_t> validSod = {0x30, 0x82, 0x01, 0x00};
    REQUIRE_NOTHROW(SecurityObjectDocument::of(validSod));

    // Empty data should throw
    std::vector<uint8_t> emptyData;
    REQUIRE_THROWS(SecurityObjectDocument::of(emptyData));
}

TEST_CASE("DataGroup creation and access", "[pa][domain]") {
    std::vector<uint8_t> content = {0x01, 0x02, 0x03};
    auto dg = DataGroup::of(DataGroupNumber::DG1, content);

    REQUIRE(dg.getNumber() == DataGroupNumber::DG1);
    REQUIRE(dg.getContent() == content);
    REQUIRE(dg.getNumberValue() == 1);
}

// =============================================================================
// Placeholder tests for future modules
// =============================================================================

TEST_CASE("Placeholder: Certificate validation", "[cert][placeholder]") {
    // TODO: Implement certificate validation tests
    REQUIRE(true);
}

TEST_CASE("Placeholder: LDAP operations", "[ldap][placeholder]") {
    // TODO: Implement LDAP operation tests
    REQUIRE(true);
}
