#pragma once

#include <string>
#include <json/json.h>

/**
 * @file query_helpers.h
 * @brief Database-agnostic SQL helper utilities
 *
 * Provides utility functions that abstract database-specific SQL syntax differences
 * between PostgreSQL and Oracle. Eliminates repetitive if/else branching in repository code.
 *
 * Usage:
 *   #include "query_helpers.h"
 *   std::string dbType = queryExecutor_->getDatabaseType();
 *   sql << common::db::currentTimestamp(dbType);
 *   sql << common::db::paginationClause(dbType, limit, offset);
 *
 * @date 2026-02-17
 */

namespace common::db {

// ============================================================================
// JSON Value Extraction (Oracle returns all values as strings)
// ============================================================================

/**
 * @brief Extract integer from JSON value with type-safe conversion
 *
 * Oracle returns all column values as strings, so Json::Value::asInt() fails.
 * This handles int, uint, string, and double types gracefully.
 *
 * @param json JSON object containing the field
 * @param field Field name to extract
 * @param defaultValue Default value if field is missing, null, or unparseable
 * @return Extracted integer value
 */
int getInt(const Json::Value& json, const std::string& field, int defaultValue = 0);

/**
 * @brief Extract boolean from JSON value with type-safe conversion
 *
 * Handles Oracle NUMBER(1) (string "1"/"0") and PostgreSQL boolean (true/false).
 *
 * @param json JSON object containing the field
 * @param field Field name to extract
 * @param defaultValue Default value if field is missing or null
 * @return Extracted boolean value
 */
bool getBool(const Json::Value& json, const std::string& field, bool defaultValue = false);

/**
 * @brief Convert a scalar JSON value to integer
 *
 * Used with IQueryExecutor::executeScalar() results which return a single value.
 * Oracle returns scalars as strings, PostgreSQL as native types.
 *
 * @param value Scalar JSON value (not an object with fields)
 * @param defaultValue Default if null or unparseable
 * @return Converted integer value
 */
int scalarToInt(const Json::Value& value, int defaultValue = 0);

// ============================================================================
// SQL Expression Generation
// ============================================================================

/**
 * @brief Get current timestamp expression
 *
 * @param dbType "postgres" or "oracle"
 * @return "NOW()" for PostgreSQL, "SYSTIMESTAMP" for Oracle
 */
std::string currentTimestamp(const std::string& dbType);

/**
 * @brief Get current timestamp as formatted string expression
 *
 * @param dbType "postgres" or "oracle"
 * @return PostgreSQL: "TO_CHAR(NOW(), 'YYYY-MM-DD HH24:MI:SS')"
 *         Oracle:     "TO_CHAR(SYSTIMESTAMP, 'YYYY-MM-DD HH24:MI:SS')"
 */
std::string currentTimestampFormatted(const std::string& dbType);

/**
 * @brief Format boolean value as SQL literal
 *
 * @param dbType "postgres" or "oracle"
 * @param value Boolean value to format
 * @return "TRUE"/"FALSE" for PostgreSQL, "1"/"0" for Oracle
 */
std::string boolLiteral(const std::string& dbType, bool value);

/**
 * @brief Build pagination clause
 *
 * @param dbType "postgres" or "oracle"
 * @param limit Maximum rows to return
 * @param offset Number of rows to skip
 * @return PostgreSQL: " LIMIT 10 OFFSET 0"
 *         Oracle:     " OFFSET 0 ROWS FETCH NEXT 10 ROWS ONLY"
 */
std::string paginationClause(const std::string& dbType, int limit, int offset);

/**
 * @brief Build simple row limit clause (no offset)
 *
 * @param dbType "postgres" or "oracle"
 * @param limit Maximum rows to return
 * @return PostgreSQL: " LIMIT 10"
 *         Oracle:     " FETCH FIRST 10 ROWS ONLY"
 */
std::string limitClause(const std::string& dbType, int limit);

/**
 * @brief Build case-insensitive search condition
 *
 * @param dbType "postgres" or "oracle"
 * @param column Column name
 * @param paramPlaceholder Parameter placeholder (e.g., "$3")
 * @return PostgreSQL: "column ILIKE $3"
 *         Oracle:     "UPPER(column) LIKE UPPER($3)"
 */
std::string ilikeCond(const std::string& dbType, const std::string& column,
                      const std::string& paramPlaceholder);

/**
 * @brief Build non-empty string filter
 *
 * Oracle treats empty strings as NULL, so filtering differs.
 *
 * @param dbType "postgres" or "oracle"
 * @param column Column name
 * @return PostgreSQL: "column IS NOT NULL AND column != ''"
 *         Oracle:     "column IS NOT NULL"
 */
std::string nonEmptyFilter(const std::string& dbType, const std::string& column);

/**
 * @brief Get hex prefix for binary data encoding
 *
 * @param dbType "postgres" or "oracle"
 * @return "\\x" for PostgreSQL, "\\\\x" for Oracle
 */
std::string hexPrefix(const std::string& dbType);

/**
 * @brief Build interval expression
 *
 * @param dbType "postgres" or "oracle"
 * @param hours Number of hours
 * @return PostgreSQL: "INTERVAL '24 hours'"
 *         Oracle:     "INTERVAL '24' HOUR"
 */
std::string intervalHours(const std::string& dbType, int hours);

} // namespace common::db
