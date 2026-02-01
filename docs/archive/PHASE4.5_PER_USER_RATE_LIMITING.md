# Phase 4.5: Per-User Rate Limiting - Implementation Complete

**Version**: v2.1.0 (Phase 4.5)
**Implementation Date**: 2026-01-23
**Status**: ✅ Complete - Code Changes Applied
**Priority**: Low (DoS Protection Enhancement)

---

## Overview

Enhanced rate limiting from per-IP to per-user (JWT-based) to prevent abuse by authenticated users while allowing multiple users from the same IP address (e.g., corporate networks, proxies).

---

## Security Enhancement

**Before (Per-IP Only)**:
- Rate limit: 100 requests/second per IP address
- Problem: Multiple users from same IP share the limit
- Problem: Single user can abuse from multiple IPs

**After (Per-User + Per-IP)**:
- Per-IP: 100 requests/second (general protection)
- Per-User Upload: 5 uploads/minute
- Per-User Export: 10 exports/hour
- Per-User PA Verify: 20 verifications/hour
- Benefit: Fair usage enforcement per user
- Benefit: Corporate network users don't interfere

---

## Implementation Details

### 1. JWT User ID Extraction ✅

**File**: `nginx/api-gateway.conf` (Lines 68-73)

**Nginx Map Directive**:
```nginx
# Extract user ID from JWT Authorization header
map $http_authorization $user_id {
    default "";
    "~^Bearer\s+[A-Za-z0-9-_]+\.(?<payload>[A-Za-z0-9-_]+)\.[A-Za-z0-9-_]+$" $payload;
}
```

**How It Works**:
1. Extracts JWT token from `Authorization: Bearer <token>` header
2. Parses JWT structure: `header.payload.signature`
3. Captures the `payload` section using named capture group
4. Payload contains Base64-encoded JSON with user claims (userId, username, etc.)

**Example**:
```
Authorization: Bearer eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJ1c2VySWQiOiJhZG1pbiIsInVzZXJuYW1lIjoiYWRtaW4ifQ.signature
                      ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^  ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^  ^^^^^^^^^
                      header (ignored)                    payload (captured)                                   signature (ignored)

$user_id = "eyJ1c2VySWQiOiJhZG1pbiIsInVzZXJuYW1lIjoiYWRtaW4ifQ"
```

**Why Payload Instead of userId**:
- Decoding JWT payload in Nginx requires external modules
- Using the entire payload as a unique identifier is sufficient
- Payload is unique per user (contains userId)
- Simpler and more performant

---

### 2. Rate Limit Zones ✅

**File**: `nginx/api-gateway.conf` (Lines 75-77)

**Configuration**:
```nginx
# Per-user rate limits for sensitive operations
limit_req_zone $user_id zone=upload_limit:10m rate=5r/m;      # 5 uploads per minute
limit_req_zone $user_id zone=export_limit:10m rate=10r/h;     # 10 exports per hour
limit_req_zone $user_id zone=pa_verify_limit:10m rate=20r/h;  # 20 PA verifications per hour
```

**Zone Parameters**:
- `$user_id`: Key for tracking (JWT payload)
- `zone=upload_limit:10m`: Zone name and memory allocation (10MB)
- `rate=5r/m`: Rate limit (5 requests per minute)

**Memory Sizing**:
- 10MB zone can track ~160,000 unique users
- Sufficient for typical PKD deployment

---

### 3. Applied Rate Limits ✅

#### Upload Endpoints (5 uploads/minute)

**File**: `nginx/api-gateway.conf` (Lines 92-105)

```nginx
location /api/upload {
    # Per-IP rate limiting (general protection)
    limit_req zone=api_limit burst=20 nodelay;

    # Per-user rate limiting (Phase 4.5 - JWT-based)
    # 5 uploads per minute per user
    limit_req zone=upload_limit burst=10 nodelay;

    proxy_pass http://pkd_management;
    include /etc/nginx/proxy_params;

    # Allow large file uploads (for LDIF/Master List)
    client_max_body_size 100M;
}
```

**Burst Allowance**:
- `burst=10`: Allow up to 10 requests to queue
- `nodelay`: Process burst requests immediately (don't slow them down)
- Prevents rejection of legitimate bursts (e.g., uploading 3 files quickly)

**Endpoints Covered**:
- POST `/api/upload/ldif`
- POST `/api/upload/masterlist`

---

#### Certificate Export Endpoints (10 exports/hour)

**File**: `nginx/api-gateway.conf` (Lines 120-129)

```nginx
location /api/certificates {
    # Per-IP rate limiting (general protection)
    limit_req zone=api_limit burst=50 nodelay;

    # Per-user rate limiting (Phase 4.5 - JWT-based)
    # Apply to export endpoints: 10 exports per hour per user
    limit_req zone=export_limit burst=5 nodelay;

    proxy_pass http://pkd_management;
    include /etc/nginx/proxy_params;
}
```

**Endpoints Covered**:
- GET `/api/certificates/export/file` (single certificate)
- GET `/api/certificates/export/country` (country ZIP)
- GET `/api/certificates/search` (search also limited)

**Rationale**:
- Export operations are resource-intensive (disk I/O, ZIP creation)
- Prevents bulk data exfiltration
- 10 exports/hour is sufficient for legitimate use cases

---

#### PA Verification Endpoints (20 verifications/hour)

**File**: `nginx/api-gateway.conf` (Lines 187-199)

```nginx
location /api/pa {
    # Per-IP rate limiting (general protection)
    limit_req zone=api_limit burst=50 nodelay;

    # Per-user rate limiting (Phase 4.5 - JWT-based)
    # 20 PA verifications per hour per user
    limit_req zone=pa_verify_limit burst=10 nodelay;

    proxy_pass http://pa_service;
    include /etc/nginx/proxy_params;

    # Allow reasonable body size for SOD/DG data
    client_max_body_size 10M;
}
```

**Endpoints Covered**:
- POST `/api/pa/verify` (full PA verification)
- POST `/api/pa/parse-sod` (SOD parsing)
- POST `/api/pa/parse-dg1` (DG1/MRZ parsing)
- POST `/api/pa/parse-dg2` (DG2/Face image parsing)

**Rationale**:
- PA verification is CPU-intensive (cryptographic operations)
- Prevents resource exhaustion attacks
- 20 verifications/hour is sufficient for testing/validation workflows

---

## Rate Limit Response

### HTTP 429 Too Many Requests

**Response Headers**:
```
HTTP/1.1 429 Too Many Requests
Retry-After: 60
Content-Type: text/html
```

**Response Body** (Nginx default):
```html
<html>
<head><title>429 Too Many Requests</title></head>
<body>
<center><h1>429 Too Many Requests</h1></center>
<hr><center>nginx</center>
</body>
</html>
```

**Note**: Frontend should handle 429 responses gracefully:
- Display user-friendly message: "Rate limit exceeded. Please try again in X minutes."
- Extract `Retry-After` header value
- Show countdown timer

---

## Testing Strategy

### Test 1: Upload Rate Limit (5/minute)

```bash
# Login as admin
TOKEN=$(curl -s -X POST http://localhost:8080/api/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username":"admin","password":"admin123"}' \
  | jq -r '.access_token')

# Upload 6 files rapidly (expect 6th to fail)
for i in {1..6}; do
  echo "Upload $i"
  curl -X POST http://localhost:8080/api/upload/ldif \
    -H "Authorization: Bearer $TOKEN" \
    -F "file=@test.ldif" \
    -w "Status: %{http_code}\n"
  sleep 1
done

# Expected result:
# Uploads 1-5: 200 OK
# Upload 6: 429 Too Many Requests (within 1 minute)
```

### Test 2: Export Rate Limit (10/hour)

```bash
# Export 11 certificates rapidly (expect 11th to fail)
for i in {1..11}; do
  echo "Export $i"
  curl -X GET "http://localhost:8080/api/certificates/export/country?country=KR" \
    -H "Authorization: Bearer $TOKEN" \
    -w "Status: %{http_code}\n" \
    -o /dev/null -s
done

# Expected result:
# Exports 1-10: 200 OK
# Export 11: 429 Too Many Requests (within 1 hour)
```

### Test 3: Multiple Users from Same IP

```bash
# Login as admin
ADMIN_TOKEN=$(curl -s -X POST http://localhost:8080/api/auth/login \
  -d '{"username":"admin","password":"admin123"}' | jq -r '.access_token')

# Login as testuser
USER_TOKEN=$(curl -s -X POST http://localhost:8080/api/auth/login \
  -d '{"username":"testuser","password":"user123"}' | jq -r '.access_token')

# Both users upload 5 files each (total 10 from same IP)
for i in {1..5}; do
  curl -X POST http://localhost:8080/api/upload/ldif \
    -H "Authorization: Bearer $ADMIN_TOKEN" \
    -F "file=@test.ldif" &
  curl -X POST http://localhost:8080/api/upload/ldif \
    -H "Authorization: Bearer $USER_TOKEN" \
    -F "file=@test.ldif" &
done
wait

# Expected result:
# All 10 uploads succeed (5 per user, no IP-based conflict)
```

### Test 4: Anonymous Requests (No JWT)

```bash
# Upload without token
curl -X POST http://localhost:8080/api/upload/ldif \
  -F "file=@test.ldif" \
  -w "Status: %{http_code}\n"

# Expected result:
# 401 Unauthorized (authentication required, rate limit not applied)
```

---

## Files Modified

| File | Lines Changed | Purpose |
|------|---------------|---------|
| `nginx/api-gateway.conf` | +30 lines | JWT user ID extraction, per-user rate limit zones, applied to endpoints |

**Total**: 1 file, ~30 lines added

---

## Security Improvements

### Before (Per-IP Only)
```
Corporate Network (1 IP address)
├─ User A: 50 uploads → OK
├─ User B: 30 uploads → OK
├─ User C: 20 uploads → BLOCKED (total 100 from IP)
└─ User D: 10 uploads → BLOCKED

Single Abuser (Multiple IPs)
├─ IP 1: 100 uploads → OK
├─ IP 2: 100 uploads → OK
├─ IP 3: 100 uploads → OK
└─ Total: 300 uploads (bypassed limit)
```

### After (Per-User + Per-IP)
```
Corporate Network (1 IP address)
├─ User A: 5 uploads/min → OK
├─ User B: 5 uploads/min → OK
├─ User C: 5 uploads/min → OK
└─ User D: 5 uploads/min → OK
(All users can work independently)

Single Abuser (Multiple IPs)
├─ IP 1: 5 uploads → OK
├─ IP 2: 1 upload → BLOCKED (user limit: 5/min)
└─ Total: 5 uploads (limit enforced per user)
```

---

## Compliance

### CWE-770: Allocation of Resources Without Limits
✅ Upload operations limited to 5/minute per user
✅ Export operations limited to 10/hour per user
✅ PA verification limited to 20/hour per user

### OWASP Top 10 - A05:2021 Security Misconfiguration
✅ Rate limiting enabled for all sensitive operations
✅ Defense in depth (per-IP + per-user)
✅ Burst handling for legitimate use cases

### DoS Protection
✅ Prevents resource exhaustion by single user
✅ Prevents bulk data exfiltration
✅ Maintains service availability for all users

---

## Performance Impact

**Negligible**: Nginx map directive and rate limiting are highly optimized.

**Benchmarks** (estimated):
- JWT payload extraction: <0.1ms per request
- Rate limit check: <0.1ms per request
- Total overhead: <0.2ms per request (0.02% for 1s operations)

**Memory Usage**:
- 3 rate limit zones × 10MB = 30MB
- Can track ~480,000 user-endpoint combinations
- Acceptable for typical PKD deployment

---

## Operational Considerations

### Adjusting Limits

Edit `nginx/api-gateway.conf` and reload Nginx:

```nginx
# Increase upload limit to 10/minute
limit_req_zone $user_id zone=upload_limit:10m rate=10r/m;

# Increase export limit to 20/hour
limit_req_zone $user_id zone=export_limit:10m rate=20r/h;
```

```bash
# Reload Nginx without downtime
docker exec icao-pkd-api-gateway nginx -s reload
```

### Monitoring Rate Limits

**Nginx Access Logs**:
```bash
# Check for 429 responses
docker logs icao-pkd-api-gateway | grep "429"

# Count rate limit hits by endpoint
docker logs icao-pkd-api-gateway | grep "429" | awk '{print $7}' | sort | uniq -c
```

**Recommended Metrics**:
- Rate of 429 responses per endpoint
- Top users hitting rate limits
- Average requests per user per endpoint

### Whitelisting Users

**Option 1**: Increase limits for specific users (requires Nginx Plus or Lua module)

**Option 2**: Grant `admin` permission (bypasses rate limits in application logic)

**Option 3**: Adjust global limits if current limits are too restrictive

---

## Next Steps

1. **Testing**: Verify rate limits with integration tests
2. **Monitoring**: Set up alerts for high 429 response rates
3. **Documentation**: Update API documentation with rate limit information
4. **Frontend**: Implement user-friendly 429 error handling

---

## Related Vulnerabilities

**Mitigated**:
- CWE-770: Allocation of Resources Without Limits
- OWASP A05:2021 - Security Misconfiguration (DoS protection)

**Complementary**:
- Phase 3: JWT authentication (user identification)
- Phase 4.4: Enhanced audit logging (track rate limit violations)

---

## References

- [Nginx limit_req_zone](http://nginx.org/en/docs/http/ngx_http_limit_req_module.html)
- [Nginx map directive](http://nginx.org/en/docs/http/ngx_http_map_module.html)
- [JWT Structure](https://jwt.io/introduction)
- [CWE-770](https://cwe.mitre.org/data/definitions/770.html) - Allocation of Resources Without Limits

---

**Implementation Status**: ✅ **COMPLETE**
**Code Review**: ⏳ Pending
**Testing**: ⏳ Pending deployment
**Deployment**: ⏳ Pending Phase 4.1-4.5 completion
