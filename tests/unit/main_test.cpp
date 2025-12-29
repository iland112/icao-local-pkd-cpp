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
// Placeholder tests for future modules
// =============================================================================

TEST_CASE("Placeholder: SOD parsing", "[pa][placeholder]") {
    // TODO: Implement SOD parsing tests
    REQUIRE(true);
}

TEST_CASE("Placeholder: Certificate validation", "[cert][placeholder]") {
    // TODO: Implement certificate validation tests
    REQUIRE(true);
}

TEST_CASE("Placeholder: LDAP operations", "[ldap][placeholder]") {
    // TODO: Implement LDAP operation tests
    REQUIRE(true);
}
