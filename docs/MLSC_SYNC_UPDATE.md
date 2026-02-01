# MLSC Sync Support - DB-LDAP Synchronization Update

**Date**: 2026-01-26
**Version**: v2.1.0
**Status**: ✅ COMPLETED

---

## Overview

Updated the DB-LDAP sync functionality to properly track and display Master List Signer Certificates (MLSC) alongside CSCA, DSC, DSC_NC, and CRL types.

---

## Changes Made

### 1. Backend - PKD Relay Service (v2.1.0)

#### 1.1. Updated Data Structures ([types.h](../services/pkd-relay-service/src/relay/sync/common/types.h))

Added `mlscCount` fields to track MLSC certificates separately:

```cpp
struct DbStats {
    int cscaCount = 0;
    int mlscCount = 0;  // Master List Signer Certificates (Sprint 3)
    int dscCount = 0;
    int dscNcCount = 0;
    int crlCount = 0;
    int storedInLdapCount = 0;
    std::map<std::string, std::map<std::string, int>> countryStats;
};

struct LdapStats {
    int cscaCount = 0;
    int mlscCount = 0;  // Master List Signer Certificates (Sprint 3)
    int dscCount = 0;
    int dscNcCount = 0;
    int crlCount = 0;
    int totalEntries = 0;
    std::map<std::string, std::map<std::string, int>> countryStats;
};

struct SyncResult {
    std::string status;
    DbStats dbStats;
    LdapStats ldapStats;
    int cscaDiscrepancy = 0;
    int mlscDiscrepancy = 0;  // Master List Signer Certificates (Sprint 3)
    int dscDiscrepancy = 0;
    int dscNcDiscrepancy = 0;
    int crlDiscrepancy = 0;
    int totalDiscrepancy = 0;
    int checkDurationMs = 0;
    std::string errorMessage;
    int syncStatusId = 0;
};
```

#### 1.2. Updated Database Statistics ([main.cpp:129-171](../services/pkd-relay-service/src/main.cpp#L129-L171))

Modified `getDbStats()` to count MLSC from database:

```cpp
// Sprint 3: Get MLSC count (stored as CSCA but distinguished by ldap_dn_v2)
res = PQexec(conn.get(), "SELECT COUNT(*) FROM certificate WHERE ldap_dn_v2 LIKE '%o=mlsc%'");
if (PQresultStatus(res) == PGRES_TUPLES_OK && PQntuples(res) > 0) {
    stats.mlscCount = std::stoi(PQgetvalue(res, 0, 0));
}
PQclear(res);
```

#### 1.3. Updated LDAP Statistics ([main.cpp:458-490](../services/pkd-relay-service/src/main.cpp#L458-L490))

Modified `getLdapStats()` to count `o=mlsc` entries:

```cpp
if (dnStr.find("o=csca,") != std::string::npos) {
    stats.cscaCount++;
} else if (dnStr.find("o=mlsc,") != std::string::npos) {
    // Sprint 3: Count Master List Signer Certificates
    stats.mlscCount++;
} else if (dnStr.find("o=lc,") != std::string::npos) {
    // Sprint 3: Count Link Certificates as CSCA
    stats.cscaCount++;
}
```

#### 1.4. Updated Sync Status Persistence

- **saveSyncStatus()**: Added MLSC columns to INSERT query ([main.cpp:228-262](../services/pkd-relay-service/src/main.cpp#L228-L262))
- **getLatestSyncStatus()**: Added MLSC to SELECT query and JSON response ([main.cpp:285-336](../services/pkd-relay-service/src/main.cpp#L285-L336))
- **handleSyncCheck()**: Added MLSC to JSON response ([main.cpp:1032-1052](../services/pkd-relay-service/src/main.cpp#L1032-L1052))
- **performSyncCheck()**: Added MLSC discrepancy calculation ([main.cpp:538-546](../services/pkd-relay-service/src/main.cpp#L538-L546))

### 2. Database Migration

Created migration script: [add_mlsc_to_sync_status.sql](../docker/db/migrations/add_mlsc_to_sync_status.sql)

```sql
ALTER TABLE sync_status
ADD COLUMN IF NOT EXISTS db_mlsc_count INTEGER DEFAULT 0,
ADD COLUMN IF NOT EXISTS ldap_mlsc_count INTEGER DEFAULT 0,
ADD COLUMN IF NOT EXISTS mlsc_discrepancy INTEGER DEFAULT 0;

UPDATE sync_status
SET
    db_mlsc_count = 0,
    ldap_mlsc_count = 0,
    mlsc_discrepancy = 0
WHERE db_mlsc_count IS NULL;
```

### 3. Frontend - React TypeScript

#### 3.1. Updated TypeScript Types ([types/index.ts:371-386](../frontend/src/types/index.ts#L371-L386))

```typescript
export interface SyncStats {
  csca: number;
  mlsc: number;  // Master List Signer Certificates (Sprint 3)
  dsc: number;
  dscNc: number;
  crl: number;
  total?: number;
  storedInLdap?: number;
}

export interface SyncDiscrepancy {
  csca: number;
  mlsc: number;  // Master List Signer Certificates (Sprint 3)
  dsc: number;
  dscNc: number;
  crl: number;
  total: number;
}
```

#### 3.2. Updated SyncDashboard Component ([SyncDashboard.tsx](../frontend/src/pages/SyncDashboard.tsx))

Added MLSC row to DB ↔ LDAP comparison table:

```tsx
<tr className="border-b border-gray-100 dark:border-gray-700/50">
  <td className="py-2 px-3 text-gray-700 dark:text-gray-300">MLSC</td>
  <td className="py-2 px-3 text-right font-mono font-semibold text-gray-900 dark:text-white">
    {status.dbStats.mlsc?.toLocaleString()}
  </td>
  <td className="py-2 px-3 text-right font-mono font-semibold text-gray-900 dark:text-white">
    {status.ldapStats.mlsc?.toLocaleString()}
  </td>
  <td className="py-2 px-3 text-right">
    {status.discrepancy && status.discrepancy.mlsc !== 0 ? (
      <span className={cn(
        'font-mono font-semibold',
        status.discrepancy.mlsc > 0 ? 'text-red-600 dark:text-red-400' : 'text-blue-600 dark:text-blue-400'
      )}>
        {status.discrepancy.mlsc > 0 ? '+' : ''}{status.discrepancy.mlsc}
      </span>
    ) : (
      <span className="text-green-600 dark:text-green-400">✓</span>
    )}
  </td>
</tr>
```

---

## API Response Example

**GET /api/sync/status**

```json
{
  "id": 2,
  "checkedAt": "2026-01-26 23:31:40.784616",
  "dbStats": {
    "csca": 536,
    "mlsc": 59,
    "dsc": 0,
    "dscNc": 0,
    "crl": 0,
    "storedInLdap": 536
  },
  "ldapStats": {
    "csca": 477,
    "mlsc": 59,
    "dsc": 0,
    "dscNc": 0,
    "crl": 0,
    "total": 536
  },
  "discrepancy": {
    "csca": 59,
    "mlsc": 0,
    "dsc": 0,
    "dscNc": 0,
    "crl": 0,
    "total": 59
  },
  "status": "DISCREPANCY",
  "checkDurationMs": 50
}
```

**Note**: The CSCA discrepancy of 59 is expected because MLSC certificates are stored in the database as `certificate_type='CSCA'` but in LDAP as `o=mlsc`. The MLSC count is tracked separately and shows 0 discrepancy.

---

## Verification

### 1. Database Migration

```bash
docker exec -i icao-local-pkd-postgres psql -U pkd -d localpkd < docker/db/migrations/add_mlsc_to_sync_status.sql
```

**Output**:
```
ALTER TABLE
UPDATE 0
COMMENT
COMMENT
COMMENT
   column_name    | data_type | column_default | is_nullable
------------------+-----------+----------------+-------------
 db_mlsc_count    | integer   | 0              | YES
 ldap_mlsc_count  | integer   | 0              | YES
 mlsc_discrepancy | integer   | 0              | YES
```

### 2. Service Logs

```bash
docker logs icao-local-pkd-relay 2>&1 | grep "ICAO Local PKD"
```

**Output**:
```
[2026-01-26 23:31:27.991] [info] [1]   ICAO Local PKD - PKD Relay Service v2.1.0
```

```bash
docker logs icao-local-pkd-relay 2>&1 | grep -E "(DB stats|LDAP stats)"
```

**Output**:
```
[2026-01-26 23:31:40.743] [info] [8] DB stats - CSCA: 536, MLSC: 59, DSC: 0, DSC_NC: 0, CRL: 0
[2026-01-26 23:31:40.776] [info] [8] LDAP stats - CSCA: 477, MLSC: 59, DSC: 0, DSC_NC: 0, CRL: 0
```

### 3. Sync Status API

```bash
curl -s http://localhost:8080/api/sync/status | jq '.dbStats, .ldapStats, .discrepancy'
```

**Output**:
```json
{
  "crl": 0,
  "csca": 536,
  "dsc": 0,
  "dscNc": 0,
  "mlsc": 59,
  "storedInLdap": 536
}
{
  "crl": 0,
  "csca": 477,
  "dsc": 0,
  "dscNc": 0,
  "mlsc": 59,
  "total": 536
}
{
  "crl": 0,
  "csca": 59,
  "dsc": 0,
  "dscNc": 0,
  "mlsc": 0,
  "total": 59
}
```

### 4. Frontend Sync Dashboard

Navigate to http://localhost:3000/sync and verify:

- ✅ MLSC row appears in DB ↔ LDAP comparison table
- ✅ DB MLSC count: 59
- ✅ LDAP MLSC count: 59
- ✅ MLSC discrepancy: 0 (shown with green ✓)
- ✅ MLSC appears in discrepancy details when expanded
- ✅ MLSC card shows in discrepancy summary section (6 cards total)

---

## Files Modified

### Backend
1. [services/pkd-relay-service/src/relay/sync/common/types.h](../services/pkd-relay-service/src/relay/sync/common/types.h)
2. [services/pkd-relay-service/src/main.cpp](../services/pkd-relay-service/src/main.cpp)

### Database
3. [docker/db/migrations/add_mlsc_to_sync_status.sql](../docker/db/migrations/add_mlsc_to_sync_status.sql) (NEW)

### Frontend
4. [frontend/src/types/index.ts](../frontend/src/types/index.ts)
5. [frontend/src/pages/SyncDashboard.tsx](../frontend/src/pages/SyncDashboard.tsx)

---

## Related Documents

- [MLSC_BUILDDN_FIX.md](MLSC_BUILDDN_FIX.md) - MLSC buildCertificateDnV2 bug fix
- [MASTER_LIST_SIGNER_CLASSIFICATION_FIX.md](MASTER_LIST_SIGNER_CLASSIFICATION_FIX.md) - Original MLSC analysis
- [COLLECTION_002_FINGERPRINT_DN_FIX.md](COLLECTION_002_FINGERPRINT_DN_FIX.md) - Fingerprint DN implementation
- [CLAUDE.md](../CLAUDE.md) - Project development guide

---

## Summary

The DB-LDAP sync functionality now properly tracks and displays MLSC (Master List Signer Certificate) counts alongside other certificate types. This provides complete visibility into all certificate types stored in the system:

- **CSCA**: Self-signed root certificates (excluding MLSC)
- **MLSC**: Master List Signer Certificates (Sprint 3)
- **DSC**: Document Signer Certificates
- **DSC_NC**: Non-conformant DSC
- **CRL**: Certificate Revocation Lists

All statistics, discrepancies, and sync status are now properly calculated and displayed for MLSC certificates.

---

## Important Fix: CSCA Counting

**Issue**: Initial implementation caused a false discrepancy of +59 in CSCA count.

**Root Cause**: MLSC certificates are stored in the database as `certificate_type='CSCA'` but in LDAP as `o=mlsc`. The database query was counting all CSCA records (536 total) which included MLSC (59), while LDAP only counted `o=csca` entries (477), creating a false discrepancy.

**Solution**: Modified `getDbStats()` to exclude MLSC when counting CSCA:

```cpp
// Before: Counted all CSCA (including MLSC) = 536
SELECT COUNT(*) FROM certificate WHERE certificate_type = 'CSCA'

// After: Excludes MLSC = 477
SELECT COUNT(*) FROM certificate
WHERE certificate_type = 'CSCA'
  AND (ldap_dn_v2 NOT LIKE '%o=mlsc%' OR ldap_dn_v2 IS NULL)
```

**Result**: All discrepancies resolved - status changed from `DISCREPANCY` to `SYNCED` ✓
