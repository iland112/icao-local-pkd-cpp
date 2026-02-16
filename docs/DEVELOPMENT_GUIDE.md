# Development Guide - ICAO Local PKD

**Version**: 2.11.0
**Last Updated**: 2026-02-17

---

## Quick Reference

### Essential Credentials

```bash
# PostgreSQL
DB_HOST=postgres
DB_PORT=5432
DB_NAME=localpkd
DB_USER=pkd
DB_PASSWORD=<from .env file>

# Oracle
ORACLE_HOST=oracle
ORACLE_PORT=1521
ORACLE_SERVICE_NAME=XEPDB1
ORACLE_USER=pkd_user
ORACLE_PASSWORD=<from .env file>

# LDAP
LDAP_HOST=openldap1:389 (or openldap2:389)
LDAP_ADMIN_DN=cn=admin,dc=ldap,dc=smartcoreinc,dc=com
LDAP_ADMIN_PASSWORD=ldap_test_password_123
LDAP_BASE_DN=dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
LDAP_DATA_DN=dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
```

### Service Ports

| Service | Port | Description |
|---------|------|-------------|
| PKD Management | 8081 | Upload, Certificate Search, ICAO Sync, Auth |
| PA Service | 8082 | Passive Authentication (ICAO 9303) |
| PKD Relay | 8083 | DB-LDAP Sync, Reconciliation |
| Monitoring Service | 8084 | System Metrics, Service Health |
| API Gateway | 8080 | nginx reverse proxy (`/api` prefix) |
| Frontend | 3000 | React 19 SPA |
| API Docs | 8090 | Swagger UI |

### Daily Commands

```bash
# Start/Stop system
./docker-start.sh
./docker-stop.sh
./docker-health.sh

# Clean init (DB + LDAP reset)
./docker-clean-and-init.sh

# Rebuild individual services
./scripts/build/rebuild-pkd-relay.sh [--no-cache]
./scripts/build/rebuild-frontend.sh

# Helper functions
source scripts/helpers/ldap-helpers.sh && ldap_count_all
source scripts/helpers/db-helpers.sh && db_count_certs
```

---

## Architecture Overview

### System Architecture (v2.11.0)

```
Frontend (React 19) --> API Gateway (nginx :8080) --> Backend Services --> DB/LDAP
                                                        |
                                                        +-- PKD Management (:8081)
                                                        |     Upload, Certificate Search, Auth, Audit
                                                        |
                                                        +-- PA Service (:8082)
                                                        |     Passive Authentication, DG2 Face Image
                                                        |
                                                        +-- PKD Relay (:8083)
                                                        |     DB-LDAP Sync, Reconciliation
                                                        |
                                                        +-- Monitoring Service (:8084)
                                                              System Metrics (DB-independent)
```

### Shared Libraries (`shared/lib/`)

| Library | Purpose |
|---------|---------|
| `icao::database` | DB connection pool, Query Executor (PostgreSQL + Oracle) |
| `icao::ldap` | Thread-safe LDAP connection pool (min=2, max=10) |
| `icao::audit` | Unified audit logging (operation_audit_log) |
| `icao::config` | Configuration management |
| `icao::exception` | Custom exception types |
| `icao::logging` | Structured logging (spdlog) |
| `icao::certificate-parser` | X.509 certificate parsing (22 fields) |
| `icao::icao9303` | ICAO 9303 SOD/DG parsers |
| `icao::validation` | ICAO 9303 certificate validation (trust chain, CRL, extensions, algorithm compliance) |

### Connection Pool Configuration

| Service | LDAP Pool | DB Pool | Notes |
|---------|-----------|---------|-------|
| PA Service | 2-10 | 5-20 | Verification-heavy |
| PKD Management | 3-15 | 5-20 | Upload/search operations |
| PKD Relay | 2-10 | 5-20 | Sync/reconciliation |
| Monitoring | N/A | N/A | DB-independent (HTTP only) |

### Design Patterns

| Pattern | Usage |
|---------|-------|
| **Repository Pattern** | 100% SQL abstraction - zero SQL in controllers |
| **Query Executor Pattern** | Database-agnostic queries via `IQueryExecutor` |
| **Factory Pattern** | Runtime database selection via `DB_TYPE` |
| **Strategy Pattern** | Processing strategies (AUTO/MANUAL upload modes) |
| **RAII** | Connection pooling (DB and LDAP) |
| **Provider/Adapter Pattern** | Infrastructure abstraction for validation (`ICscaProvider`, `ICrlProvider`) |

---

## Multi-DBMS Support

### Switching Databases

```bash
# Edit .env: DB_TYPE=postgres or DB_TYPE=oracle
# Then restart services:
docker compose -f docker/docker-compose.yaml restart pkd-management pa-service pkd-relay
```

### Oracle Compatibility Notes

| Issue | PostgreSQL | Oracle | Solution |
|-------|-----------|--------|----------|
| Column names | lowercase | UPPERCASE | Auto-lowercase in OracleQueryExecutor |
| Booleans | TRUE/FALSE | 1/0 | Database-aware formatting |
| Pagination | LIMIT/OFFSET | OFFSET ROWS FETCH NEXT | Database-specific SQL |
| Case search | ILIKE | UPPER() LIKE UPPER() | Conditional SQL |
| JSONB cast | `$1::jsonb` | `$1` (no cast) | Remove cast for Oracle |
| Empty string | '' (empty) | NULL | IS NOT NULL filter |
| Timestamps | NOW() | SYSTIMESTAMP | Database-specific function |

### RAII Pattern Example

```cpp
// Database - Query Executor
auto& executor = DatabaseConnectionPool::getExecutor();
auto result = executor.executeQuery(
    "SELECT * FROM certificate WHERE country_code = $1",
    {"KR"}
);

// LDAP - Connection Pool
auto conn = ldapPool_->acquire();
if (!conn.isValid()) {
    throw std::runtime_error("Failed to acquire LDAP connection");
}
LDAP* ld = conn.get();
// Connection automatically released when 'conn' goes out of scope
```

---

## Development Workflow

### 1. Code Modification

Source code locations:

| Service | Source Path |
|---------|------------|
| PKD Management | `services/pkd-management/src/` |
| PA Service | `services/pa-service/src/` |
| PKD Relay | `services/pkd-relay-service/src/` |
| Monitoring | `services/monitoring-service/src/` |
| Shared Libraries | `shared/lib/` |
| Frontend | `frontend/src/` |

### 2. Build and Deploy

**Option A: Quick rebuild (recommended)**
```bash
# Rebuild specific service
./scripts/build/rebuild-pkd-relay.sh
./scripts/build/rebuild-frontend.sh

# Or via docker compose
docker compose -f docker/docker-compose.yaml build pkd-management
docker compose -f docker/docker-compose.yaml up -d pkd-management
```

**Option B: Force fresh build (dependency changes, Dockerfile changes)**
```bash
./scripts/build/rebuild-pkd-relay.sh --no-cache
```

**When to use `--no-cache`:**
- CMakeLists.txt or vcpkg.json changes
- New library additions
- Dockerfile modifications
- Version mismatch between source and binary

### 3. Testing

```bash
# Source helper scripts
source scripts/helpers/ldap-helpers.sh
source scripts/helpers/db-helpers.sh

# Check all service health
./docker-health.sh

# Or individually
curl http://localhost:8080/api/health | jq .
curl http://localhost:8080/api/pa/health | jq .
curl http://localhost:8080/api/sync/health | jq .

# Data verification
ldap_count_all          # Count all LDAP entries
db_count_certs          # Count DB certificates

# Test reconciliation
curl -X POST http://localhost:8080/api/sync/reconcile \
  -H "Content-Type: application/json" \
  -d '{"dryRun": false}' | jq .

# Test PA verification
curl -X POST http://localhost:8080/api/pa/verify \
  -H "Content-Type: application/json" \
  -d '{"sod":"<base64>","dataGroups":{"1":"<base64>","2":"<base64>"}}' | jq .
```

### Unit Tests

```bash
# Run icao::validation unit tests (86 tests) via Docker
docker build -f shared/lib/icao-validation/tests/Dockerfile.test \
  --build-arg BASE_IMAGE=icao-vcpkg-base:latest \
  -t icao-validation-tests .

# Or build locally (requires OpenSSL + GTest)
cd shared/lib/icao-validation
cmake -B build -S . -DBUILD_VALIDATION_TESTS=ON
cmake --build build -j$(nproc)
cd build && ctest --output-on-failure --verbose
```

Test modules (86 tests total):
- `test_cert_ops` — Pure X509 certificate operations (34 tests)
- `test_extension_validator` — X.509 extension validation (10 tests)
- `test_algorithm_compliance` — ICAO algorithm requirements (14 tests)
- `test_trust_chain_builder` — Trust chain construction (11 tests)
- `test_crl_checker` — CRL revocation checking (17 tests)

All modules include idempotency verification (50-100 iterations per function).

---

## Helper Scripts

### Directory Structure

```
scripts/
+-- docker/          # Docker management (start, stop, restart, health, logs, backup)
+-- luckfox/         # ARM64 deployment (same structure as docker/)
+-- build/           # Build scripts (build, rebuild-*, check-freshness, verify-*)
+-- helpers/         # Utility functions (db-helpers.sh, ldap-helpers.sh)
+-- maintenance/     # Data management (reset-all-data, reset-ldap, dn-migration)
+-- monitoring/      # System monitoring (icao-version-check)
+-- deploy/          # Deployment (from-github-artifacts)
+-- dev/             # Development scripts (start/stop/rebuild/logs dev services)
```

### LDAP Helpers

```bash
source scripts/helpers/ldap-helpers.sh
ldap_info                   # Show connection info
ldap_count_all              # Count all certificates
ldap_count_certs CRL        # Count CRLs
ldap_search_country KR      # Search by country
ldap_delete_all_crls        # Delete all CRLs (testing)
```

### Database Helpers

```bash
source scripts/helpers/db-helpers.sh
db_info                     # Show connection info
db_count_certs              # Count certificates by type
db_count_crls               # Count CRLs
db_reset_crl_flags          # Reset CRL stored_in_ldap flags
db_reconciliation_summary 10    # Show last 10 reconciliations
db_latest_reconciliation_logs   # Show latest reconciliation logs
db_sync_status 10           # Show sync status history
```

---

## Common LDAP Operations

### Correct LDAP Search Commands

```bash
# Count CRLs
docker exec icao-local-pkd-openldap1 \
  ldapsearch -x \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w "ldap_test_password_123" \
  -b "dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  "(objectClass=cRLDistributionPoint)" dn 2>&1 | \
  grep "^dn:" | wc -l

# Count certificates
docker exec icao-local-pkd-openldap1 \
  ldapsearch -x \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w "ldap_test_password_123" \
  -b "dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  "(objectClass=pkdDownload)" dn 2>&1 | \
  grep "^dn:" | wc -l
```

**Common Mistakes:**
- Wrong password: `admin` -> Correct: `ldap_test_password_123`
- Wrong base DN: `dc=pkd,...` -> Correct: `dc=download,dc=pkd,...`
- Wrong filter: `(cn=*)` -> Correct: `(objectClass=cRLDistributionPoint)`
- Missing `-x` flag -> Always use `-x` for simple authentication

---

## Common Database Operations

### PostgreSQL Commands

```bash
# Count certificates
docker exec icao-local-pkd-postgres \
  psql -U pkd -d localpkd \
  -c "SELECT certificate_type, COUNT(*) FROM certificate GROUP BY certificate_type;"

# Check reconciliation history
docker exec icao-local-pkd-postgres \
  psql -U pkd -d localpkd \
  -c "SELECT id, started_at, status, total_processed
      FROM reconciliation_summary ORDER BY started_at DESC LIMIT 5;"
```

### Oracle Commands

```bash
# Count certificates
docker exec icao-local-pkd-oracle bash -c \
  "echo \"SELECT certificate_type, COUNT(*) FROM certificate GROUP BY certificate_type;\" \
  | sqlplus -s pkd_user/\${ORACLE_PASSWORD}@//localhost:1521/XEPDB1"
```

**Common Mistakes:**
- Wrong DB name: `pkd` -> Correct: `localpkd`
- Oracle: remember `UPPERCASE` column names in result sets

---

## Troubleshooting

### Build Issues

**Binary version mismatch:**
```bash
./scripts/build/rebuild-pkd-relay.sh --no-cache
docker logs icao-local-pkd-relay --tail 5 | grep version
```

### LDAP Connection Issues

**`ldap_bind: Invalid credentials (49)`:**
```bash
# Use correct password
LDAP_PASSWORD="ldap_test_password_123"  # NOT "admin"
source scripts/helpers/ldap-helpers.sh && ldap_count_all
```

**`No such object (32)`:**
```bash
# Check DN hierarchy exists
docker exec icao-local-pkd-openldap1 \
  ldapsearch -x -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w "ldap_test_password_123" \
  -b "dc=ldap,dc=smartcoreinc,dc=com" "(objectClass=*)" dn | grep "^dn:"
```

### Oracle Issues

**"Value is not convertible to Int":**
- Oracle returns all values as strings. Use `getInt()` helper.

**Empty string = NULL:**
- `WHERE column != ''` fails on Oracle. Use `WHERE column IS NOT NULL`.

### 502 Bad Gateway after container restart

nginx uses `resolver 127.0.0.11 valid=10s` with per-request DNS resolution. Usually resolves within 10 seconds.

### Sync Dashboard Timeout

Thread-safe connection pool (RAII pattern) prevents this. If it occurs, restart the pkd-relay service.

---

## Production Data Summary

| Certificate Type | Count | LDAP | Coverage |
|------------------|-------|------|----------|
| CSCA | 845 | 845 | 100% |
| MLSC | 27 | 27 | 100% |
| DSC | 29,838 | 29,838 | 100% |
| DSC_NC | 502 | 502 | 100% |
| CRL | 69 | 69 | 100% |
| **Total** | **31,212** | **31,212** | **100%** |
