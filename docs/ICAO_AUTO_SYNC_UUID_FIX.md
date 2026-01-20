# ICAO Auto Sync - UUID Compatibility Fix

**Date**: 2026-01-19
**Issue**: Database schema and code type mismatch
**Status**: ✅ Fixed

---

## Problem

### Database Error During Migration

```
ERROR:  foreign key constraint "icao_pkd_versions_import_upload_id_fkey" cannot be implemented
DETAIL:  Key columns "import_upload_id" and "id" are of incompatible types: integer and uuid.
```

### Root Cause

The `uploaded_file` table uses **UUID** for its primary key `id`, but the initial ICAO migration script referenced it as **INTEGER**:

```sql
-- WRONG (Initial Implementation)
CREATE TABLE icao_pkd_versions (
    ...
    import_upload_id INTEGER REFERENCES uploaded_file(id),  -- ❌ Type mismatch
    ...
);
```

### Impact

- Database migration failed
- Foreign key constraint could not be created
- `linkToUpload()` functionality broken
- ICAO version tracking incomplete

---

## Solution

### 1. Database Schema Fix

**File**: `docker/init-scripts/004_create_icao_versions_table.sql`

```sql
-- FIXED
CREATE TABLE icao_pkd_versions (
    ...
    import_upload_id UUID REFERENCES uploaded_file(id),  -- ✅ Correct type
    ...
);
```

**Commit**: 0d1480e

### 2. Domain Model Update

**File**: `services/pkd-management/src/domain/models/icao_version.h`

```cpp
// BEFORE
struct IcaoVersion {
    ...
    std::optional<int> importUploadId;  // ❌ Wrong type
    ...
};

// AFTER
struct IcaoVersion {
    ...
    std::optional<std::string> importUploadId;  // ✅ UUID as string
    ...
};
```

### 3. Repository Interface Update

**File**: `services/pkd-management/src/repositories/icao_version_repository.h`

```cpp
// BEFORE
bool linkToUpload(const std::string& fileName, int uploadId,
                 int certificateCount);  // ❌ int uploadId

// AFTER
bool linkToUpload(const std::string& fileName, const std::string& uploadId,
                 int certificateCount);  // ✅ string uploadId (UUID)
```

### 4. Repository Implementation Update

**File**: `services/pkd-management/src/repositories/icao_version_repository.cpp`

```cpp
// BEFORE
bool IcaoVersionRepository::linkToUpload(const std::string& fileName,
                                        int uploadId,
                                        int certificateCount) {
    ...
    const char* paramValues[3] = {
        std::to_string(uploadId).c_str(),  // ❌ Convert int to string
        ...
    };
}

// AFTER
bool IcaoVersionRepository::linkToUpload(const std::string& fileName,
                                        const std::string& uploadId,
                                        int certificateCount) {
    ...
    const char* paramValues[3] = {
        uploadId.c_str(),  // ✅ UUID string directly
        ...
    };
}
```

```cpp
// BEFORE (rowToVersion method)
version.importUploadId = getOptionalInt(res, row, 10);  // ❌ Parse as int

// AFTER
version.importUploadId = getOptionalString(res, row, 10);  // ✅ Parse as string (UUID)
```

**Commit**: c0af18d

---

## Verification

### 1. Database Schema

```bash
docker compose -f docker/docker-compose.yaml exec postgres psql -U pkd -d localpkd -c "\d icao_pkd_versions"
```

**Expected Output**:
```
 import_upload_id     | uuid                        |           |          |
```

✅ **Confirmed**: Column type is UUID

### 2. Foreign Key Constraint

```bash
docker compose -f docker/docker-compose.yaml exec postgres psql -U pkd -d localpkd -c "\d icao_pkd_versions" | grep -A2 "Foreign-key"
```

**Expected Output**:
```
Foreign-key constraints:
    "icao_pkd_versions_import_upload_id_fkey" FOREIGN KEY (import_upload_id) REFERENCES uploaded_file(id)
```

✅ **Confirmed**: Foreign key created successfully

### 3. Code Compilation

```bash
docker build -t icao-pkd-management:test-v1.7.0-uuid -f services/pkd-management/Dockerfile .
```

**Expected**: No compilation errors related to type mismatches

---

## UUID Format

### PostgreSQL UUID Type

- **Storage**: 16 bytes (128 bits)
- **Display**: `a0eebc99-9c0b-4ef8-bb6d-6bb9bd380a11` (36 characters with hyphens)
- **Functions**: `uuid_generate_v4()` for random UUIDs

### C++ String Representation

- **Type**: `std::string`
- **Format**: Same as display format (36 chars)
- **Example**: `"550e8400-e29b-41d4-a716-446655440000"`

### PostgreSQL `libpq` Handling

```cpp
// Reading UUID from query result
std::string uuid = PQgetvalue(res, row, col);
// Returns: "550e8400-e29b-41d4-a716-446655440000"

// Passing UUID as parameter
const char* paramValues[] = { uuid.c_str() };
PQexecParams(conn, query, 1, nullptr, paramValues, nullptr, nullptr, 0);
```

---

## Testing Checklist

### Database Migration
- [x] Migration script runs without errors
- [x] `icao_pkd_versions` table created
- [x] `import_upload_id` column is UUID type
- [x] Foreign key constraint created successfully
- [x] `uploaded_file.icao_version_id` column created

### Code Compilation
- [x] Domain model compiles
- [x] Repository interface compiles
- [x] Repository implementation compiles
- [x] Handler compiles (no changes needed)
- [x] Docker image builds successfully

### Runtime Testing (Pending)
- [ ] `linkToUpload()` accepts UUID string
- [ ] PostgreSQL query executes successfully
- [ ] Foreign key relationship works
- [ ] JSON API returns UUID correctly

---

## Lessons Learned

### 1. Always Check Existing Schema First

Before creating foreign keys, verify the referenced column's data type:

```bash
psql -c "\d uploaded_file" | grep "^  id "
```

### 2. UUID vs Integer Trade-offs

| Aspect | UUID | Integer (Serial) |
|--------|------|------------------|
| **Size** | 16 bytes | 4 bytes (int), 8 bytes (bigint) |
| **Collision** | Virtually impossible | Requires sequence coordination |
| **Readability** | Hard to remember | Easy to use (1, 2, 3...) |
| **Distribution** | Excellent for sharding | Sequential, predictable |
| **Performance** | Slightly slower index | Faster index operations |

### 3. Type Safety in Foreign Keys

PostgreSQL enforces strict type matching for foreign keys. Mixed types (even if castable) are rejected:

```sql
-- ❌ Rejected
uuid_column REFERENCES integer_pk_column

-- ✅ Accepted
uuid_column REFERENCES uuid_pk_column
integer_column REFERENCES integer_pk_column
```

### 4. C++ String vs Numeric Types

For PostgreSQL `libpq`, UUID is always handled as **string**:

```cpp
// ✅ Correct
std::string uuid = "550e8400-e29b-41d4-a716-446655440000";
const char* param = uuid.c_str();

// ❌ Wrong (no UUID native type in libpq C API)
// There's no "UUID type" in libpq - always use string representation
```

---

## Related Issues

### Issue: Handler JSON Serialization

**Status**: ✅ No changes needed

The handler already used `Json::Value` which accepts strings directly:

```cpp
// Already correct
if (version.importUploadId.has_value()) {
    json["import_upload_id"] = version.importUploadId.value();
    // Works for both int (before) and string (after)
}
```

### Issue: Service Layer

**Status**: ✅ No changes needed

The service layer (`IcaoSyncService`) doesn't directly manipulate `importUploadId`, so no changes required.

---

## Future Considerations

### 1. Consistent UUID Usage

Consider using UUID for all new primary keys to maintain consistency:

```sql
-- Recommendation for new tables
CREATE TABLE new_feature (
    id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
    ...
);
```

### 2. UUID Extension

Ensure `uuid-ossp` extension is enabled in PostgreSQL:

```sql
CREATE EXTENSION IF NOT EXISTS "uuid-ossp";
```

**Status**: ✅ Already enabled in init script `001_create_certificate_tables.sql`

### 3. Type Documentation

Add type comments in code for clarity:

```cpp
struct IcaoVersion {
    std::optional<std::string> importUploadId;  // UUID string format (36 chars)
    ...
};
```

---

## Commits

| Commit | Description | Files Changed |
|--------|-------------|---------------|
| 0d1480e | Database schema: INTEGER → UUID | `004_create_icao_versions_table.sql` |
| c0af18d | Code: int → string for UUID | `icao_version.h`, `icao_version_repository.h/cpp` |

**Branch**: feature/icao-auto-sync-tier1

---

## Summary

**Problem**: Type mismatch between ICAO migration script (INTEGER) and existing schema (UUID)

**Solution**: Updated both database schema and C++ code to use UUID (represented as string in C++)

**Result**:
- ✅ Database migration successful
- ✅ Foreign key constraint created
- ✅ Code compiles without errors
- ✅ Type consistency maintained

**Impact**: Zero functional changes, pure type compatibility fix

---

**Document Created**: 2026-01-19
**Status**: Issue Resolved ✅
