# Phase 4.4: Enhanced Audit Logging - Implementation Guide

**Version**: v2.1.0 (Phase 4.4)
**Status**: 🚧 Implementation Guide (Code Infrastructure Complete)
**Priority**: Low (Operational Visibility)

---

## Overview

Expand audit logging beyond authentication events to cover all sensitive business operations, providing comprehensive operational visibility and compliance audit trails.

---

## Objectives

**Current State**: Authentication events logged (login, logout, token operations)

**Enhancement Goals**:
1. Log all file upload operations (LDIF, Master List)
2. Log all certificate export operations (single, ZIP)
3. Log upload deletion operations
4. Log PA verification requests
5. Provide operation-level performance metrics (duration tracking)
6. Enable forensic analysis and compliance reporting

---

## Database Schema

### New Table: `operation_audit_log`

**File**: `docker/init-scripts/05-operation-audit-log.sql` ✅ Created

**Columns**:
- `id` UUID PRIMARY KEY
- `user_id` UUID REFERENCES users(id)
- `username` VARCHAR(255)
- `operation_type` VARCHAR(50) - FILE_UPLOAD, CERT_EXPORT, UPLOAD_DELETE, PA_VERIFY, SYNC_TRIGGER
- `operation_subtype` VARCHAR(50) - LDIF, MASTER_LIST, SINGLE_CERT, COUNTRY_ZIP, etc.
- `resource_id` VARCHAR(255) - Upload ID, Certificate DN, Verification ID
- `resource_type` VARCHAR(50) - UPLOADED_FILE, CERTIFICATE, PA_VERIFICATION
- `ip_address` VARCHAR(45)
- `user_agent` TEXT
- `request_method` VARCHAR(10) - GET, POST, PUT, DELETE
- `request_path` TEXT - /api/upload/ldif, /api/certificates/export/file
- `success` BOOLEAN
- `status_code` INTEGER - HTTP status (200, 400, 500)
- `error_message` TEXT
- `metadata` JSONB - Operation-specific data
- `duration_ms` INTEGER - Operation duration
- `created_at` TIMESTAMP

**Indexes**:
- idx_op_audit_user_id
- idx_op_audit_created_at (DESC)
- idx_op_audit_operation_type
- idx_op_audit_success
- idx_op_audit_username
- idx_op_audit_resource_id
- idx_op_audit_ip_address
- idx_op_audit_metadata (GIN index for JSONB queries)

---

## Code Infrastructure

### Audit Logging Utility

**File**: `services/pkd-management/src/common/audit_log.h` ✅ Created

**Components**:

#### 1. `OperationType` Enum
```cpp
enum class OperationType {
    FILE_UPLOAD,
    CERT_EXPORT,
    UPLOAD_DELETE,
    PA_VERIFY,
    SYNC_TRIGGER
};
```

#### 2. `AuditLogEntry` Struct
```cpp
struct AuditLogEntry {
    std::optional<std::string> userId;
    std::optional<std::string> username;
    OperationType operationType;
    std::optional<std::string> operationSubtype;
    std::optional<std::string> resourceId;
    std::optional<std::string> resourceType;
    std::optional<std::string> ipAddress;
    std::optional<std::string> userAgent;
    std::optional<std::string> requestMethod;
    std::optional<std::string> requestPath;
    bool success = true;
    std::optional<int> statusCode;
    std::optional<std::string> errorMessage;
    std::optional<Json::Value> metadata;
    std::optional<int> durationMs;
};
```

#### 3. `AuditTimer` Class (RAII)
```cpp
class AuditTimer {
public:
    AuditTimer() : startTime_(std::chrono::steady_clock::now()) {}
    int getDurationMs() const;
};
```

#### 4. Helper Functions
- `logOperation(PGconn* conn, const AuditLogEntry& entry)` - Insert audit log
- `getUserInfoFromSession(const drogon::SessionPtr& session)` - Extract user ID/username
- `getClientIpAddress(const drogon::HttpRequestPtr& req)` - Extract IP (X-Forwarded-For or peer)

---

## Implementation Points

### 1. File Upload Operations ⏳

**Location**: `services/pkd-management/src/main.cpp`

**Endpoints**:
- POST `/api/upload/ldif` (line 4615-4800)
- POST `/api/upload/masterlist` (line 4900-5100)

**Integration Pattern**:
```cpp
#include "common/audit_log.h"

// POST /api/upload/ldif handler
common::AuditTimer timer;  // Start timing

// ... existing upload logic ...

// Extract user info
auto session = req->getSession();
auto [userId, username] = common::getUserInfoFromSession(session);

// Build metadata
Json::Value metadata;
metadata["fileName"] = fileName;
metadata["fileSize"] = fileSize;
metadata["processingMode"] = processingMode;
metadata["totalEntries"] = totalEntries;  // from parsing result
metadata["fileHash"] = fileHash.substr(0, 16);  // First 16 chars

// Log operation
common::AuditLogEntry entry;
entry.userId = userId;
entry.username = username;
entry.operationType = common::OperationType::FILE_UPLOAD;
entry.operationSubtype = "LDIF";  // or "MASTER_LIST"
entry.resourceId = uploadId;
entry.resourceType = "UPLOADED_FILE";
entry.ipAddress = common::getClientIpAddress(req);
entry.userAgent = req->getHeader("User-Agent");
entry.requestMethod = "POST";
entry.requestPath = "/api/upload/ldif";
entry.success = true;  // or false if failed
entry.statusCode = 200;  // or error code
entry.metadata = metadata;
entry.durationMs = timer.getDurationMs();

PGconn* conn = ...; // Get DB connection
common::logOperation(conn, entry);
```

**Metadata Fields for FILE_UPLOAD**:
- `fileName` - Original filename
- `fileSize` - File size in bytes
- `processingMode` - AUTO or MANUAL
- `totalEntries` - Number of LDIF entries or certificates in ML
- `fileHash` - SHA-256 hash (first 16 chars)
- `validationStats` - CSCA/DSC counts, validation results (optional)

---

### 2. Certificate Export Operations ⏳

**Location**: `services/pkd-management/src/main.cpp`

**Endpoints**:
- GET `/api/certificates/export/file` (line 5820-5900) - Single certificate
- GET `/api/certificates/export/country` (line 5878-6100) - Country ZIP

**Integration Pattern**:
```cpp
common::AuditTimer timer;

// ... export logic ...

Json::Value metadata;
metadata["country"] = country;
metadata["certType"] = certType;
metadata["format"] = format;  // "DER" or "PEM"
metadata["certificateCount"] = certificateCount;  // for ZIP export

common::AuditLogEntry entry;
entry.userId = userId;
entry.username = username;
entry.operationType = common::OperationType::CERT_EXPORT;
entry.operationSubtype = (certificateCount > 1) ? "COUNTRY_ZIP" : "SINGLE_CERT";
entry.resourceId = dn;  // or country code
entry.resourceType = "CERTIFICATE";
entry.ipAddress = common::getClientIpAddress(req);
entry.userAgent = req->getHeader("User-Agent");
entry.requestMethod = "GET";
entry.requestPath = req->path();
entry.success = true;
entry.statusCode = 200;
entry.metadata = metadata;
entry.durationMs = timer.getDurationMs();

common::logOperation(conn, entry);
```

**Metadata Fields for CERT_EXPORT**:
- `country` - Country code (for ZIP export)
- `certType` - CSCA, DSC, DSC_NC, CRL, ALL
- `format` - DER or PEM
- `certificateCount` - Number of certificates exported
- `zipSize` - ZIP file size in bytes (for COUNTRY_ZIP)

---

### 3. Upload Deletion Operations ⏳

**Location**: `services/pkd-management/src/main.cpp`

**Endpoint**: DELETE `/api/upload/{uploadId}` (line 6300-6400)

**Integration Pattern**:
```cpp
common::AuditTimer timer;

// ... deletion logic ...

Json::Value metadata;
metadata["uploadId"] = uploadId;
metadata["fileName"] = fileName;  // retrieved from DB before deletion
metadata["deletedRecords"] = Json::Value();
metadata["deletedRecords"]["certificates"] = certCount;
metadata["deletedRecords"]["crls"] = crlCount;
metadata["deletedRecords"]["masterLists"] = mlCount;

common::AuditLogEntry entry;
entry.userId = userId;
entry.username = username;
entry.operationType = common::OperationType::UPLOAD_DELETE;
entry.operationSubtype = "FAILED_UPLOAD";
entry.resourceId = uploadId;
entry.resourceType = "UPLOADED_FILE";
entry.ipAddress = common::getClientIpAddress(req);
entry.userAgent = req->getHeader("User-Agent");
entry.requestMethod = "DELETE";
entry.requestPath = req->path();
entry.success = true;
entry.statusCode = 200;
entry.metadata = metadata;
entry.durationMs = timer.getDurationMs();

common::logOperation(conn, entry);
```

**Metadata Fields for UPLOAD_DELETE**:
- `uploadId` - UUID of deleted upload
- `fileName` - Original filename
- `deletedRecords` - JSON object with counts (certificates, crls, masterLists)

---

### 4. PA Verification Operations ⏳

**Location**: `services/pa-service/src/main.cpp`

**Endpoint**: POST `/api/pa/verify`

**Integration Pattern**:
```cpp
common::AuditTimer timer;

// ... PA verification logic ...

Json::Value metadata;
metadata["issuingCountry"] = verificationResult.issuingCountry;
metadata["documentNumber"] = verificationResult.documentNumber;
metadata["verificationSteps"] = Json::Value(Json::arrayValue);
// Add verification steps (SOD, DG1, DG2, Trust Chain, etc.)

common::AuditLogEntry entry;
entry.userId = userId;
entry.username = username;
entry.operationType = common::OperationType::PA_VERIFY;
entry.operationSubtype = "SOD";  // or "FULL_VERIFICATION"
entry.resourceId = verificationId;  // UUID from pa_verification_history
entry.resourceType = "PA_VERIFICATION";
entry.ipAddress = common::getClientIpAddress(req);
entry.userAgent = req->getHeader("User-Agent");
entry.requestMethod = "POST";
entry.requestPath = "/api/pa/verify";
entry.success = verificationResult.isValid;
entry.statusCode = 200;
entry.metadata = metadata;
entry.durationMs = timer.getDurationMs();

common::logOperation(conn, entry);
```

**Metadata Fields for PA_VERIFY**:
- `issuingCountry` - Country code from MRZ
- `documentNumber` - Document number from MRZ
- `verificationSteps` - Array of verification step results
- `sodSignatureValid` - Boolean
- `trustChainValid` - Boolean
- `dscSubject` - DSC certificate subject

---

## API Endpoints for Audit Logs

### GET /api/audit/operations

**Purpose**: Retrieve operation audit logs with filtering and pagination

**Query Parameters**:
- `limit` (default: 20, max: 100)
- `offset` (default: 0)
- `username` - Filter by username
- `operation_type` - FILE_UPLOAD, CERT_EXPORT, UPLOAD_DELETE, PA_VERIFY
- `success` - true/false
- `from_date` - ISO 8601 timestamp
- `to_date` - ISO 8601 timestamp

**Response**:
```json
{
  "total": 1234,
  "logs": [
    {
      "id": "uuid",
      "username": "admin",
      "operationType": "FILE_UPLOAD",
      "operationSubtype": "LDIF",
      "resourceId": "upload-uuid",
      "ipAddress": "192.168.100.11",
      "success": true,
      "statusCode": 200,
      "metadata": {
        "fileName": "icaopkd-001-complete-009668.ldif",
        "fileSize": 125829344,
        "processingMode": "AUTO",
        "totalEntries": 30081
      },
      "durationMs": 45320,
      "createdAt": "2026-01-23T14:30:00Z"
    }
  ]
}
```

### GET /api/audit/operations/stats

**Purpose**: Get operation statistics

**Response**:
```json
{
  "totalOperations": 5678,
  "successRate": 98.5,
  "operationBreakdown": {
    "FILE_UPLOAD": 1200,
    "CERT_EXPORT": 3400,
    "UPLOAD_DELETE": 50,
    "PA_VERIFY": 1028
  },
  "averageDuration": {
    "FILE_UPLOAD": 45320,
    "CERT_EXPORT": 1200,
    "UPLOAD_DELETE": 500,
    "PA_VERIFY": 3500
  },
  "topUsers": [
    {"username": "admin", "operations": 3200},
    {"username": "operator1", "operations": 1800}
  ]
}
```

---

## Frontend Integration ⏳

### New Page: Operation Audit Log

**File**: `frontend/src/pages/OperationAuditLog.tsx`

**Features**:
- Statistics cards (Total Operations, Success Rate, Avg Duration, Top User)
- Filter panel (username, operation type, date range, success/failure)
- Operation logs table with columns:
  - Timestamp
  - Username
  - Operation Type / Subtype
  - Resource
  - IP Address
  - Status (success badge)
  - Duration (ms)
  - Actions (View Details)
- Details modal showing full metadata JSON

**Sidebar Integration**:
```typescript
{
  title: 'Audit & Logs',
  items: [
    { label: 'Authentication Log', path: '/admin/audit-log' },  // existing
    { label: 'Operation Log', path: '/admin/operation-log' },   // new
  ]
}
```

---

## Sample Queries (Operational Insights)

### 1. Most Active Users
```sql
SELECT username, operation_type, COUNT(*) as count
FROM operation_audit_log
GROUP BY username, operation_type
ORDER BY count DESC
LIMIT 10;
```

### 2. Failed Operations in Last 24 Hours
```sql
SELECT *
FROM operation_audit_log
WHERE success = false
  AND created_at > NOW() - INTERVAL '24 hours'
ORDER BY created_at DESC;
```

### 3. Slowest Operations
```sql
SELECT operation_type, operation_subtype,
       AVG(duration_ms) as avg_ms,
       MAX(duration_ms) as max_ms,
       COUNT(*) as count
FROM operation_audit_log
WHERE duration_ms IS NOT NULL
GROUP BY operation_type, operation_subtype
ORDER BY avg_ms DESC;
```

### 4. Export Activity by Country
```sql
SELECT metadata->>'country' as country,
       COUNT(*) as export_count
FROM operation_audit_log
WHERE operation_type = 'CERT_EXPORT'
  AND metadata->>'country' IS NOT NULL
GROUP BY metadata->>'country'
ORDER BY export_count DESC;
```

### 5. PA Verification Success Rate by Country
```sql
SELECT metadata->>'issuingCountry' as country,
       COUNT(*) as total,
       SUM(CASE WHEN success THEN 1 ELSE 0 END) as successful,
       ROUND(100.0 * SUM(CASE WHEN success THEN 1 ELSE 0 END) / COUNT(*), 2) as success_rate
FROM operation_audit_log
WHERE operation_type = 'PA_VERIFY'
  AND metadata->>'issuingCountry' IS NOT NULL
GROUP BY metadata->>'issuingCountry'
ORDER BY total DESC;
```

---

## Implementation Checklist

### Database ✅
- [x] Create `operation_audit_log` table schema
- [x] Add indexes for performance
- [x] Test sample queries

### Backend Infrastructure ✅
- [x] Create `audit_log.h` utility header
- [x] Implement `AuditLogEntry` structure
- [x] Implement `AuditTimer` RAII class
- [x] Implement `logOperation()` function
- [x] Implement helper functions (getUserInfo, getClientIp)

### Backend Integration ⏳
- [ ] Add audit logging to FILE_UPLOAD handlers (LDIF, Master List)
- [ ] Add audit logging to CERT_EXPORT handlers (Single, ZIP)
- [ ] Add audit logging to UPLOAD_DELETE handler
- [ ] Add audit logging to PA_VERIFY handler (pa-service)
- [ ] Create GET /api/audit/operations endpoint
- [ ] Create GET /api/audit/operations/stats endpoint

### Frontend ⏳
- [ ] Create OperationAuditLog.tsx component
- [ ] Add route /admin/operation-log
- [ ] Update sidebar menu
- [ ] Create statistics cards component
- [ ] Create filter panel component
- [ ] Create operation logs table component
- [ ] Create details modal component

### Testing ⏳
- [ ] Test audit logging for successful operations
- [ ] Test audit logging for failed operations
- [ ] Test duration tracking
- [ ] Test metadata JSON serialization
- [ ] Test filter/pagination API
- [ ] Test statistics API
- [ ] Performance test with 10,000+ audit logs

---

## Performance Considerations

**Write Performance**:
- Audit logging should not block main operations
- Consider async logging for high-throughput endpoints
- Failed audit log insertion should not fail the operation (fail-open)

**Query Performance**:
- Indexes on `created_at DESC` for chronological queries
- GIN index on `metadata` for JSONB queries
- Composite index on `(username, created_at)` for user-specific queries
- Consider partitioning by date for long-term retention

**Data Retention**:
- Default: Keep all logs indefinitely
- Recommended: Archive logs older than 1 year to separate table
- Implement periodic cleanup job (optional)

---

## Compliance Benefits

**GDPR / Data Protection**:
- Track who accessed/exported certificate data
- Track who performed PA verifications (biometric data processing)
- Provide audit trail for data subject access requests

**SOC 2 / ISO 27001**:
- Demonstrate access control effectiveness
- Track privileged operations (uploads, deletions)
- Detect anomalous behavior patterns

**Forensic Analysis**:
- Investigate security incidents
- Track unauthorized data exfiltration attempts
- Analyze performance degradation issues

---

## Estimated Effort

- Database schema: ✅ 1 hour (Complete)
- Backend infrastructure: ✅ 2 hours (Complete)
- Backend integration: ⏳ 3-4 hours (Pending)
  - FILE_UPLOAD: 1 hour
  - CERT_EXPORT: 1 hour
  - UPLOAD_DELETE: 0.5 hours
  - PA_VERIFY: 1 hour
  - API endpoints: 0.5 hours
- Frontend implementation: ⏳ 2-3 hours (Pending)
- Testing: ⏳ 1 hour (Pending)

**Total**: 8-10 hours (3-4 hours remaining)

---

## Next Steps

1. **Backend Integration**: Add `common::logOperation()` calls to 4 operation handlers
2. **API Endpoints**: Create audit log retrieval and statistics endpoints
3. **Frontend**: Build OperationAuditLog component
4. **Testing**: Verify audit logging with integration tests
5. **Documentation**: Update API documentation with audit endpoints

---

**Status**: 🚧 **Infrastructure Complete, Integration Pending**
**Code Complete**: Database schema, Utility library
**Code Pending**: Handler integration, API endpoints, Frontend
