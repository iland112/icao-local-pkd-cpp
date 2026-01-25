# Development Guide - ICAO Local PKD

**Version**: 2.0.5
**Last Updated**: 2026-01-25

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

# LDAP
LDAP_HOST=openldap1:389 (or openldap2:389)
LDAP_ADMIN_DN=cn=admin,dc=ldap,dc=smartcoreinc,dc=com
LDAP_ADMIN_PASSWORD=ldap_test_password_123
LDAP_BASE_DN=dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
LDAP_DATA_DN=dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
```

### Helper Scripts

All helper scripts are located in `scripts/` directory:

```bash
# Rebuild PKD Relay Service
./scripts/rebuild-pkd-relay.sh              # Normal build (uses cache)
./scripts/rebuild-pkd-relay.sh --no-cache   # Force fresh build

# LDAP Operations
source scripts/ldap-helpers.sh              # Load LDAP helper functions
ldap_info                                   # Show connection info
ldap_count_all                              # Count all certificates
ldap_count_certs CRL                        # Count CRLs
ldap_search_country KR                      # Search by country
ldap_delete_all_crls                        # Delete all CRLs (testing)

# Database Operations
source scripts/db-helpers.sh                # Load DB helper functions
db_info                                     # Show connection info
db_count_certs                              # Count certificates by type
db_count_crls                               # Count CRLs
db_reset_crl_flags                          # Reset CRL stored_in_ldap flags
db_reconciliation_summary 10                # Show last 10 reconciliations
db_latest_reconciliation_logs               # Show latest reconciliation logs
db_sync_status 10                           # Show sync status history
```

---

## Development Workflow

### 1. Code Modification

```bash
# Edit source files
vim services/pkd-relay-service/src/relay/sync/reconciliation_engine.cpp

# Always update version in main.cpp for cache busting
vim services/pkd-relay-service/src/main.cpp
# Update: spdlog::info("... v2.0.X ...")
```

### 2. Build and Deploy

**Option A: Quick rebuild (recommended for testing)**
```bash
./scripts/rebuild-pkd-relay.sh
```

**Option B: Force fresh build (when cache issues suspected)**
```bash
./scripts/rebuild-pkd-relay.sh --no-cache
```

**Manual build (for debugging)**
```bash
# Build image
docker build -t icao-pkd-relay:v2.0.5 -f services/pkd-relay-service/Dockerfile .

# Stop current container
docker-compose -f docker/docker-compose.yaml stop pkd-relay

# Remove current container
docker-compose -f docker/docker-compose.yaml rm -f pkd-relay

# Start new container
docker-compose -f docker/docker-compose.yaml up -d pkd-relay

# Check logs
docker logs icao-local-pkd-relay --tail 50
```

### 3. Testing

```bash
# Source helper scripts
source scripts/ldap-helpers.sh
source scripts/db-helpers.sh

# Check service health
curl http://localhost:8080/api/sync/health | jq .

# Check sync status
curl http://localhost:8080/api/sync/status | jq .

# Prepare test data (reset CRL flags)
db_reset_crl_flags

# Trigger reconciliation
curl -X POST http://localhost:8080/api/sync/reconcile \
  -H "Content-Type: application/json" \
  -d '{"dryRun": false}' | jq .

# Verify results
ldap_count_all                              # Check LDAP counts
db_count_crls                               # Check DB counts
db_latest_reconciliation_logs               # Check reconciliation logs
```

---

## Common LDAP Operations

### Correct LDAP Search Commands

**Always use these exact parameters:**

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

# Search by country
docker exec icao-local-pkd-openldap1 \
  ldapsearch -x \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w "ldap_test_password_123" \
  -LLL \
  -b "c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  "(objectClass=pkdDownload)" dn
```

**Common Mistakes to Avoid:**

- ❌ Wrong password: `admin` → ✅ Correct: `ldap_test_password_123`
- ❌ Wrong base DN: `dc=pkd,...` → ✅ Correct: `dc=download,dc=pkd,...`
- ❌ Wrong filter: `(cn=*)` → ✅ Correct: `(objectClass=cRLDistributionPoint)`
- ❌ Missing `-x` flag → ✅ Always use `-x` for simple authentication

---

## Common Database Operations

### Correct PostgreSQL Commands

```bash
# Count CRLs
docker exec icao-local-pkd-postgres \
  psql -U pkd -d localpkd \
  -c "SELECT COUNT(*), stored_in_ldap FROM crl GROUP BY stored_in_ldap;"

# Reset CRL flags
docker exec icao-local-pkd-postgres \
  psql -U pkd -d localpkd \
  -c "UPDATE crl SET stored_in_ldap = FALSE;
      SELECT COUNT(*) FROM crl WHERE stored_in_ldap = FALSE;"

# Check reconciliation summary
docker exec icao-local-pkd-postgres \
  psql -U pkd -d localpkd \
  -c "SELECT id, started_at, status, crl_added, total_processed
      FROM reconciliation_summary
      ORDER BY started_at DESC LIMIT 5;"

# Check reconciliation logs
docker exec icao-local-pkd-postgres \
  psql -U pkd -d localpkd \
  -c "SELECT cert_type, cert_fingerprint, status, operation
      FROM reconciliation_log
      WHERE reconciliation_id = (SELECT MAX(id) FROM reconciliation_summary)
      LIMIT 10;"
```

**Common Mistakes to Avoid:**

- ❌ Wrong DB name: `pkd` → ✅ Correct: `localpkd`
- ❌ Missing quotes in SQL → ✅ Always wrap SQL in quotes
- ❌ Forgetting to check reconciliation_id → ✅ Use MAX(id) for latest

---

## Docker Build Best Practices

### Cache Strategy

**When to use cache (default):**
- Minor code changes
- Dependency versions unchanged
- Quick iteration during development

**When to use --no-cache:**
- Dependency updates (vcpkg.json changed)
- Build errors that might be cache-related
- Version mismatch between source and binary
- After major refactoring

### Build Verification

Always verify the build version after rebuild:

```bash
# Check version in logs
docker logs icao-local-pkd-relay --tail 5 | grep "v2.0"

# Expected output:
# [info] ICAO Local PKD - PKD Relay Service v2.0.5
```

If version doesn't match source code:
1. Use `--no-cache` flag
2. Check if version string was updated in main.cpp
3. Verify docker image tag matches

---

## Troubleshooting

### Build Issues

**Symptom**: Binary version doesn't match source code version

**Solution**:
```bash
# Force rebuild without cache
./scripts/rebuild-pkd-relay.sh --no-cache

# Verify version
docker logs icao-local-pkd-relay --tail 5 | grep version
```

**Symptom**: Build hangs or takes too long

**Solution**:
```bash
# Kill background builds
docker ps -a | grep build
docker kill <build-container-id>

# Clean build artifacts
docker system prune -f

# Retry with normal build
./scripts/rebuild-pkd-relay.sh
```

### LDAP Connection Issues

**Symptom**: `ldap_bind: Invalid credentials (49)`

**Solution**:
```bash
# Use correct password
LDAP_PASSWORD="ldap_test_password_123"  # NOT "admin"

# Or use helper script
source scripts/ldap-helpers.sh
ldap_count_all
```

**Symptom**: `No such object (32)`

**Solution**:
```bash
# Check base DN is correct
BASE_DN="dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com"

# Verify DN hierarchy exists
docker exec icao-local-pkd-openldap1 \
  ldapsearch -x -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w "ldap_test_password_123" \
  -b "dc=ldap,dc=smartcoreinc,dc=com" \
  "(objectClass=*)" dn | grep "^dn:"
```

### Database Issues

**Symptom**: `relation does not exist`

**Solution**:
```bash
# Check table exists
db_query "\dt"

# Check in correct database
docker exec icao-local-pkd-postgres psql -U pkd -l
# Should show: localpkd (not pkd)
```

**Symptom**: Reconciliation logs not appearing

**Solution**:
```bash
# Check table schema
db_query "\d reconciliation_log"

# Should have: cert_fingerprint VARCHAR(64)
# NOT: cert_id INTEGER

# If schema wrong, apply migration:
db_query "ALTER TABLE reconciliation_log ADD COLUMN cert_fingerprint VARCHAR(64);"
db_query "ALTER TABLE reconciliation_log DROP COLUMN cert_id;"
```

---

## Version History

### v2.0.5 (2026-01-25)
- CRL reconciliation support
- reconciliation_log UUID fix (cert_id → cert_fingerprint)
- Helper scripts for consistent development

### v2.0.4 (2026-01-25)
- Auto parent DN creation in LDAP

### v2.0.3 (2026-01-24)
- Fingerprint-based DN format

### v2.0.0 (2026-01-21)
- Service separation (PKD Relay Service)

---

## Contact

For questions or issues:
- Check logs: `docker logs icao-local-pkd-relay --tail 100`
- Check helpers: `source scripts/ldap-helpers.sh && ldap_info`
- Check database: `source scripts/db-helpers.sh && db_info`
