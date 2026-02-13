# Master List Upload 502 Error Troubleshooting

**Date**: 2026-01-31
**Version**: v2.2.1 (hotfix)
**Issue**: Master List 파일 업로드 시 502 Bad Gateway 에러

---

## Issue Summary

### Symptoms
- POST /api/upload/masterlist 요청 시 502 Bad Gateway 응답
- Frontend → API Gateway → pkd-management 경로에서 실패
- 파일 업로드 폼은 정상 작동하나 서버 처리 단계에서 크래시

### Impact
- Master List 파일 업로드 불가
- 전체 시스템 데이터 업로드 workflow 중단
- 사용자는 LDIF 파일 업로드만 가능

---

## Root Cause Analysis

### 1. Initial Investigation (nginx Logs)

**API Gateway Error Log** (`/var/log/nginx/error.log`):
```
2026/01/31 05:08:10 [error] upstream prematurely closed connection
while reading response header from upstream,
client: 172.18.0.11, server: localhost,
request: "POST /api/upload/masterlist HTTP/1.1",
upstream: "http://172.18.0.9:8081/api/upload/masterlist"
```

**Analysis**: Backend (pkd-management)가 응답을 보내기 전에 연결을 끊음 → 서비스 크래시 의심

### 2. Backend Service Logs

**pkd-management Container Logs**:
```
[2026-01-31 14:10:11.995] [debug] [UploadRepository] Finding upload by file hash: bd7bd35f78d36b27...
column number -1 is out of range 0..26

  _____ _____          ____    _                    _   ____  _  ______
 |_   _/ ____|   /\   / __ \  | |                  | | |  _ \| |/ /  _ \
   | || |       /  \ | |  | | | |     ___   ___ __ | | | |_) | ' /| | | |
   | || |      / /\ \| |  | | | |    / _ \ / __/ _` | | |  _ <|  < | | | |
  _| || |____ / ____ \ |__| | | |___| (_) | (_| (_| | | | |_) | . \| |_| |
 |_____\_____/_/    \_\____/  |______\___/ \___\__,_|_| |____/|_|\_\____/
```

**Analysis**: PostgreSQL result parsing 에러 → C++ 프로그램 크래시

### 3. Code Investigation

**File**: [services/pkd-management/src/repositories/upload_repository.cpp](../services/pkd-management/src/repositories/upload_repository.cpp)

**Function**: `UploadRepository::findByFileHash()` (Lines 279-314)

**Problem**:
```cpp
// Line 284-293: SQL Query
const char* query =
    "SELECT id, file_name, file_format, file_size, status, uploaded_by, "  // ❌ file_hash missing
    "error_message, processing_mode, total_entries, processed_entries, "
    "csca_count, dsc_count, dsc_nc_count, crl_count, mlsc_count, ml_count, "
    "upload_timestamp, completed_timestamp, "
    "COALESCE(validation_valid_count, 0), ..."
    "FROM uploaded_file WHERE file_hash = $1 LIMIT 1";

// Line 304: resultToUpload() parsing
Upload upload = resultToUpload(res, 0);
```

**File**: [services/pkd-management/src/repositories/upload_repository.cpp](../services/pkd-management/src/repositories/upload_repository.cpp)

**Function**: `UploadRepository::resultToUpload()` (Line 795)

```cpp
upload.fileHash = PQgetvalue(res, row, PQfnumber(res, "file_hash"));  // ❌ Column not in result set
```

**Error Flow**:
1. SQL 쿼리에서 `file_hash` 컬럼을 SELECT하지 않음
2. `PQfnumber(res, "file_hash")` 호출 → 컬럼이 없어서 **-1 반환**
3. `PQgetvalue(res, row, -1)` 호출 → **"column number -1 is out of range 0..26"** 에러
4. C++ exception 발생 → 프로세스 크래시
5. nginx가 "upstream prematurely closed connection" 에러 기록

---

## Solution

### Code Fix

**File**: `services/pkd-management/src/repositories/upload_repository.cpp`
**Line**: 285

**Before**:
```cpp
const char* query =
    "SELECT id, file_name, file_format, file_size, status, uploaded_by, "
    "error_message, processing_mode, total_entries, processed_entries, "
    ...
```

**After**:
```cpp
const char* query =
    "SELECT id, file_name, file_hash, file_format, file_size, status, uploaded_by, "
    "error_message, processing_mode, total_entries, processed_entries, "
    ...
```

**Change**: Added `file_hash` to SELECT clause (position 3, after `file_name`)

### Deployment

```bash
# Rebuild service with fix
cd docker
docker-compose build --no-cache pkd-management

# Restart service
docker-compose up -d --force-recreate pkd-management

# Verify health
curl http://localhost:8080/api/health
```

### Verification

**Test Upload**:
```bash
# Upload Master List file
curl -X POST http://localhost:8080/api/upload/masterlist \
  -F "file=@ICAO_ml_December2025.ml" \
  -F "mode=AUTO"
```

**Expected Result**:
- HTTP 200 OK (not 502)
- JSON response with uploadId
- No service crash in logs

---

## Additional Improvements (Preventive)

### 1. nginx Stability Enhancements

**File**: `nginx/api-gateway.conf`

**Changes**:
```nginx
# Docker DNS resolver (prevents IP caching issues)
resolver 127.0.0.11 valid=10s ipv6=off;
resolver_timeout 5s;

# Disable caching (development/staging environment)
proxy_buffering off;
proxy_cache off;
proxy_no_cache 1;
proxy_cache_bypass 1;
```

**File**: `nginx/proxy_params`

**Changes**:
```nginx
# Increased timeouts for large file uploads
proxy_connect_timeout 60s;
proxy_send_timeout 600s;
proxy_read_timeout 600s;

# Increased buffers for large responses
proxy_buffer_size 8k;
proxy_buffers 16 32k;
proxy_busy_buffers_size 64k;

# Error handling (retry on upstream errors)
proxy_next_upstream error timeout http_502 http_503 http_504;
proxy_next_upstream_tries 2;
proxy_next_upstream_timeout 10s;
```

**Benefits**:
- Dynamic upstream resolution (no IP caching)
- Better handling of large file uploads (Master List: 810KB)
- Automatic retry on transient errors
- No caching issues during development

---

## Lessons Learned

### 1. Column Name Mismatch Pattern

**Problem**:
- SQL 쿼리와 parsing 코드가 서로 다른 위치에 있음
- 컴파일 타임에 검증 불가
- Runtime에만 에러 발견

**Prevention**:
- ✅ Repository unit tests with mock database
- ✅ SQL query validation in CI/CD
- ✅ Column name constants (avoid hardcoded strings)

### 2. PostgreSQL libpq Error Handling

**Current**:
```cpp
int colIndex = PQfnumber(res, "file_hash");  // Returns -1 if not found
PQgetvalue(res, row, colIndex);              // Crashes on -1
```

**Better**:
```cpp
int colIndex = PQfnumber(res, "file_hash");
if (colIndex == -1) {
    throw std::runtime_error("Column 'file_hash' not found in result set");
}
PQgetvalue(res, row, colIndex);
```

**Recommendation**: Add validation layer in `resultToUpload()` helper

### 3. Docker Build Cache Issues

**Observation**:
- First rebuild (with cache) didn't apply the fix
- Second rebuild (with --no-cache) succeeded

**Root Cause**: CMake/build cache retained old binary despite source changes

**Solution**:
- Always use `--no-cache` for critical bug fixes
- Add build timestamp verification in logs
- Use `--force-recreate` when restarting containers

---

## Testing Checklist

- [x] Master List upload works (ICAO_ml_December2025.ml)
- [x] LDIF upload still works (regression test)
- [x] File hash deduplication works
- [ ] Upload history shows correct file hash
- [ ] Duplicate detection works across uploads
- [ ] nginx handles large files (>10MB) without timeout
- [ ] Service remains stable after multiple uploads

---

## Related Issues

- ✅ nginx 502 errors on container restart (fixed: DNS resolver)
- ✅ Upload form shows 502 instead of error message (backend crash)
- ⏭️ Add Repository layer unit tests (Phase 4.5)

---

## References

- **CLAUDE.md**: Main project documentation
- **DEVELOPMENT_GUIDE.md**: Debugging and troubleshooting guide
- **PostgreSQL libpq Documentation**: https://www.postgresql.org/docs/current/libpq.html
- **nginx proxy module**: http://nginx.org/en/docs/http/ngx_http_proxy_module.html
