# Sprint 1: LDAP DN Schema Design

**Version**: 1.0.0
**Created**: 2026-01-23
**Sprint Duration**: 7 days
**Target**: Week 5 (LDAP Storage Fix)

---

## Objective

Redesign LDAP Distinguished Name (DN) structure to use SHA-256 fingerprint as the primary identifier instead of Subject DN + Serial Number combination, resolving RFC 5280 non-compliance and DN escaping issues.

---

## Current DN Structure (Problematic)

### Certificate DN Format
```
cn={ESCAPED-SUBJECT-DN}+sn={SERIAL},o={csca|dsc|dsc_nc},c={COUNTRY},dc={data|nc-data},dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
```

**Example**:
```
cn=C\=KR\,O\=Government of Korea\,OU\=MOFA\,CN\=CSCA-KOREA-2025+sn=1,o=csca,c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
```

### Master List DN Format
```
cn={fingerprint},o=ml,c={COUNTRY},dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
```

**Example**:
```
cn=0f6c529d...,o=ml,c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
```

### CRL DN Format
```
cn={fingerprint},o=crl,c={COUNTRY},dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
```

---

## Problems with Current Design

### 1. RFC 5280 Serial Number Uniqueness Violation

**Issue**: Multiple certificates share the same serial number across different issuers.

**Evidence**:
```sql
SELECT serial_number, COUNT(*) AS count,
       STRING_AGG(DISTINCT issuer_dn, ' | ') AS issuers
FROM certificate
GROUP BY serial_number
HAVING COUNT(*) > 1
ORDER BY count DESC
LIMIT 5;
```

**Result**:
```
serial_number | count | issuers
--------------+-------+--------------------------------------------------
1             | 150   | C=US,O=...,CN=CSCA-USA | C=FR,O=...,CN=CSCA-FR | ...
2             | 120   | C=DE,O=...,CN=CSCA-DE | C=IT,O=...,CN=CSCA-IT | ...
```

**Impact**: DN collision when using `+sn={SERIAL}` as uniqueness depends on issuer context.

### 2. DN Escaping Complexity

**Issue**: Subject DN contains special characters that require RFC 4514 escaping.

**Special Characters**: `,` `+` `"` `\` `<` `>` `;` `=`

**Escaping Function** ([main.cpp:1853-1895](services/pkd-management/src/main.cpp#L1853-L1895)):
```cpp
std::string escapeDnComponent(const std::string& value) {
    std::string escaped;
    for (char c : value) {
        switch (c) {
            case ',': case '+': case '"': case '\\':
            case '<': case '>': case ';': case '=':
                escaped += '\\';
                escaped += c;
                break;
            default:
                escaped += c;
        }
    }
    return escaped;
}
```

**Problem**: Long and error-prone DN strings.

### 3. DN Length Limitation

**Issue**: LDAP DN length limit (typically 255 characters) can be exceeded.

**Example Subject DN**:
```
C=US,O=U.S. Department of State,OU=Passport Office,OU=Certification Authorities,CN=U.S. Department of State Public Passport Country Signing CA
```

**Escaped DN**: 200+ characters before adding `+sn=` and organization/country components.

### 4. Search Performance

**Issue**: LDAP searches on escaped DN components are inefficient.

**Current Search**:
```ldap
(&(cn=C\=KR\,O\=Government of Korea*)(objectClass=pkdDownload))
```

**Index**: LDAP cannot efficiently index on escaped DN prefixes.

---

## New DN Structure (Fingerprint-based)

### Design Principles

1. **Uniqueness**: SHA-256 fingerprint is cryptographically unique
2. **Simplicity**: No escaping required (hex string)
3. **Consistency**: Same format for all certificate types
4. **Backward Compatibility**: Support lookup by both fingerprint and Subject DN during migration

### Certificate DN Format (New)

```
cn={SHA256-FINGERPRINT},o={csca|dsc|dsc_nc},c={COUNTRY},dc={data|nc-data},dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
```

**Example**:
```
cn=0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b,o=csca,c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
```

**Length**: Fixed 64 hex characters (SHA-256) + organizational components = ~130 characters (well under 255 limit)

### Master List DN Format (No Change)

```
cn={SHA256-FINGERPRINT},o=ml,c={COUNTRY},dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
```

**Reason**: Already using fingerprint-based DN.

### CRL DN Format (No Change)

```
cn={SHA256-FINGERPRINT},o=crl,c={COUNTRY},dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
```

**Reason**: Already using fingerprint-based DN.

---

## LDAP Schema Design

### Attribute Additions

To support backward-compatible lookups, add indexed attributes for Subject DN and Serial Number:

```ldif
# Extended ICAO PKD schema
dn: cn=icao-pkd-ext,cn=schema,cn=config
objectClass: olcSchemaConfig
cn: icao-pkd-ext

# Subject DN attribute (for search)
olcAttributeTypes: ( 1.3.6.1.4.1.99999.1.1.1
  NAME 'pkdSubjectDN'
  DESC 'Certificate Subject Distinguished Name'
  EQUALITY caseIgnoreMatch
  SUBSTR caseIgnoreSubstringsMatch
  SYNTAX 1.3.6.1.4.1.1466.115.121.1.15
  SINGLE-VALUE )

# Serial Number attribute (for search)
olcAttributeTypes: ( 1.3.6.1.4.1.99999.1.1.2
  NAME 'pkdSerialNumber'
  DESC 'Certificate Serial Number'
  EQUALITY caseIgnoreMatch
  SYNTAX 1.3.6.1.4.1.1466.115.121.1.15
  SINGLE-VALUE )

# Fingerprint attribute (for uniqueness verification)
olcAttributeTypes: ( 1.3.6.1.4.1.99999.1.1.3
  NAME 'pkdFingerprint'
  DESC 'SHA-256 Fingerprint'
  EQUALITY caseIgnoreMatch
  SYNTAX 1.3.6.1.4.1.1466.115.121.1.15
  SINGLE-VALUE )

# Update pkdDownload objectClass
olcObjectClasses: ( 1.3.6.1.4.1.99999.1.2.1
  NAME 'pkdDownload'
  DESC 'ICAO PKD Certificate Entry'
  SUP top STRUCTURAL
  MUST ( cn $ pkdFingerprint )
  MAY ( userCertificate $ cACertificate $ pkdSubjectDN $ pkdSerialNumber $ c $ o $ ou ) )
```

### Index Configuration

```ldif
dn: olcDatabase={2}mdb,cn=config
changetype: modify
add: olcDbIndex
olcDbIndex: pkdFingerprint eq
olcDbIndex: pkdSubjectDN eq,sub
olcDbIndex: pkdSerialNumber eq
```

---

## Implementation Plan

### Day 1-2: Design & Preparation

#### Task 1.1: Finalize DN Schema Design ✅
- [x] Document current DN structure
- [x] Identify problems
- [x] Design new fingerprint-based DN
- [x] Define LDAP schema extensions

#### Task 1.2: Create Test Data Set
- [ ] Generate 100 test certificates with known serial number collisions
- [ ] Create Master Lists with CSCAs
- [ ] Prepare CRLs for testing

#### Task 1.3: Update Database Schema
- [ ] Add `ldap_dn_v2` column to `certificate`, `master_list`, `crl` tables
- [ ] Create migration tracking table

**SQL Migration**:
```sql
-- Add new DN column
ALTER TABLE certificate ADD COLUMN ldap_dn_v2 VARCHAR(512);
ALTER TABLE master_list ADD COLUMN ldap_dn_v2 VARCHAR(512);
ALTER TABLE crl ADD COLUMN ldap_dn_v2 VARCHAR(512);

-- Migration tracking
CREATE TABLE ldap_migration_status (
    id SERIAL PRIMARY KEY,
    table_name VARCHAR(50) NOT NULL,
    total_records INTEGER NOT NULL DEFAULT 0,
    migrated_records INTEGER NOT NULL DEFAULT 0,
    failed_records INTEGER NOT NULL DEFAULT 0,
    migration_start TIMESTAMP,
    migration_end TIMESTAMP,
    status VARCHAR(20) NOT NULL DEFAULT 'PENDING',
    error_log TEXT
);
```

### Day 3-4: Implementation

#### Task 2.1: Update DN Building Function

**File**: [services/pkd-management/src/main.cpp](services/pkd-management/src/main.cpp#L1897-L1909)

**Current Code**:
```cpp
std::string buildLdapDn(const std::string& subjectDn,
                        const std::string& serialNumber,
                        const std::string& certType,
                        const std::string& countryCode,
                        bool isNonConformant) {
    std::string escapedSubjectDn = escapeDnComponent(subjectDn);
    std::string ou = (certType == "CSCA") ? "csca" :
                     (certType == "DSC_NC" || isNonConformant) ? "dsc_nc" : "dsc";
    std::string dc = (certType == "DSC_NC" || isNonConformant) ? "nc-data" : "data";

    return "cn=" + escapedSubjectDn + "+sn=" + serialNumber + ",o=" + ou +
           ",c=" + countryCode + ",dc=" + dc + ",dc=download,dc=pkd," + baseDn;
}
```

**New Code**:
```cpp
std::string buildLdapDnV2(const std::string& fingerprint,
                          const std::string& certType,
                          const std::string& countryCode,
                          bool isNonConformant) {
    std::string ou = (certType == "CSCA") ? "csca" :
                     (certType == "DSC_NC" || isNonConformant) ? "dsc_nc" : "dsc";
    std::string dc = (certType == "DSC_NC" || isNonConformant) ? "nc-data" : "data";

    return "cn=" + fingerprint + ",o=" + ou + ",c=" + countryCode +
           ",dc=" + dc + ",dc=download,dc=pkd," + baseDn;
}
```

#### Task 2.2: Create Dual-Mode LDAP Operations

**Support both old and new DN formats during migration**:

```cpp
bool addCertificateToLdap(const std::string& fingerprint,
                          const std::string& subjectDn,
                          const std::string& serialNumber,
                          const std::string& certType,
                          const std::string& countryCode,
                          const std::vector<uint8_t>& certData,
                          bool isNonConformant,
                          bool useLegacyDn = false) {

    std::string dn = useLegacyDn ?
        buildLdapDn(subjectDn, serialNumber, certType, countryCode, isNonConformant) :
        buildLdapDnV2(fingerprint, certType, countryCode, isNonConformant);

    // Build LDAPMod array
    LDAPMod *mods[7];
    // ... (rest of LDAP add logic)
}
```

#### Task 2.3: Update Certificate Search

**File**: [services/pkd-management/src/repositories/ldap_certificate_repository.cpp](services/pkd-management/src/repositories/ldap_certificate_repository.cpp)

**Add Fingerprint-based Search**:
```cpp
std::vector<std::string> LdapCertificateRepository::getDnsByFingerprints(
    const std::vector<std::string>& fingerprints) {

    ensureConnected();

    // Build filter: (|(cn=fp1)(cn=fp2)...)
    std::string filter = "(|";
    for (const auto& fp : fingerprints) {
        filter += "(cn=" + fp + ")";
    }
    filter += ")";

    // Search across all organizational units
    std::string baseDn = "dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";

    // Execute search
    // ...
}
```

### Day 5-6: Migration Execution

#### Task 3.1: Dry-Run Migration Script

**File**: `scripts/ldap-dn-migration-dryrun.sh`

```bash
#!/bin/bash

set -e

POSTGRES_HOST=${DB_HOST:-postgres}
POSTGRES_DB=${DB_NAME:-pkd}
POSTGRES_USER=${DB_USER:-pkd}
POSTGRES_PASSWORD=${DB_PASSWORD:-pkd123}

echo "=== LDAP DN Migration - Dry Run ==="
echo "Timestamp: $(date)"

# 1. Count total records
echo "1. Counting total records..."
TOTAL_CERTS=$(psql -h $POSTGRES_HOST -U $POSTGRES_USER -d $POSTGRES_DB -tAc \
  "SELECT COUNT(*) FROM certificate WHERE ldap_stored = true")
echo "   Total certificates in LDAP: $TOTAL_CERTS"

# 2. Identify records with serial number collisions
echo "2. Identifying serial number collisions..."
psql -h $POSTGRES_HOST -U $POSTGRES_USER -d $POSTGRES_DB <<SQL
SELECT serial_number, COUNT(*) as collision_count,
       STRING_AGG(DISTINCT issuer_dn, ' | ') as issuers
FROM certificate
WHERE ldap_stored = true
GROUP BY serial_number
HAVING COUNT(*) > 1
ORDER BY collision_count DESC
LIMIT 10;
SQL

# 3. Generate new DNs (dry-run)
echo "3. Generating new DNs (not applied)..."
psql -h $POSTGRES_HOST -U $POSTGRES_USER -d $POSTGRES_DB <<SQL
SELECT
    id,
    fingerprint_sha256,
    certificate_type,
    country_code,
    CASE
        WHEN certificate_type = 'CSCA' THEN
            'cn=' || fingerprint_sha256 || ',o=csca,c=' || country_code || ',dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com'
        WHEN certificate_type = 'DSC_NC' THEN
            'cn=' || fingerprint_sha256 || ',o=dsc_nc,c=' || country_code || ',dc=nc-data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com'
        ELSE
            'cn=' || fingerprint_sha256 || ',o=dsc,c=' || country_code || ',dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com'
    END AS new_dn
FROM certificate
WHERE ldap_stored = true
LIMIT 10;
SQL

echo "=== Dry Run Complete ==="
```

#### Task 3.2: Live Migration Script

**File**: `scripts/ldap-dn-migration.sh`

```bash
#!/bin/bash

set -e

# Configuration
BATCH_SIZE=100
MIGRATION_MODE=${1:-"test"}  # test, production

echo "=== LDAP DN Migration - Live Mode ==="
echo "Mode: $MIGRATION_MODE"
echo "Batch Size: $BATCH_SIZE"
echo "Timestamp: $(date)"

# 1. Initialize migration tracking
psql -h $POSTGRES_HOST -U $POSTGRES_USER -d $POSTGRES_DB <<SQL
TRUNCATE ldap_migration_status;
INSERT INTO ldap_migration_status (table_name, total_records, status, migration_start)
SELECT
    'certificate',
    COUNT(*),
    'IN_PROGRESS',
    NOW()
FROM certificate
WHERE ldap_stored = true;
SQL

# 2. Batch migration
OFFSET=0
while true; do
    echo "Processing batch starting at offset $OFFSET..."

    # Get batch of certificates
    RESULT=$(psql -h $POSTGRES_HOST -U $POSTGRES_USER -d $POSTGRES_DB -tAc \
      "SELECT COUNT(*) FROM certificate WHERE ldap_stored = true OFFSET $OFFSET LIMIT $BATCH_SIZE")

    if [ "$RESULT" -eq 0 ]; then
        echo "Migration complete!"
        break
    fi

    # Call migration service (REST API)
    curl -X POST http://localhost:8081/api/internal/migrate-ldap-dns \
      -H "Content-Type: application/json" \
      -d "{\"offset\": $OFFSET, \"limit\": $BATCH_SIZE, \"mode\": \"$MIGRATION_MODE\"}"

    OFFSET=$((OFFSET + BATCH_SIZE))

    # Sleep between batches to avoid overwhelming LDAP
    sleep 2
done

# 3. Finalize migration status
psql -h $POSTGRES_HOST -U $POSTGRES_USER -d $POSTGRES_DB <<SQL
UPDATE ldap_migration_status
SET status = 'COMPLETED',
    migration_end = NOW()
WHERE table_name = 'certificate' AND status = 'IN_PROGRESS';
SQL

echo "=== Migration Complete ==="
```

#### Task 3.3: Migration Service Endpoint

**File**: [services/pkd-management/src/main.cpp](services/pkd-management/src/main.cpp)

**Add Internal API**:
```cpp
app().registerHandler(
    "/api/internal/migrate-ldap-dns",
    [](const HttpRequestPtr &req, std::function<void (const HttpResponsePtr &)> &&callback) {
        auto json = req->getJsonObject();
        int offset = (*json)["offset"].asInt();
        int limit = (*json)["limit"].asInt();
        std::string mode = (*json)["mode"].asString();

        // Execute migration batch
        int success = 0, failed = 0;
        std::vector<std::string> errors;

        try {
            // 1. Fetch certificates from DB
            std::string query =
                "SELECT id, fingerprint_sha256, certificate_type, country_code, "
                "       certificate_data, subject_dn, serial_number "
                "FROM certificate "
                "WHERE ldap_stored = true "
                "ORDER BY id "
                "OFFSET $1 LIMIT $2";

            const char* paramValues[2];
            std::string offsetStr = std::to_string(offset);
            std::string limitStr = std::to_string(limit);
            paramValues[0] = offsetStr.c_str();
            paramValues[1] = limitStr.c_str();

            PGresult* res = PQexecParams(conn, query.c_str(), 2, nullptr,
                                         paramValues, nullptr, nullptr, 0);

            if (PQresultStatus(res) != PGRES_TUPLES_OK) {
                throw std::runtime_error("DB query failed");
            }

            int rowCount = PQntuples(res);

            for (int i = 0; i < rowCount; i++) {
                std::string certId = PQgetvalue(res, i, 0);
                std::string fingerprint = PQgetvalue(res, i, 1);
                std::string certType = PQgetvalue(res, i, 2);
                std::string country = PQgetvalue(res, i, 3);
                // ... extract other fields

                // 2. Build new DN
                std::string newDn = buildLdapDnV2(fingerprint, certType, country, false);

                // 3. Add to LDAP with new DN
                bool ldapSuccess = false;
                if (mode == "production") {
                    ldapSuccess = addCertificateToLdap(
                        fingerprint, subjectDn, serialNumber, certType, country,
                        certData, false, false  // useLegacyDn = false
                    );
                } else {
                    // Test mode: just verify DN generation
                    ldapSuccess = true;
                }

                // 4. Update DB with new DN
                if (ldapSuccess) {
                    std::string updateQuery =
                        "UPDATE certificate SET ldap_dn_v2 = $1 WHERE id = $2";
                    const char* updateParams[2];
                    updateParams[0] = newDn.c_str();
                    updateParams[1] = certId.c_str();

                    PGresult* updateRes = PQexecParams(conn, updateQuery.c_str(),
                                                       2, nullptr, updateParams,
                                                       nullptr, nullptr, 0);

                    if (PQresultStatus(updateRes) == PGRES_COMMAND_OK) {
                        success++;
                    } else {
                        failed++;
                        errors.push_back(certId + ": DB update failed");
                    }
                    PQclear(updateRes);
                } else {
                    failed++;
                    errors.push_back(certId + ": LDAP add failed");
                }
            }

            PQclear(res);

        } catch (const std::exception& e) {
            Json::Value resp;
            resp["success"] = false;
            resp["error"] = e.what();
            callback(HttpResponse::newHttpJsonResponse(resp));
            return;
        }

        // Return results
        Json::Value resp;
        resp["success"] = true;
        resp["processed"] = success + failed;
        resp["success_count"] = success;
        resp["failed_count"] = failed;
        resp["errors"] = Json::Value(Json::arrayValue);
        for (const auto& err : errors) {
            resp["errors"].append(err);
        }

        callback(HttpResponse::newHttpJsonResponse(resp));
    },
    {Post}
);
```

### Day 7: Verification & Rollback Testing

#### Task 4.1: Verification Queries

```sql
-- 1. Check migration completeness
SELECT
    COUNT(*) AS total,
    COUNT(ldap_dn_v2) AS migrated,
    COUNT(*) - COUNT(ldap_dn_v2) AS remaining
FROM certificate
WHERE ldap_stored = true;

-- 2. Verify DN format
SELECT
    ldap_dn_v2,
    certificate_type,
    country_code
FROM certificate
WHERE ldap_dn_v2 IS NOT NULL
LIMIT 10;

-- 3. Check for DN duplicates (should be 0)
SELECT ldap_dn_v2, COUNT(*)
FROM certificate
WHERE ldap_dn_v2 IS NOT NULL
GROUP BY ldap_dn_v2
HAVING COUNT(*) > 1;
```

#### Task 4.2: LDAP Verification

```bash
# 1. Count entries with new DN format
ldapsearch -x -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w admin \
  -b "dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  "(cn=*)" cn | grep "^dn:" | wc -l

# 2. Verify specific certificate by fingerprint
FINGERPRINT="0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b"
ldapsearch -x -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w admin \
  -b "dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  "(cn=$FINGERPRINT)" \
  dn userCertificate
```

#### Task 4.3: Rollback Procedure

**File**: `scripts/ldap-dn-rollback.sh`

```bash
#!/bin/bash

set -e

echo "=== LDAP DN Migration Rollback ==="
echo "WARNING: This will delete all new DN entries from LDAP"
read -p "Are you sure? (yes/no): " confirm

if [ "$confirm" != "yes" ]; then
    echo "Rollback cancelled"
    exit 0
fi

# 1. Get list of new DNs from database
echo "Fetching new DNs..."
psql -h $POSTGRES_HOST -U $POSTGRES_USER -d $POSTGRES_DB -tAc \
  "SELECT ldap_dn_v2 FROM certificate WHERE ldap_dn_v2 IS NOT NULL" \
  > /tmp/new_dns.txt

# 2. Delete from LDAP
echo "Deleting entries from LDAP..."
while IFS= read -r dn; do
    ldapdelete -x -H ldap://openldap1:389 \
      -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
      -w admin \
      "$dn" || echo "Failed to delete: $dn"
done < /tmp/new_dns.txt

# 3. Clear ldap_dn_v2 column
echo "Clearing ldap_dn_v2 column..."
psql -h $POSTGRES_HOST -U $POSTGRES_USER -d $POSTGRES_DB <<SQL
UPDATE certificate SET ldap_dn_v2 = NULL;
UPDATE master_list SET ldap_dn_v2 = NULL;
UPDATE crl SET ldap_dn_v2 = NULL;
SQL

# 4. Reset migration status
psql -h $POSTGRES_HOST -U $POSTGRES_USER -d $POSTGRES_DB <<SQL
UPDATE ldap_migration_status
SET status = 'ROLLED_BACK',
    migration_end = NOW()
WHERE status = 'IN_PROGRESS' OR status = 'COMPLETED';
SQL

echo "=== Rollback Complete ==="
```

---

## Testing Strategy

### Unit Tests

**File**: `services/pkd-management/tests/ldap_dn_test.cpp`

```cpp
#include <gtest/gtest.h>
#include "../src/main_utils.h"

TEST(LdapDnTest, BuildDnV2_CSCA) {
    std::string fingerprint = "0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b";
    std::string dn = buildLdapDnV2(fingerprint, "CSCA", "KR", false);

    std::string expected = "cn=0a1b2c3d4e5f6a7b8c9d0e1f2a3b4c5d6e7f8a9b0c1d2e3f4a5b6c7d8e9f0a1b,"
                           "o=csca,c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";
    EXPECT_EQ(dn, expected);
}

TEST(LdapDnTest, BuildDnV2_DSC_NC) {
    std::string fingerprint = "1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef";
    std::string dn = buildLdapDnV2(fingerprint, "DSC", "US", true);

    std::string expected = "cn=1234567890abcdef1234567890abcdef1234567890abcdef1234567890abcdef,"
                           "o=dsc_nc,c=US,dc=nc-data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";
    EXPECT_EQ(dn, expected);
}

TEST(LdapDnTest, DnLength_UnderLimit) {
    std::string fingerprint = "a".repeat(64);  // Max SHA-256 length
    std::string dn = buildLdapDnV2(fingerprint, "CSCA", "XX", false);

    EXPECT_LT(dn.length(), 255);  // LDAP DN length limit
}
```

### Integration Tests

**File**: `services/pkd-management/tests/ldap_migration_test.cpp`

```cpp
TEST(LdapMigrationTest, MigrateBatch_Success) {
    // 1. Create test certificates in DB
    insertTestCertificate(conn, "test-cert-1", "fp1234...", "CSCA", "KR");

    // 2. Trigger migration
    auto resp = migrateLD apDnsBatch(0, 10, "test");

    // 3. Verify DB update
    auto cert = getCertificateById(conn, "test-cert-1");
    EXPECT_FALSE(cert.ldap_dn_v2.empty());
    EXPECT_TRUE(cert.ldap_dn_v2.starts_with("cn=fp1234"));

    // 4. Verify LDAP entry (in test mode, not actually added)
}

TEST(LdapMigrationTest, SerialNumberCollision_Resolved) {
    // 1. Create 2 certificates with same serial number
    insertTestCertificate(conn, "cert-a", "fp-a", "DSC", "US", "1");
    insertTestCertificate(conn, "cert-b", "fp-b", "DSC", "FR", "1");

    // 2. Migrate both
    migrateLD apDnsBatch(0, 10, "test");

    // 3. Verify unique DNs
    auto certA = getCertificateById(conn, "cert-a");
    auto certB = getCertificateById(conn, "cert-b");
    EXPECT_NE(certA.ldap_dn_v2, certB.ldap_dn_v2);
}
```

---

## Risk Assessment

### High Risk

**Risk**: LDAP replication lag causes inconsistency between openldap1 and openldap2.

**Mitigation**:
- Write all migrations to openldap1 (primary master) directly
- Verify replication status before marking migration complete
- Implement retry logic for replication failures

### Medium Risk

**Risk**: Migration script failure leaves database in inconsistent state.

**Mitigation**:
- Transaction-based batch processing
- Idempotent migration operations
- Rollback script tested before production

### Low Risk

**Risk**: Performance degradation during migration.

**Mitigation**:
- Run migration during low-traffic period
- Batch size limit (100 records)
- 2-second delay between batches

---

## Success Criteria

- [ ] All certificates have `ldap_dn_v2` populated
- [ ] No DN duplicates in LDAP
- [ ] Serial number collisions resolved (verified by search)
- [ ] DN length < 255 characters for all entries
- [ ] Migration rollback tested successfully
- [ ] Zero downtime during migration

---

## Next Steps

After Sprint 1 completion:
- **Sprint 2 (Week 6)**: Link Certificate Validation Core
- **Sprint 3 (Week 7)**: Integration with DSC validation pipeline
- **Sprint 3.5 (Week 7 - Parallel)**: Validation Detail Enhancement
- **Sprint 4 (Week 8)**: Testing & Deployment

---

**Document Status**: ✅ Complete
**Approved By**: [Pending]
**Implementation Start**: 2026-01-23
