# LDAP Connection Failure Fix - Data Consistency Protection

**Date**: 2026-01-23
**Version**: v2.0.0
**Issue**: Silent LDAP upload failure causing data inconsistency (PostgreSQL has data, LDAP doesn't)
**Status**: ✅ Fixed

---

## Problem Description

### Root Cause

During Collection 002 Master List upload in AUTO mode, if `getLdapWriteConnection()` failed:

1. **OLD BEHAVIOR** (❌ Incorrect):
   ```cpp
   LDAP* ld = getLdapWriteConnection();
   if (!ld) {
       spdlog::warn("LDAP write connection failed - will only save to DB");
       // ❌ Processing continues with ld=nullptr
   }
   ```

2. **CONSEQUENCE**:
   - 536 CSCA certificates saved to PostgreSQL ✅
   - 0 certificates saved to LDAP ❌
   - All certificates have `stored_in_ldap = false`
   - Upload status: `COMPLETED` (misleading)
   - **Data inconsistency**: DB and LDAP out of sync

### Architectural Inconsistency

**MANUAL Mode** (processing_strategy.cpp:382-386):
```cpp
LDAP* ld = getLdapWriteConnection();
if (!ld) {
    throw std::runtime_error("LDAP write connection failed");
    // ✅ CORRECT: Stops processing, prevents inconsistency
}
```

**AUTO Mode** (main.cpp:3069-3072, 3641-3644, 4244-4246, 5394-5396):
```cpp
LDAP* ld = getLdapWriteConnection();
if (!ld) {
    spdlog::warn("LDAP write connection failed - will only save to DB");
    // ❌ WRONG: Continues processing, creates inconsistency
}
```

---

## Solution

### Design Principle

**Fail Fast, Fail Loud** - Data consistency is more important than partial processing.

**NEW BEHAVIOR** (✅ Correct):
```cpp
LDAP* ld = nullptr;
if (processingMode == "AUTO") {
    ld = getLdapWriteConnection();
    if (!ld) {
        spdlog::error("CRITICAL: LDAP write connection failed in AUTO mode for upload {}", uploadId);
        spdlog::error("Cannot proceed - data consistency requires both DB and LDAP storage");

        // Update upload status to FAILED
        const char* failQuery = "UPDATE uploaded_file SET status = 'FAILED', "
                               "error_message = 'LDAP connection failure - cannot ensure data consistency', "
                               "updated_at = NOW() WHERE id = $1";
        const char* failParams[1] = {uploadId.c_str()};
        PGresult* failRes = PQexecParams(conn, failQuery, 1, nullptr, failParams,
                                        nullptr, nullptr, 0);
        PQclear(failRes);

        // Send failure progress
        ProgressManager::getInstance().sendProgress(
            ProcessingProgress::create(uploadId, ProcessingStage::FAILED,
                0, 0, "LDAP 연결 실패", "데이터 일관성을 보장할 수 없어 처리를 중단했습니다."));

        PQfinish(conn);
        return;  // ✅ Exit early, prevent partial processing
    }
    spdlog::info("LDAP write connection established successfully for AUTO mode");
}
```

### Files Modified

| File | Lines Changed | Purpose |
|------|---------------|---------|
| `services/pkd-management/src/main.cpp` | 4 locations | LDAP connection failure handling |

**4 Locations Fixed**:
1. Line ~3067: LDIF AUTO mode processing
2. Line ~3638: Master List async processing (first handler)
3. Line ~4241: Master List async processing (second handler)
4. Line ~5392: Master List async processing via Strategy (third handler)

---

## Benefits

### 1. Data Consistency Guarantee

✅ **Before Fix**: PostgreSQL ≠ LDAP (536 vs 0 certificates)
✅ **After Fix**: Processing stops if LDAP unavailable, prevents inconsistency

### 2. Clear Error Messaging

**Frontend**: User sees clear error message: "LDAP 연결 실패 - 데이터 일관성을 보장할 수 없어 처리를 중단했습니다."

**Logs**:
```
[error] CRITICAL: LDAP write connection failed in AUTO mode for upload 123e4567-e89b-12d3-a456-426614174000
[error] Cannot proceed - data consistency requires both DB and LDAP storage
```

### 3. Upload Status Accuracy

**Before**: Upload marked `COMPLETED` even though LDAP upload failed
**After**: Upload marked `FAILED` with clear error message

### 4. Operational Visibility

Administrators can immediately identify LDAP connection issues instead of discovering data inconsistency later.

---

## Testing Strategy

### Test Case 1: Normal Operation (LDAP Available)

**Setup**: LDAP server running normally

**Expected**:
1. Upload starts
2. LDAP connection succeeds
3. Certificates saved to both DB and LDAP
4. Upload status: `COMPLETED`
5. `stored_in_ldap = true` for all certificates

**Verification**:
```bash
# Check certificate count
psql -U pkd -d pkd -c "SELECT COUNT(*) FROM certificate WHERE stored_in_ldap = true;"

# Check LDAP entries
ldapsearch -x -H ldap://localhost:389 -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" -w admin \
  -b "dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" "(objectClass=pkdDownload)" | grep "numEntries"
```

### Test Case 2: LDAP Connection Failure (Server Down)

**Setup**: Stop LDAP server before upload
```bash
docker compose stop openldap1
```

**Expected**:
1. Upload starts
2. LDAP connection fails
3. **Processing stops immediately**
4. Upload status: `FAILED`
5. Error message: "LDAP connection failure - cannot ensure data consistency"
6. **No certificates saved to DB** (no partial data)

**Verification**:
```bash
# Check upload status
curl http://localhost:8080/api/upload/history

# Check certificate count (should be 0 for this upload)
psql -U pkd -d pkd -c "SELECT COUNT(*) FROM certificate WHERE upload_id = 'FAILED_UPLOAD_ID';"
```

### Test Case 3: LDAP Authentication Failure

**Setup**: Incorrect LDAP bind password in environment
```bash
# docker-compose.yaml
environment:
  - LDAP_BIND_PASSWORD=wrong_password
```

**Expected**: Same as Test Case 2 (immediate failure)

### Test Case 4: MANUAL Mode (Unchanged Behavior)

**Setup**: Upload in MANUAL mode

**Expected**:
1. Stage 1 (Parse): Succeeds, saves to temp file
2. Stage 2 (Validate): **Requires LDAP connection**
3. If LDAP fails at Stage 2: Upload fails with exception (existing behavior)

---

## LDAP Connection Failure Scenarios

### Scenario 1: Network Unreachable

**Cause**: `ldap_initialize()` fails
```cpp
int rc = ldap_initialize(&ld, uri.c_str());
if (rc != LDAP_SUCCESS) {
    spdlog::error("LDAP write connection initialize failed: {}", ldap_err2string(rc));
    return nullptr;  // ← Triggers fail-fast logic
}
```

**Example**: LDAP server down, wrong hostname, network partition

### Scenario 2: Authentication Failure

**Cause**: `ldap_sasl_bind_s()` fails
```cpp
rc = ldap_sasl_bind_s(ld, appConfig.ldapBindDn.c_str(), LDAP_SASL_SIMPLE, &cred, ...);
if (rc != LDAP_SUCCESS) {
    spdlog::error("LDAP write connection bind failed: {}", ldap_err2string(rc));
    ldap_unbind_ext_s(ld, nullptr, nullptr);
    return nullptr;  // ← Triggers fail-fast logic
}
```

**Example**: Wrong password, invalid bind DN, insufficient permissions

### Scenario 3: Timeout

**Cause**: Network latency, slow LDAP server

**Mitigation**: LDAP C API has default timeout (configurable via `LDAP_OPT_NETWORK_TIMEOUT`)

---

## Migration Path for Existing Inconsistent Data

### Step 1: Identify Affected Uploads

```sql
-- Find uploads with certificates but no LDAP storage
SELECT
    uf.id AS upload_id,
    uf.file_name,
    uf.status,
    uf.uploaded_at,
    COUNT(c.id) AS db_certificates,
    SUM(CASE WHEN c.stored_in_ldap THEN 1 ELSE 0 END) AS ldap_certificates,
    COUNT(c.id) - SUM(CASE WHEN c.stored_in_ldap THEN 1 ELSE 0 END) AS missing_in_ldap
FROM uploaded_file uf
LEFT JOIN certificate c ON c.upload_id = uf.id
WHERE uf.status = 'COMPLETED'
GROUP BY uf.id, uf.file_name, uf.status, uf.uploaded_at
HAVING COUNT(c.id) > 0 AND SUM(CASE WHEN c.stored_in_ldap THEN 1 ELSE 0 END) = 0
ORDER BY uf.uploaded_at DESC;
```

### Step 2: Re-upload Affected Files

**Option A**: Delete and re-upload
```sql
-- Delete certificates from affected upload
DELETE FROM certificate WHERE upload_id = 'AFFECTED_UPLOAD_ID';

-- Delete upload record
DELETE FROM uploaded_file WHERE id = 'AFFECTED_UPLOAD_ID';

-- Re-upload the file via frontend
```

**Option B**: Trigger LDAP sync from DB (Manual)
```bash
# TODO: Implement /api/admin/sync-db-to-ldap endpoint
# This would read certificates from DB and upload to LDAP
curl -X POST http://localhost:8080/api/admin/sync-db-to-ldap \
  -H "Content-Type: application/json" \
  -d '{"uploadId": "AFFECTED_UPLOAD_ID"}'
```

### Step 3: Verify Consistency

```bash
# Check LDAP entries
ldapsearch -x -H ldap://localhost:389 -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" -w admin \
  -b "dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" "(objectClass=pkdDownload)" | grep "numEntries"

# Compare with DB count
psql -U pkd -d pkd -c "SELECT COUNT(*) FROM certificate WHERE stored_in_ldap = true;"
```

---

## Future Enhancements

### 1. Transaction-like Behavior

Implement rollback if LDAP save fails after DB save:

```cpp
// Pseudocode
try {
    saveToDatabase(cert);
    saveToLdap(cert);
} catch (LdapException& e) {
    rollbackDatabase(cert);  // Delete from DB
    throw;
}
```

### 2. Retry Mechanism

Add exponential backoff for transient LDAP failures:

```cpp
LDAP* getLdapWriteConnectionWithRetry(int maxRetries = 3) {
    for (int i = 0; i < maxRetries; i++) {
        LDAP* ld = getLdapWriteConnection();
        if (ld) return ld;

        spdlog::warn("LDAP connection attempt {} failed, retrying...", i + 1);
        std::this_thread::sleep_for(std::chrono::seconds(1 << i));  // 1s, 2s, 4s
    }
    return nullptr;
}
```

### 3. Admin Sync Endpoint

Create API to sync certificates from DB to LDAP:

```cpp
// POST /api/admin/sync-db-to-ldap
app.registerHandler("/api/admin/sync-db-to-ldap",
    [](const HttpRequestPtr& req, ...) {
        auto json = req->getJsonObject();
        std::string uploadId = (*json)["uploadId"].asString();

        // Read certificates from DB where stored_in_ldap = false
        // Upload each to LDAP
        // Update stored_in_ldap = true, ldap_dn = ...
    },
    {Post});
```

### 4. Health Check Enhancement

Add LDAP connection check to health endpoint:

```json
{
  "status": "UP",
  "database": {
    "status": "UP",
    "version": "PostgreSQL 15.3"
  },
  "ldap": {
    "status": "UP",
    "writeHost": "openldap1:389",
    "readHost": "haproxy:389",
    "lastConnectionTest": "2026-01-23T10:30:00Z"
  }
}
```

### 5. Monitoring & Alerting

Add metrics for:
- LDAP connection failures per hour
- Data inconsistency alerts (DB count ≠ LDAP count)
- Retry success rate

---

## Lessons Learned

### 1. Silent Failures Are Dangerous

**Principle**: **Never tolerate silent failures for critical operations**

**Before**: `spdlog::warn("LDAP write connection failed - will only save to DB")`
**After**: Throw exception or fail the entire operation

### 2. Consistency > Availability

**Tradeoff**: In distributed systems (DB + LDAP), consistency is more important than availability.

**Decision**: Better to reject an upload than to create data inconsistency.

### 3. Fail Fast Philosophy

**Early Exit**: Check all prerequisites at the start, fail immediately if any are missing.

**Benefits**:
- No wasted processing
- Clear error messages
- No partial state cleanup needed

### 4. Mode-Specific Behavior Must Be Consistent

**Issue**: MANUAL mode threw exception on LDAP failure, but AUTO mode didn't.

**Fix**: Make both modes behave the same way for critical failures.

### 5. Testing Edge Cases

**Must Test**:
- ✅ Happy path (everything works)
- ✅ LDAP server down
- ✅ LDAP authentication failure
- ✅ Network timeout
- ✅ Partial failure (DB succeeds, LDAP fails)

---

## References

- **ICAO Doc 9303**: PKI for MRTDs (Master List, CSCA requirements)
- **RFC 4511**: LDAP Protocol (connection, bind operations)
- **PostgreSQL Documentation**: Transaction management, rollback strategies
- **CLAUDE.md**: System architecture, LDAP DIT structure

---

## Commit Information

**Commit**: [Pending]
**Branch**: main
**Files Modified**: 1 (`services/pkd-management/src/main.cpp`)
**Lines Changed**: ~80 lines (4 locations × ~20 lines each)

---

## Deployment Checklist

- [ ] Code review approved
- [ ] Unit tests passed (if applicable)
- [ ] Integration tests passed
  - [ ] LDAP connection success
  - [ ] LDAP connection failure (server down)
  - [ ] LDAP authentication failure
- [ ] Docker build successful
- [ ] Deployed to development environment
- [ ] Manual testing completed
- [ ] Deployed to production (Luckfox)
- [ ] Monitoring alerts configured
- [ ] Documentation updated (CLAUDE.md)

---

**Implemented by**: Claude Code (Anthropic)
**Date**: 2026-01-23
**Version**: v2.0.0
