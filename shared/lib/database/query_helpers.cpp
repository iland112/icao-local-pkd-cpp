/**
 * @file query_helpers.cpp
 * @brief Database-agnostic SQL helper utilities implementation
 * @date 2026-02-17
 */

#include "query_helpers.h"
#include <sstream>
#include <stdexcept>

namespace common::db {

// ============================================================================
// JSON Value Extraction
// ============================================================================

int getInt(const Json::Value& json, const std::string& field, int defaultValue) {
    if (!json.isMember(field) || json[field].isNull()) return defaultValue;
    const auto& v = json[field];
    if (v.isInt()) return v.asInt();
    if (v.isUInt()) return static_cast<int>(v.asUInt());
    if (v.isString()) {
        try { return std::stoi(v.asString()); }
        catch (...) { return defaultValue; }
    }
    if (v.isDouble()) return static_cast<int>(v.asDouble());
    return defaultValue;
}

bool getBool(const Json::Value& json, const std::string& field, bool defaultValue) {
    if (!json.isMember(field) || json[field].isNull()) return defaultValue;
    const auto& v = json[field];
    if (v.isBool()) return v.asBool();
    if (v.isString()) {
        const auto& s = v.asString();
        return s == "1" || s == "true" || s == "TRUE" || s == "t" || s == "T";
    }
    if (v.isInt()) return v.asInt() != 0;
    if (v.isUInt()) return v.asUInt() != 0;
    return defaultValue;
}

int scalarToInt(const Json::Value& value, int defaultValue) {
    if (value.isNull()) return defaultValue;
    if (value.isInt()) return value.asInt();
    if (value.isUInt()) return static_cast<int>(value.asUInt());
    if (value.isString()) {
        const auto& str = value.asString();
        if (str.empty()) return defaultValue;
        try { return std::stoi(str); }
        catch (...) { return defaultValue; }
    }
    if (value.isDouble()) return static_cast<int>(value.asDouble());
    return defaultValue;
}

// ============================================================================
// SQL Expression Generation
// ============================================================================

std::string currentTimestamp(const std::string& dbType) {
    return (dbType == "oracle") ? "SYSTIMESTAMP" : "NOW()";
}

std::string currentTimestampFormatted(const std::string& dbType) {
    if (dbType == "oracle") {
        return "TO_CHAR(SYSTIMESTAMP, 'YYYY-MM-DD HH24:MI:SS')";
    }
    return "TO_CHAR(NOW(), 'YYYY-MM-DD HH24:MI:SS')";
}

std::string boolLiteral(const std::string& dbType, bool value) {
    if (dbType == "oracle") {
        return value ? "1" : "0";
    }
    return value ? "TRUE" : "FALSE";
}

std::string paginationClause(const std::string& dbType, int limit, int offset) {
    std::ostringstream ss;
    if (dbType == "oracle") {
        ss << " OFFSET " << offset << " ROWS FETCH NEXT " << limit << " ROWS ONLY";
    } else {
        ss << " LIMIT " << limit << " OFFSET " << offset;
    }
    return ss.str();
}

std::string limitClause(const std::string& dbType, int limit) {
    std::ostringstream ss;
    if (dbType == "oracle") {
        ss << " FETCH FIRST " << limit << " ROWS ONLY";
    } else {
        ss << " LIMIT " << limit;
    }
    return ss.str();
}

std::string ilikeCond(const std::string& dbType, const std::string& column,
                      const std::string& paramPlaceholder) {
    if (dbType == "oracle") {
        return "UPPER(" + column + ") LIKE UPPER(" + paramPlaceholder + ")";
    }
    return column + " ILIKE " + paramPlaceholder;
}

std::string nonEmptyFilter(const std::string& dbType, const std::string& column) {
    if (dbType == "oracle") {
        return column + " IS NOT NULL";
    }
    return column + " IS NOT NULL AND " + column + " != ''";
}

std::string hexPrefix(const std::string& dbType) {
    return (dbType == "oracle") ? "\\\\x" : "\\x";
}

std::string intervalHours(const std::string& dbType, int hours) {
    std::ostringstream ss;
    if (dbType == "oracle") {
        ss << "INTERVAL '" << hours << "' HOUR";
    } else {
        ss << "INTERVAL '" << hours << " hours'";
    }
    return ss.str();
}

} // namespace common::db
