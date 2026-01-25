# Sprint 1: LDAP DN Migration - Testing Guide

**Version**: 1.0.0
**Created**: 2026-01-23
**Sprint**: Week 5 - LDAP Storage Fix
**Environment**: Local System (Docker Desktop / WSL2)

---

## Prerequisites

### Required Tools

- Docker & Docker Compose
- PostgreSQL client (psql)
- OpenLDAP client (ldapsearch, ldapdelete)
- curl
- jq (optional, for JSON parsing)

### Environment Setup

```bash
# 1. Navigate to project root
cd /home/kbjung/projects/c/icao-local-pkd

# 2. Ensure .env file exists with valid credentials
cp .env.example .env
# Edit .env and set DB_PASSWORD, LDAP_BIND_PASSWORD, JWT_SECRET_KEY

# 3. Start all services
./docker-start.sh --build

# 4. Wait for services to be ready (~2 minutes)
./docker-health.sh
```

---

## Test Plan Overview

| Phase | Duration | Description |
|-------|----------|-------------|
| **Phase 1** | 10 min | Database schema verification |
| **Phase 2** | 15 min | Unit tests (buildCertificateDnV2) |
| **Phase 3** | 20 min | Dry-run migration |
| **Phase 4** | 30 min | Test mode migration (100 records) |
| **Phase 5** | 10 min | Rollback verification |
| **Phase 6** | 60 min | Production mode migration (full dataset) |
| **Phase 7** | 15 min | Integration testing |
| **Total** | **2.5 hours** | |

---

## Phase 1: Database Schema Verification

### Step 1.1: Check Migration Tables

```bash
docker exec -it icao-pkd-postgres psql -U pkd -d pkd <<SQL
-- Check ldap_dn_v2 columns exist
\d certificate
\d master_list
\d crl

-- Check migration tracking tables
\d ldap_migration_status
\d ldap_migration_error_log
SQL
```

**Expected Output**:
- `certificate` table has `ldap_dn_v2 VARCHAR(512)` column
- `ldap_migration_status` table exists with proper schema
- `ldap_migration_error_log` table exists

### Step 1.2: Verify Initial State

```bash
docker exec -it icao-pkd-postgres psql -U pkd -d pkd <<SQL
-- All ldap_dn_v2 should be NULL initially
SELECT COUNT(*) AS total_certs,
       COUNT(ldap_dn_v2) AS migrated_certs
FROM certificate
WHERE ldap_stored = true;

-- Migration status should be PENDING or not exist
SELECT * FROM ldap_migration_status
WHERE table_name = 'certificate'
ORDER BY created_at DESC LIMIT 1;
SQL
```

**Expected Output**:
- `total_certs`: > 0 (number of certificates in LDAP)
- `migrated_certs`: 0 (no migration done yet)
- `status`: PENDING or no rows

---

## Phase 2: Unit Tests

### Step 2.1: Build Unit Tests

```bash
cd services/pkd-management

# Build with tests enabled
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake
cmake --build build --target test_ldap_dn
```

### Step 2.2: Run Unit Tests

```bash
cd build
./test_ldap_dn

# Or with verbose output
./test_ldap_dn --gtest_output=xml:test_results.xml
```

**Expected Output**:
```
[==========] Running 13 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 13 tests from LdapDnV2Test
[ RUN      ] LdapDnV2Test.BuildDnV2_CSCA_Basic
[       OK ] LdapDnV2Test.BuildDnV2_CSCA_Basic (0 ms)
[ RUN      ] LdapDnV2Test.BuildDnV2_DSC_Basic
[       OK ] LdapDnV2Test.BuildDnV2_DSC_Basic (0 ms)
...
[----------] 13 tests from LdapDnV2Test (5 ms total)

[==========] 13 tests from 1 test suite ran. (5 ms total)
[  PASSED  ] 13 tests.
```

**If any test fails**: Stop and investigate before proceeding.

---

## Phase 3: Dry-Run Migration

### Step 3.1: Run Dry-Run Script

```bash
cd /home/kbjung/projects/c/icao-local-pkd
./scripts/ldap-dn-migration-dryrun.sh
```

### Step 3.2: Review Dry-Run Output

**Check Serial Number Collisions**:
```
Serial number collisions:
serial_number | collision_count | issuers
--------------+-----------------+-----------------------------------
1             | 150             | C=US,O=...,CN=CSCA-USA | C=FR,...
2             | 120             | C=DE,O=...,CN=CSCA-DE | C=IT,...
```

**Expected**: Multiple serial number collisions found (this is the problem we're solving)

**Check Sample New DNs**:
```
cert_id    | fingerprint_short  | certificate_type | country_code | new_dn_sample
-----------+--------------------+------------------+--------------+---------------
uuid-123   | 0a1b2c3d...        | CSCA             | KR           | cn=0a1b2c3d...,o=csca,c=KR,...
uuid-456   | 1234abcd...        | DSC              | US           | cn=1234abcd...,o=dsc,c=US,...
```

**Expected**: DNs are ~130 characters, well under 255 limit

**Check DN Length Distribution**:
```
cert_type | count | min_dn_length | max_dn_length | avg_dn_length
----------+-------+---------------+---------------+---------------
CSCA      | 525   | 128           | 135           | 131
DSC       | 29610 | 128           | 135           | 131
DSC_NC    | 502   | 133           | 140           | 136
```

**Expected**: All DN lengths < 255

**Check Fingerprint Uniqueness**:
```
All fingerprints are unique - migration is safe
```

**Expected**: No duplicate fingerprints (PASS)

**If dry-run shows issues**: Investigate before proceeding to test migration.

---

## Phase 4: Test Mode Migration (100 Records)

### Step 4.1: Run Test Migration

```bash
cd /home/kbjung/projects/c/icao-local-pkd
./scripts/ldap-dn-migration.sh test
```

### Step 4.2: Monitor Progress

**Expected Output**:
```
=============================================================================
 LDAP DN Migration - Live Mode
=============================================================================
Mode: test
Batch Size: 100
...

[PROGRESS] Batch #1 - Processing 30226 remaining records (offset: 0)...
[SUCCESS]   Batch #1 complete - Success: 100, Failed: 0
[INFO]      Overall progress: 100/30226 (0%)

Migration complete!

=============================================================================
 Migration Summary
=============================================================================
[SUCCESS] Migration mode: test
[SUCCESS] Total certificates: 30226
[SUCCESS] Successfully migrated: 100
[SUCCESS] Migration status: COMPLETED
```

### Step 4.3: Verify Test Results

```bash
docker exec -it icao-pkd-postgres psql -U pkd -d pkd <<SQL
-- Check migration progress
SELECT COUNT(*) AS total,
       COUNT(ldap_dn_v2) AS migrated,
       COUNT(*) - COUNT(ldap_dn_v2) AS remaining
FROM certificate
WHERE ldap_stored = true;

-- Verify DN format
SELECT id, fingerprint_sha256, certificate_type, country_code,
       LEFT(ldap_dn_v2, 80) AS dn_preview
FROM certificate
WHERE ldap_dn_v2 IS NOT NULL
LIMIT 10;

-- Check for duplicates (should be 0)
SELECT ldap_dn_v2, COUNT(*)
FROM certificate
WHERE ldap_dn_v2 IS NOT NULL
GROUP BY ldap_dn_v2
HAVING COUNT(*) > 1;
SQL
```

**Expected Output**:
- `migrated`: 100
- `remaining`: 30126 (or total - 100)
- `dn_preview`: Starts with `cn=` + 64-char fingerprint
- Duplicate check: 0 rows

**Test Mode Limitation**: LDAP entries are NOT created in test mode (only database updated).

---

## Phase 5: Rollback Verification

### Step 5.1: Run Rollback

```bash
cd /home/kbjung/projects/c/icao-local-pkd
./scripts/ldap-dn-rollback.sh
```

**Confirmation Prompt**:
```
⚠️  WARNING: This script will delete all migrated LDAP entries!
⚠️  WARNING: This action cannot be undone!

Are you sure you want to rollback the migration? (type 'yes' to confirm):
```

Type: `yes`

### Step 5.2: Verify Rollback

```bash
docker exec -it icao-pkd-postgres psql -U pkd -d pkd <<SQL
-- All ldap_dn_v2 should be NULL again
SELECT COUNT(*) AS total,
       COUNT(ldap_dn_v2) AS migrated
FROM certificate
WHERE ldap_stored = true;

-- Migration status should be ROLLED_BACK
SELECT status FROM ldap_migration_status
WHERE table_name = 'certificate'
ORDER BY created_at DESC LIMIT 1;
SQL
```

**Expected Output**:
- `migrated`: 0 (all rolled back)
- `status`: ROLLED_BACK

**If rollback fails**: Critical issue - investigate immediately.

---

## Phase 6: Production Mode Migration (Full Dataset)

⚠️ **CRITICAL**: Only proceed if all previous phases passed.

### Step 6.1: Pre-Migration Backup

```bash
# Backup PostgreSQL
./docker-backup.sh

# Verify backup exists
ls -lh backups/
```

### Step 6.2: Run Production Migration

```bash
cd /home/kbjung/projects/c/icao-local-pkd
./scripts/ldap-dn-migration.sh production
```

**Confirmation Prompt**:
```
⚠️  PRODUCTION MODE - Will write to LDAP!
⚠️  This action cannot be undone without rollback!

Are you sure you want to proceed? (type 'yes' to confirm):
```

Type: `yes`

### Step 6.3: Monitor Production Migration

**Expected Output** (for ~30,000 certificates):
```
[PROGRESS] Batch #1 - Processing 30226 remaining records (offset: 0)...
[SUCCESS]   Batch #1 complete - Success: 100, Failed: 0
[INFO]      Overall progress: 100/30226 (0%)

[PROGRESS] Batch #2 - Processing 30126 remaining records (offset: 100)...
[SUCCESS]   Batch #2 complete - Success: 100, Failed: 0
[INFO]      Overall progress: 200/30226 (0%)

...

[PROGRESS] Batch #303 - Processing 26 remaining records (offset: 30200)...
[SUCCESS]   Batch #303 complete - Success: 26, Failed: 0
[INFO]      Overall progress: 30226/30226 (100%)

All certificates migrated!

=============================================================================
 Migration Summary
=============================================================================
[SUCCESS] Migration mode: production
[SUCCESS] Total certificates: 30226
[SUCCESS] Successfully migrated: 30226
[SUCCESS] Migration status: COMPLETED
```

**Estimated Duration**: ~10 minutes (100 certs/batch, 2s delay = ~606s)

**If errors occur**: Check error log, consider partial rollback.

### Step 6.4: Verify Production Migration

```bash
docker exec -it icao-pkd-postgres psql -U pkd -d pkd <<SQL
-- All certificates should be migrated
SELECT COUNT(*) AS total,
       COUNT(ldap_dn_v2) AS migrated,
       COUNT(*) - COUNT(ldap_dn_v2) AS remaining
FROM certificate
WHERE ldap_stored = true;

-- Check migration statistics by type
SELECT certificate_type,
       COUNT(*) AS total,
       COUNT(ldap_dn_v2) AS migrated
FROM certificate
WHERE ldap_stored = true
GROUP BY certificate_type;

-- Verify no DN duplicates
SELECT COUNT(*) AS duplicate_count
FROM (
    SELECT ldap_dn_v2
    FROM certificate
    WHERE ldap_dn_v2 IS NOT NULL
    GROUP BY ldap_dn_v2
    HAVING COUNT(*) > 1
) AS dups;
SQL
```

**Expected Output**:
- `remaining`: 0 (all migrated)
- `duplicate_count`: 0 (no collisions)

### Step 6.5: Verify LDAP Entries

```bash
# Count total entries with new DN format
docker exec -it icao-openldap1 ldapsearch -x \
  -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w admin \
  -b "dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  "(cn=*)" cn | grep "^dn:" | wc -l

# Sample 10 entries to verify DN format
docker exec -it icao-openldap1 ldapsearch -x \
  -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w admin \
  -b "dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  "(cn=*)" dn | head -30
```

**Expected**: 30,226+ entries (including old DNs if not cleaned up)

---

## Phase 7: Integration Testing

### Step 7.1: Certificate Search API

```bash
# Search by country (should use new DNs internally)
curl http://localhost:8080/api/certificates/search?country=KR&limit=10

# Expected: success=true, certificates returned
```

### Step 7.2: Certificate Export

```bash
# Export KR certificates to ZIP
curl http://localhost:8080/api/certificates/export/country?country=KR&certType=CSCA -o kr_csca.zip

# Verify ZIP contents
unzip -l kr_csca.zip
```

**Expected**: ZIP file with CSCA certificates from Korea

### Step 7.3: Upload New LDIF

```bash
# Upload a test LDIF file to verify new certificates use v2 DN
curl -X POST http://localhost:8080/api/upload/ldif \
  -F "file=@test_data/sample.ldif" \
  -F "processingMode=AUTO"

# Check if new certificates have ldap_dn_v2 populated
docker exec -it icao-pkd-postgres psql -U pkd -d pkd <<SQL
SELECT id, certificate_type, country_code, ldap_dn_v2
FROM certificate
WHERE created_at > NOW() - INTERVAL '5 minutes'
  AND ldap_stored = true
LIMIT 10;
SQL
```

**Expected**: New certificates have `ldap_dn_v2` populated with fingerprint-based DN.

---

## Rollback Procedure (If Issues Found)

### Emergency Rollback

```bash
cd /home/kbjung/projects/c/icao-local-pkd
./scripts/ldap-dn-rollback.sh

# Confirm with: yes
```

### Restore from Backup

```bash
# Restore PostgreSQL
./docker-restore.sh backups/icao-backup-YYYYMMDD_HHMMSS/postgres-pkd.sql

# Restart services
./docker-restart.sh
```

---

## Success Criteria

### ✅ All Tests Must Pass

- [ ] Database schema has `ldap_dn_v2` columns
- [ ] Unit tests pass (13/13)
- [ ] Dry-run shows no blocking issues
- [ ] Test mode migration (100 records) succeeds
- [ ] Rollback works correctly
- [ ] Production migration completes (30,226 records)
- [ ] No DN duplicates in database
- [ ] LDAP has correct number of entries
- [ ] Certificate search API works
- [ ] Certificate export API works
- [ ] New uploads use v2 DN format

### 📊 Expected Metrics

| Metric | Before | After |
|--------|--------|-------|
| Certificates with ldap_dn_v2 | 0 | 30,226 |
| Serial number collisions | 150+ | Resolved (DNs unique) |
| Average DN length | 200+ chars | ~131 chars |
| DN duplicates | Possible | 0 |

---

## Troubleshooting

### Issue: Unit tests fail

**Solution**: Fix code issues before proceeding. Do NOT continue to migration.

### Issue: Dry-run shows duplicate fingerprints

**Solution**: Investigate database integrity. This is a critical blocker.

### Issue: Migration API returns 500 error

**Check logs**:
```bash
docker logs icao-pkd-management --tail 100
```

**Common causes**:
- LDAP connection failure (check credentials)
- Database connection failure
- Out of memory (reduce batch size)

### Issue: Migration is very slow

**Tuning**:
```bash
# Increase batch size (default: 100)
export BATCH_SIZE=500

# Reduce delay between batches (default: 2s)
export BATCH_DELAY=1

./scripts/ldap-dn-migration.sh production
```

### Issue: Some certificates fail to migrate

**Check error log**:
```bash
docker exec -it icao-pkd-postgres psql -U pkd -d pkd <<SQL
SELECT * FROM ldap_migration_error_log
ORDER BY created_at DESC
LIMIT 20;
SQL
```

**Common causes**:
- Invalid certificate binary data
- LDAP add failure (check LDAP logs)
- Database constraint violation

---

## Post-Testing Checklist

After all tests pass on Local System:

- [ ] Document any issues encountered and solutions
- [ ] Update migration scripts if needed
- [ ] Create GitHub issue for tracking deployment
- [ ] Prepare deployment checklist for Luckfox
- [ ] **DO NOT deploy to Luckfox until 100% verification complete**

---

## Next Steps

After successful local testing:

1. **Code Review**: Peer review of all changes
2. **Documentation Update**: Update CLAUDE.md with Sprint 1 completion
3. **Deployment Planning**: Schedule Luckfox deployment
4. **Sprint 2 Preparation**: Begin Link Certificate Validation Core design

---

**Testing Status**: ⏳ Pending
**Last Updated**: 2026-01-23
**Next Review**: After Day 7 testing complete
