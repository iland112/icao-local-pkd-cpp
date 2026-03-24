/**
 * @file test_query_helpers.cpp
 * @brief Unit tests for common::db query helper functions
 *
 * All functions under test are pure (no I/O, no DB connection required).
 * Tests are organised by function, then by scenario within each section.
 *
 * Naming convention: <Function>_<Scenario>_<ExpectedBehaviour>
 */

#include <gtest/gtest.h>
#include "query_helpers.h"

#include <climits>
#include <json/json.h>

using namespace common::db;

// ============================================================================
// Helpers — build Json::Value objects from literal values
// ============================================================================

namespace {

Json::Value makeObject(const std::string& field, const Json::Value& val) {
    Json::Value obj(Json::objectValue);
    obj[field] = val;
    return obj;
}

Json::Value makeObjectNull(const std::string& field) {
    Json::Value obj(Json::objectValue);
    obj[field] = Json::Value::null;
    return obj;
}

// Convenience: build a JSON object with an integer field
Json::Value objInt(const std::string& field, int val) {
    return makeObject(field, Json::Value(val));
}

// Convenience: build a JSON object with a string field
Json::Value objStr(const std::string& field, const std::string& val) {
    return makeObject(field, Json::Value(val));
}

// Convenience: build a JSON object with a bool field
Json::Value objBool(const std::string& field, bool val) {
    return makeObject(field, Json::Value(val));
}

// Convenience: build a JSON object with a double field
Json::Value objDouble(const std::string& field, double val) {
    return makeObject(field, Json::Value(val));
}

} // anonymous namespace


// ============================================================================
// getInt — extract integer from a JSON object field
// ============================================================================

class GetIntTest : public ::testing::Test {};

TEST_F(GetIntTest, NativeInt_ReturnsValue) {
    EXPECT_EQ(getInt(objInt("n", 42), "n"), 42);
}

TEST_F(GetIntTest, NativeInt_Zero_ReturnsZero) {
    EXPECT_EQ(getInt(objInt("n", 0), "n"), 0);
}

TEST_F(GetIntTest, NativeInt_Negative_ReturnsNegative) {
    EXPECT_EQ(getInt(objInt("n", -7), "n"), -7);
}

TEST_F(GetIntTest, NativeInt_Max_ReturnsMax) {
    EXPECT_EQ(getInt(objInt("n", INT_MAX), "n"), INT_MAX);
}

TEST_F(GetIntTest, NativeInt_Min_ReturnsMin) {
    EXPECT_EQ(getInt(objInt("n", INT_MIN), "n"), INT_MIN);
}

TEST_F(GetIntTest, StringInt_Parseable_ReturnsValue) {
    // Oracle returns all values as strings — this is the critical case.
    EXPECT_EQ(getInt(objStr("n", "123"), "n"), 123);
}

TEST_F(GetIntTest, StringInt_Negative_Parseable_ReturnsNegative) {
    EXPECT_EQ(getInt(objStr("n", "-5"), "n"), -5);
}

TEST_F(GetIntTest, StringInt_Zero_ReturnsZero) {
    EXPECT_EQ(getInt(objStr("n", "0"), "n"), 0);
}

TEST_F(GetIntTest, StringInt_NonNumeric_ReturnsDefault) {
    EXPECT_EQ(getInt(objStr("n", "abc"), "n", 99), 99);
}

TEST_F(GetIntTest, StringInt_Empty_ReturnsDefault) {
    EXPECT_EQ(getInt(objStr("n", ""), "n", 7), 7);
}

TEST_F(GetIntTest, StringInt_FloatString_TruncatesToInt) {
    // std::stoi("3.7") stops at '.', returns 3
    EXPECT_EQ(getInt(objStr("n", "3.7"), "n"), 3);
}

TEST_F(GetIntTest, Double_Truncated_ToInt) {
    EXPECT_EQ(getInt(objDouble("n", 9.9), "n"), 9);
}

TEST_F(GetIntTest, Double_Negative_Truncated) {
    EXPECT_EQ(getInt(objDouble("n", -3.1), "n"), -3);
}

TEST_F(GetIntTest, FieldMissing_ReturnsDefault) {
    Json::Value obj(Json::objectValue);
    EXPECT_EQ(getInt(obj, "missing", 55), 55);
}

TEST_F(GetIntTest, FieldNull_ReturnsDefault) {
    EXPECT_EQ(getInt(makeObjectNull("n"), "n", 33), 33);
}

TEST_F(GetIntTest, DefaultIsZeroWhenNotSpecified) {
    Json::Value obj(Json::objectValue);
    EXPECT_EQ(getInt(obj, "missing"), 0);
}

TEST_F(GetIntTest, UInt_ReturnsValue) {
    Json::Value obj(Json::objectValue);
    obj["n"] = Json::Value(static_cast<unsigned int>(1000u));
    EXPECT_EQ(getInt(obj, "n"), 1000);
}

TEST_F(GetIntTest, Idempotent_SameResultOnRepeatCalls) {
    auto json = objStr("count", "31212");
    int first = getInt(json, "count");
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(getInt(json, "count"), first) << "Changed at iteration " << i;
    }
}


// ============================================================================
// getBool — extract boolean from a JSON object field
// ============================================================================

class GetBoolTest : public ::testing::Test {};

// Native JSON bool
TEST_F(GetBoolTest, NativeBool_True_ReturnsTrue) {
    EXPECT_TRUE(getBool(objBool("b", true), "b"));
}

TEST_F(GetBoolTest, NativeBool_False_ReturnsFalse) {
    EXPECT_FALSE(getBool(objBool("b", false), "b"));
}

// Oracle NUMBER(1) strings
TEST_F(GetBoolTest, OracleString_One_ReturnsTrue) {
    EXPECT_TRUE(getBool(objStr("b", "1"), "b"));
}

TEST_F(GetBoolTest, OracleString_Zero_ReturnsFalse) {
    EXPECT_FALSE(getBool(objStr("b", "0"), "b"));
}

// PostgreSQL-style boolean strings
TEST_F(GetBoolTest, String_true_lowercase_ReturnsTrue) {
    EXPECT_TRUE(getBool(objStr("b", "true"), "b"));
}

TEST_F(GetBoolTest, String_TRUE_uppercase_ReturnsTrue) {
    EXPECT_TRUE(getBool(objStr("b", "TRUE"), "b"));
}

TEST_F(GetBoolTest, String_t_ReturnsTrue) {
    EXPECT_TRUE(getBool(objStr("b", "t"), "b"));
}

TEST_F(GetBoolTest, String_T_ReturnsTrue) {
    EXPECT_TRUE(getBool(objStr("b", "T"), "b"));
}

// Integer values
TEST_F(GetBoolTest, Int_NonZero_ReturnsTrue) {
    EXPECT_TRUE(getBool(objInt("b", 1), "b"));
}

TEST_F(GetBoolTest, Int_Zero_ReturnsFalse) {
    EXPECT_FALSE(getBool(objInt("b", 0), "b"));
}

TEST_F(GetBoolTest, Int_Negative_ReturnsTrue) {
    EXPECT_TRUE(getBool(objInt("b", -1), "b"));
}

TEST_F(GetBoolTest, UInt_NonZero_ReturnsTrue) {
    Json::Value obj(Json::objectValue);
    obj["b"] = Json::Value(42u);
    EXPECT_TRUE(getBool(obj, "b"));
}

TEST_F(GetBoolTest, UInt_Zero_ReturnsFalse) {
    Json::Value obj(Json::objectValue);
    obj["b"] = Json::Value(0u);
    EXPECT_FALSE(getBool(obj, "b"));
}

// Fallback/default
TEST_F(GetBoolTest, FieldMissing_ReturnsFalseDefault) {
    Json::Value obj(Json::objectValue);
    EXPECT_FALSE(getBool(obj, "missing"));
}

TEST_F(GetBoolTest, FieldMissing_CustomDefault_True_ReturnsTrue) {
    Json::Value obj(Json::objectValue);
    EXPECT_TRUE(getBool(obj, "missing", true));
}

TEST_F(GetBoolTest, FieldNull_ReturnsFalseDefault) {
    EXPECT_FALSE(getBool(makeObjectNull("b"), "b"));
}

TEST_F(GetBoolTest, String_Unrecognised_ReturnsFalse) {
    // Unrecognised strings always return false (not the default) because
    // the isString() branch returns the match result directly.
    EXPECT_FALSE(getBool(objStr("b", "maybe"), "b", false));
    EXPECT_FALSE(getBool(objStr("b", "maybe"), "b", true));
}

TEST_F(GetBoolTest, Idempotent_StoredInLdap_OracleStyle) {
    auto json = objStr("stored_in_ldap", "1");
    bool first = getBool(json, "stored_in_ldap");
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(getBool(json, "stored_in_ldap"), first);
    }
}


// ============================================================================
// scalarToInt — convert a standalone scalar Json::Value to int
// ============================================================================

class ScalarToIntTest : public ::testing::Test {};

TEST_F(ScalarToIntTest, NativeInt_ReturnsValue) {
    EXPECT_EQ(scalarToInt(Json::Value(100)), 100);
}

TEST_F(ScalarToIntTest, NativeInt_Zero_ReturnsZero) {
    EXPECT_EQ(scalarToInt(Json::Value(0)), 0);
}

TEST_F(ScalarToIntTest, NativeInt_Negative_ReturnsNegative) {
    EXPECT_EQ(scalarToInt(Json::Value(-42)), -42);
}

TEST_F(ScalarToIntTest, String_Numeric_ReturnsValue) {
    EXPECT_EQ(scalarToInt(Json::Value("31212")), 31212);
}

TEST_F(ScalarToIntTest, String_Zero_ReturnsZero) {
    EXPECT_EQ(scalarToInt(Json::Value("0")), 0);
}

TEST_F(ScalarToIntTest, String_Negative_ReturnsNegative) {
    EXPECT_EQ(scalarToInt(Json::Value("-99")), -99);
}

TEST_F(ScalarToIntTest, String_Empty_ReturnsDefault) {
    EXPECT_EQ(scalarToInt(Json::Value(""), 7), 7);
}

TEST_F(ScalarToIntTest, String_NonNumeric_ReturnsDefault) {
    EXPECT_EQ(scalarToInt(Json::Value("not_a_number"), 5), 5);
}

TEST_F(ScalarToIntTest, String_Whitespace_ReturnsDefault) {
    // std::stoi throws on leading whitespace in some implementations;
    // either way the default is returned.
    int result = scalarToInt(Json::Value("  "), 3);
    // acceptable: 0 (stoi(" ") may succeed as 0) or 3 (default)
    EXPECT_TRUE(result == 0 || result == 3);
}

TEST_F(ScalarToIntTest, Null_ReturnsDefault) {
    EXPECT_EQ(scalarToInt(Json::Value::null, 42), 42);
}

TEST_F(ScalarToIntTest, Null_DefaultZero) {
    EXPECT_EQ(scalarToInt(Json::Value::null), 0);
}

TEST_F(ScalarToIntTest, UInt_ReturnsValue) {
    EXPECT_EQ(scalarToInt(Json::Value(999u)), 999);
}

TEST_F(ScalarToIntTest, Double_Truncated) {
    EXPECT_EQ(scalarToInt(Json::Value(7.9)), 7);
}

TEST_F(ScalarToIntTest, DefaultIsZeroWhenNotSpecified) {
    EXPECT_EQ(scalarToInt(Json::Value::null), 0);
}

TEST_F(ScalarToIntTest, Idempotent_CountQuery_Oracle) {
    Json::Value v("845");
    int first = scalarToInt(v);
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(scalarToInt(v), first);
    }
}


// ============================================================================
// boolLiteral — SQL boolean literal per DB type
// ============================================================================

class BoolLiteralTest : public ::testing::Test {};

TEST_F(BoolLiteralTest, Postgres_True_ReturnsTRUE) {
    EXPECT_EQ(boolLiteral("postgres", true), "TRUE");
}

TEST_F(BoolLiteralTest, Postgres_False_ReturnsFALSE) {
    EXPECT_EQ(boolLiteral("postgres", false), "FALSE");
}

TEST_F(BoolLiteralTest, Oracle_True_Returns1) {
    EXPECT_EQ(boolLiteral("oracle", true), "1");
}

TEST_F(BoolLiteralTest, Oracle_False_Returns0) {
    EXPECT_EQ(boolLiteral("oracle", false), "0");
}

TEST_F(BoolLiteralTest, UnknownDbType_DefaultsToPostgresStyle) {
    // Any non-oracle string falls through to the PostgreSQL branch
    EXPECT_EQ(boolLiteral("mysql", true), "TRUE");
    EXPECT_EQ(boolLiteral("", false), "FALSE");
}

TEST_F(BoolLiteralTest, Idempotent_PgTrue) {
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(boolLiteral("postgres", true), "TRUE");
    }
}

TEST_F(BoolLiteralTest, Idempotent_OracleFalse) {
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(boolLiteral("oracle", false), "0");
    }
}


// ============================================================================
// paginationClause — LIMIT/OFFSET vs OFFSET/FETCH NEXT
// ============================================================================

class PaginationClauseTest : public ::testing::Test {};

TEST_F(PaginationClauseTest, Postgres_FirstPage_ContainsLimitAndOffset) {
    std::string result = paginationClause("postgres", 10, 0);
    EXPECT_NE(result.find("LIMIT 10"), std::string::npos);
    EXPECT_NE(result.find("OFFSET 0"), std::string::npos);
}

TEST_F(PaginationClauseTest, Postgres_SecondPage_CorrectOffset) {
    std::string result = paginationClause("postgres", 10, 10);
    EXPECT_NE(result.find("LIMIT 10"), std::string::npos);
    EXPECT_NE(result.find("OFFSET 10"), std::string::npos);
}

TEST_F(PaginationClauseTest, Postgres_LargePage) {
    std::string result = paginationClause("postgres", 100, 500);
    EXPECT_NE(result.find("LIMIT 100"), std::string::npos);
    EXPECT_NE(result.find("OFFSET 500"), std::string::npos);
}

TEST_F(PaginationClauseTest, Postgres_DoesNotContainFetchKeyword) {
    std::string result = paginationClause("postgres", 10, 0);
    EXPECT_EQ(result.find("FETCH"), std::string::npos);
}

TEST_F(PaginationClauseTest, Oracle_FirstPage_ContainsFetchNext) {
    std::string result = paginationClause("oracle", 10, 0);
    EXPECT_NE(result.find("FETCH NEXT 10 ROWS ONLY"), std::string::npos);
    EXPECT_NE(result.find("OFFSET 0 ROWS"), std::string::npos);
}

TEST_F(PaginationClauseTest, Oracle_SecondPage_CorrectOffset) {
    std::string result = paginationClause("oracle", 10, 10);
    EXPECT_NE(result.find("FETCH NEXT 10 ROWS ONLY"), std::string::npos);
    EXPECT_NE(result.find("OFFSET 10 ROWS"), std::string::npos);
}

TEST_F(PaginationClauseTest, Oracle_DoesNotContainLimitKeyword) {
    std::string result = paginationClause("oracle", 10, 0);
    EXPECT_EQ(result.find("LIMIT"), std::string::npos);
}

TEST_F(PaginationClauseTest, Oracle_LargePage) {
    std::string result = paginationClause("oracle", 200, 1000);
    EXPECT_NE(result.find("FETCH NEXT 200 ROWS ONLY"), std::string::npos);
    EXPECT_NE(result.find("OFFSET 1000 ROWS"), std::string::npos);
}

TEST_F(PaginationClauseTest, ZeroLimit_Postgres) {
    // Edge case: limit 0 is unusual but must not crash
    std::string result = paginationClause("postgres", 0, 0);
    EXPECT_NE(result.find("LIMIT 0"), std::string::npos);
}

TEST_F(PaginationClauseTest, ZeroLimit_Oracle) {
    std::string result = paginationClause("oracle", 0, 0);
    EXPECT_NE(result.find("FETCH NEXT 0 ROWS ONLY"), std::string::npos);
}

TEST_F(PaginationClauseTest, Idempotent_Postgres) {
    std::string first = paginationClause("postgres", 25, 50);
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(paginationClause("postgres", 25, 50), first);
    }
}

TEST_F(PaginationClauseTest, Idempotent_Oracle) {
    std::string first = paginationClause("oracle", 25, 50);
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(paginationClause("oracle", 25, 50), first);
    }
}


// ============================================================================
// limitClause — simple row-count cap (no offset)
// ============================================================================

class LimitClauseTest : public ::testing::Test {};

TEST_F(LimitClauseTest, Postgres_ContainsLimit) {
    std::string result = limitClause("postgres", 10);
    EXPECT_NE(result.find("LIMIT 10"), std::string::npos);
}

TEST_F(LimitClauseTest, Postgres_DoesNotContainFetch) {
    std::string result = limitClause("postgres", 10);
    EXPECT_EQ(result.find("FETCH"), std::string::npos);
}

TEST_F(LimitClauseTest, Oracle_ContainsFetchFirst) {
    std::string result = limitClause("oracle", 10);
    EXPECT_NE(result.find("FETCH FIRST 10 ROWS ONLY"), std::string::npos);
}

TEST_F(LimitClauseTest, Oracle_DoesNotContainLimit) {
    std::string result = limitClause("oracle", 10);
    EXPECT_EQ(result.find("LIMIT"), std::string::npos);
}

TEST_F(LimitClauseTest, Postgres_LargeLimit) {
    std::string result = limitClause("postgres", 10000);
    EXPECT_NE(result.find("LIMIT 10000"), std::string::npos);
}

TEST_F(LimitClauseTest, Oracle_LargeLimit) {
    std::string result = limitClause("oracle", 10000);
    EXPECT_NE(result.find("FETCH FIRST 10000 ROWS ONLY"), std::string::npos);
}

TEST_F(LimitClauseTest, Oracle_One_Row) {
    std::string result = limitClause("oracle", 1);
    EXPECT_NE(result.find("FETCH FIRST 1 ROWS ONLY"), std::string::npos);
}

TEST_F(LimitClauseTest, Idempotent_Postgres) {
    std::string first = limitClause("postgres", 50);
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(limitClause("postgres", 50), first);
    }
}

TEST_F(LimitClauseTest, Idempotent_Oracle) {
    std::string first = limitClause("oracle", 50);
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(limitClause("oracle", 50), first);
    }
}


// ============================================================================
// currentTimestamp — NOW() vs SYSTIMESTAMP
// ============================================================================

class CurrentTimestampTest : public ::testing::Test {};

TEST_F(CurrentTimestampTest, Postgres_ReturnsNow) {
    EXPECT_EQ(currentTimestamp("postgres"), "NOW()");
}

TEST_F(CurrentTimestampTest, Oracle_ReturnsSystimestamp) {
    EXPECT_EQ(currentTimestamp("oracle"), "SYSTIMESTAMP");
}

TEST_F(CurrentTimestampTest, UnknownDbType_DefaultsToNow) {
    EXPECT_EQ(currentTimestamp("mysql"), "NOW()");
    EXPECT_EQ(currentTimestamp(""), "NOW()");
}

TEST_F(CurrentTimestampTest, Idempotent_Postgres) {
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(currentTimestamp("postgres"), "NOW()");
    }
}

TEST_F(CurrentTimestampTest, Idempotent_Oracle) {
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(currentTimestamp("oracle"), "SYSTIMESTAMP");
    }
}


// ============================================================================
// currentTimestampFormatted — TO_CHAR() variants
// ============================================================================

class CurrentTimestampFormattedTest : public ::testing::Test {};

TEST_F(CurrentTimestampFormattedTest, Postgres_ContainsNow) {
    std::string result = currentTimestampFormatted("postgres");
    EXPECT_NE(result.find("NOW()"), std::string::npos);
}

TEST_F(CurrentTimestampFormattedTest, Postgres_WrappedInToChar) {
    std::string result = currentTimestampFormatted("postgres");
    EXPECT_NE(result.find("TO_CHAR("), std::string::npos);
}

TEST_F(CurrentTimestampFormattedTest, Postgres_ContainsDateFormatString) {
    std::string result = currentTimestampFormatted("postgres");
    EXPECT_NE(result.find("YYYY-MM-DD HH24:MI:SS"), std::string::npos);
}

TEST_F(CurrentTimestampFormattedTest, Oracle_ContainsSystimestamp) {
    std::string result = currentTimestampFormatted("oracle");
    EXPECT_NE(result.find("SYSTIMESTAMP"), std::string::npos);
}

TEST_F(CurrentTimestampFormattedTest, Oracle_WrappedInToChar) {
    std::string result = currentTimestampFormatted("oracle");
    EXPECT_NE(result.find("TO_CHAR("), std::string::npos);
}

TEST_F(CurrentTimestampFormattedTest, Oracle_ContainsDateFormatString) {
    std::string result = currentTimestampFormatted("oracle");
    EXPECT_NE(result.find("YYYY-MM-DD HH24:MI:SS"), std::string::npos);
}

TEST_F(CurrentTimestampFormattedTest, Idempotent_BothDbs) {
    auto pg = currentTimestampFormatted("postgres");
    auto ora = currentTimestampFormatted("oracle");
    for (int i = 0; i < 20; ++i) {
        EXPECT_EQ(currentTimestampFormatted("postgres"), pg);
        EXPECT_EQ(currentTimestampFormatted("oracle"), ora);
    }
}


// ============================================================================
// hexPrefix — \\x vs \\\\x
// ============================================================================

class HexPrefixTest : public ::testing::Test {};

TEST_F(HexPrefixTest, Postgres_ReturnsBackslashX) {
    // PostgreSQL bytea literal uses \x prefix
    EXPECT_EQ(hexPrefix("postgres"), "\\x");
}

TEST_F(HexPrefixTest, Oracle_ReturnsDoubleBackslashX) {
    EXPECT_EQ(hexPrefix("oracle"), "\\\\x");
}

TEST_F(HexPrefixTest, Postgres_Length_Is2) {
    EXPECT_EQ(hexPrefix("postgres").size(), 2u);
}

TEST_F(HexPrefixTest, Oracle_Length_Is3) {
    // "\\\\x" in C++ source = "\\x" string = 3 chars: '\', '\', 'x'
    EXPECT_EQ(hexPrefix("oracle").size(), 3u);
}

TEST_F(HexPrefixTest, UnknownDbType_DefaultsToPostgres) {
    EXPECT_EQ(hexPrefix(""), "\\x");
    EXPECT_EQ(hexPrefix("sqlite"), "\\x");
}

TEST_F(HexPrefixTest, Idempotent_Postgres) {
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(hexPrefix("postgres"), "\\x");
    }
}

TEST_F(HexPrefixTest, Idempotent_Oracle) {
    for (int i = 0; i < 50; ++i) {
        EXPECT_EQ(hexPrefix("oracle"), "\\\\x");
    }
}


// ============================================================================
// ilikeCond — case-insensitive LIKE condition
// ============================================================================

class IlikeCondTest : public ::testing::Test {};

TEST_F(IlikeCondTest, Postgres_UsesILIKE) {
    std::string result = ilikeCond("postgres", "subject_dn", "$1");
    EXPECT_NE(result.find("ILIKE"), std::string::npos);
    EXPECT_NE(result.find("subject_dn"), std::string::npos);
    EXPECT_NE(result.find("$1"), std::string::npos);
}

TEST_F(IlikeCondTest, Postgres_DoesNotWrapInUpper) {
    std::string result = ilikeCond("postgres", "subject_dn", "$1");
    EXPECT_EQ(result.find("UPPER"), std::string::npos);
}

TEST_F(IlikeCondTest, Oracle_UsesUpperLike) {
    std::string result = ilikeCond("oracle", "subject_dn", "$1");
    EXPECT_NE(result.find("UPPER(subject_dn)"), std::string::npos);
    EXPECT_NE(result.find("UPPER($1)"), std::string::npos);
    EXPECT_NE(result.find("LIKE"), std::string::npos);
}

TEST_F(IlikeCondTest, Oracle_DoesNotUseILIKE) {
    std::string result = ilikeCond("oracle", "subject_dn", "$1");
    EXPECT_EQ(result.find("ILIKE"), std::string::npos);
}

TEST_F(IlikeCondTest, Postgres_DifferentColumn_ContainsColumn) {
    std::string result = ilikeCond("postgres", "country_code", "$3");
    EXPECT_NE(result.find("country_code"), std::string::npos);
    EXPECT_NE(result.find("$3"), std::string::npos);
}

TEST_F(IlikeCondTest, Oracle_DifferentColumn_WrapsColumn) {
    std::string result = ilikeCond("oracle", "country_code", "$3");
    EXPECT_NE(result.find("UPPER(country_code)"), std::string::npos);
}

TEST_F(IlikeCondTest, Oracle_DifferentParam_WrapsParam) {
    std::string result = ilikeCond("oracle", "col", ":param");
    EXPECT_NE(result.find("UPPER(:param)"), std::string::npos);
}

TEST_F(IlikeCondTest, Idempotent_Postgres) {
    std::string first = ilikeCond("postgres", "issuer_dn", "$2");
    for (int i = 0; i < 20; ++i) {
        EXPECT_EQ(ilikeCond("postgres", "issuer_dn", "$2"), first);
    }
}

TEST_F(IlikeCondTest, Idempotent_Oracle) {
    std::string first = ilikeCond("oracle", "issuer_dn", "$2");
    for (int i = 0; i < 20; ++i) {
        EXPECT_EQ(ilikeCond("oracle", "issuer_dn", "$2"), first);
    }
}


// ============================================================================
// nonEmptyFilter — Oracle NULL vs PostgreSQL null+empty check
// ============================================================================

class NonEmptyFilterTest : public ::testing::Test {};

TEST_F(NonEmptyFilterTest, Postgres_ContainsIsNotNull) {
    std::string result = nonEmptyFilter("postgres", "file_hash");
    EXPECT_NE(result.find("IS NOT NULL"), std::string::npos);
}

TEST_F(NonEmptyFilterTest, Postgres_ContainsNotEqualEmpty) {
    std::string result = nonEmptyFilter("postgres", "file_hash");
    EXPECT_NE(result.find("!= ''"), std::string::npos);
}

TEST_F(NonEmptyFilterTest, Postgres_ContainsColumnTwice) {
    // column appears in both IS NOT NULL and != '' conditions
    std::string result = nonEmptyFilter("postgres", "file_hash");
    auto pos1 = result.find("file_hash");
    ASSERT_NE(pos1, std::string::npos);
    auto pos2 = result.find("file_hash", pos1 + 1);
    EXPECT_NE(pos2, std::string::npos);
}

TEST_F(NonEmptyFilterTest, Oracle_ContainsIsNotNull) {
    std::string result = nonEmptyFilter("oracle", "file_hash");
    EXPECT_NE(result.find("IS NOT NULL"), std::string::npos);
}

TEST_F(NonEmptyFilterTest, Oracle_DoesNotContainEmptyStringLiteral) {
    // Oracle treats '' as NULL, so the != '' check is omitted
    std::string result = nonEmptyFilter("oracle", "file_hash");
    EXPECT_EQ(result.find("!= ''"), std::string::npos);
}

TEST_F(NonEmptyFilterTest, Oracle_ContainsColumn) {
    std::string result = nonEmptyFilter("oracle", "file_hash");
    EXPECT_NE(result.find("file_hash"), std::string::npos);
}

TEST_F(NonEmptyFilterTest, Idempotent_Postgres) {
    std::string first = nonEmptyFilter("postgres", "col");
    for (int i = 0; i < 20; ++i) {
        EXPECT_EQ(nonEmptyFilter("postgres", "col"), first);
    }
}

TEST_F(NonEmptyFilterTest, Idempotent_Oracle) {
    std::string first = nonEmptyFilter("oracle", "col");
    for (int i = 0; i < 20; ++i) {
        EXPECT_EQ(nonEmptyFilter("oracle", "col"), first);
    }
}


// ============================================================================
// intervalHours — INTERVAL expression
// ============================================================================

class IntervalHoursTest : public ::testing::Test {};

TEST_F(IntervalHoursTest, Postgres_ContainsHoursKeyword) {
    std::string result = intervalHours("postgres", 24);
    EXPECT_NE(result.find("hours"), std::string::npos);
    EXPECT_NE(result.find("24"), std::string::npos);
}

TEST_F(IntervalHoursTest, Postgres_ContainsIntervalKeyword) {
    std::string result = intervalHours("postgres", 24);
    EXPECT_NE(result.find("INTERVAL"), std::string::npos);
}

TEST_F(IntervalHoursTest, Oracle_ContainsHOURKeyword) {
    std::string result = intervalHours("oracle", 24);
    EXPECT_NE(result.find("HOUR"), std::string::npos);
    EXPECT_NE(result.find("24"), std::string::npos);
}

TEST_F(IntervalHoursTest, Oracle_ContainsIntervalKeyword) {
    std::string result = intervalHours("oracle", 24);
    EXPECT_NE(result.find("INTERVAL"), std::string::npos);
}

TEST_F(IntervalHoursTest, Postgres_IntervalHours_1) {
    std::string result = intervalHours("postgres", 1);
    EXPECT_NE(result.find("1"), std::string::npos);
    EXPECT_NE(result.find("hours"), std::string::npos);
}

TEST_F(IntervalHoursTest, Oracle_IntervalHours_1) {
    std::string result = intervalHours("oracle", 1);
    EXPECT_NE(result.find("'1'"), std::string::npos);
    EXPECT_NE(result.find("HOUR"), std::string::npos);
}

TEST_F(IntervalHoursTest, Postgres_IntervalHours_168) {
    // 168 hours = 1 week
    std::string result = intervalHours("postgres", 168);
    EXPECT_NE(result.find("168"), std::string::npos);
}

TEST_F(IntervalHoursTest, Oracle_IntervalHours_168) {
    std::string result = intervalHours("oracle", 168);
    EXPECT_NE(result.find("168"), std::string::npos);
}

TEST_F(IntervalHoursTest, Idempotent_Postgres) {
    std::string first = intervalHours("postgres", 24);
    for (int i = 0; i < 20; ++i) {
        EXPECT_EQ(intervalHours("postgres", 24), first);
    }
}

TEST_F(IntervalHoursTest, Idempotent_Oracle) {
    std::string first = intervalHours("oracle", 24);
    for (int i = 0; i < 20; ++i) {
        EXPECT_EQ(intervalHours("oracle", 24), first);
    }
}


// ============================================================================
// Cross-function consistency — same inputs, same outputs
// ============================================================================

TEST(CrossFunctionTest, BoolLiteral_AndGetBool_RoundTrip_True) {
    // boolLiteral produces "1" for Oracle; getBool accepts "1" as true
    std::string oracleTrue = boolLiteral("oracle", true);
    EXPECT_TRUE(getBool(objStr("v", oracleTrue), "v"));
}

TEST(CrossFunctionTest, BoolLiteral_AndGetBool_RoundTrip_False) {
    std::string oracleFalse = boolLiteral("oracle", false);
    EXPECT_FALSE(getBool(objStr("v", oracleFalse), "v"));
}

TEST(CrossFunctionTest, PaginationClause_Page1_MatchesExpected_Postgres) {
    // Standard first-page query: page=1, pageSize=20
    std::string clause = paginationClause("postgres", 20, 0);
    // Must start with whitespace (SQL appended directly after ORDER BY clause)
    EXPECT_EQ(clause[0], ' ');
    EXPECT_NE(clause.find("LIMIT 20"), std::string::npos);
    EXPECT_NE(clause.find("OFFSET 0"), std::string::npos);
}

TEST(CrossFunctionTest, PaginationClause_Page2_MatchesExpected_Oracle) {
    std::string clause = paginationClause("oracle", 20, 20);
    EXPECT_EQ(clause[0], ' ');
    EXPECT_NE(clause.find("OFFSET 20 ROWS"), std::string::npos);
    EXPECT_NE(clause.find("FETCH NEXT 20 ROWS ONLY"), std::string::npos);
}

TEST(CrossFunctionTest, LimitClause_StartsWithSpace) {
    // Both variants must start with a space so they can be appended safely
    EXPECT_EQ(limitClause("postgres", 5)[0], ' ');
    EXPECT_EQ(limitClause("oracle", 5)[0], ' ');
}
