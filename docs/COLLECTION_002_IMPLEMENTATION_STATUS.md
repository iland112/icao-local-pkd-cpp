# Collection 002 CSCA Extraction - Implementation Status

**Version**: 2.0.0
**Date**: 2026-01-23
**Status**: Phase 6 Complete - Ready for Testing

---

## Overview

This document tracks the implementation status of Collection 002 Master List processing changes, which extract individual CSCAs from each Master List CMS entry instead of storing the entire CMS without parsing.

---

## Background

### Problem

**DB vs LDAP CSCA Discrepancy:**
- Database: 536 CSCAs (all from ICAO Master List `.ml` file)
- LDAP: 105 CSCAs (78 in `o=csca` + 27 in `o=ml`)
- Missing: ~450 CSCAs from Collection 002 LDIF entries

**Root Cause:**
- Collection 002 LDIF has 27 Master List entries
- Each entry contains a country-specific Master List CMS with ~15-20 CSCAs
- Current code (`parseMasterListEntry()` in main.cpp:2892) only:
  - Counts CSCAs: `cscaCount = sk_X509_num(certs);`
  - Saves entire CMS to `o=ml` LDAP branch
  - **Does NOT extract individual CSCAs**

### Solution (v2.0.0)

Change Collection 002 processing to:

1. **Extract Individual CSCAs**: Parse CMS and loop through `sk_X509_value(certs, i)`
2. **Primary Storage**: Save CSCAs to `o=csca` (included in statistics)
3. **Backup Storage**: Save original Master List CMS to `o=ml` (excluded from statistics)
4. **Duplicate Detection**: Check by `fingerprint_sha256`, track all sources
5. **Comprehensive Logging**: Log each CSCA with NEW/DUPLICATE status

Expected result: LDAP CSCA count will increase from 78 to ~500 (78 + 450 new CSCAs)

---

## Implementation Checklist

### Phase 1: Database Schema ✅ COMPLETE

**Files Created:**
- ✅ `docker/init-scripts/005_certificate_duplicates.sql`

**Schema Changes:**

1. ✅ **New Table: `certificate_duplicates`**
   - Tracks all sources (ML_FILE, LDIF_001, LDIF_002, LDIF_003)
   - Columns: certificate_id, upload_id, source_type, source_country, source_entry_dn, source_file_name, detected_at
   - Unique constraint: (certificate_id, upload_id, source_type)
   - 4 indexes for performance

2. ✅ **ALTER `certificate` Table**
   - Added columns: duplicate_count, first_upload_id, last_seen_upload_id, last_seen_at
   - 3 indexes for new columns

3. ✅ **ALTER `uploaded_file` Table**
   - Added columns: csca_extracted_from_ml, csca_duplicates

4. ✅ **View: `certificate_duplicate_stats`**
   - Comprehensive duplicate statistics with all source information
   - JSON aggregation of all sources

5. ✅ **Helper Function: `get_duplicate_summary_by_source()`**
   - Returns duplicate statistics grouped by source type

**Deployment Status:**
- ⏳ **Not Yet Applied** - Migration script ready, awaiting execution
- Test on local PostgreSQL first
- Then deploy to Luckfox production

---

### Phase 2: Utility Functions ✅ COMPLETE

**Files Created:**
- ✅ `services/pkd-management/src/common/certificate_utils.h`
- ✅ `services/pkd-management/src/common/certificate_utils.cpp`

**Functions Implemented:**

1. ✅ **`saveCertificateWithDuplicateCheck()`**
   - Returns: `std::pair<int, bool>` (certificate_id, isDuplicate)
   - Uses parameterized SQL queries (SQL injection safe)
   - Checks duplicate by (certificate_type, fingerprint_sha256)
   - If duplicate: Returns existing ID
   - If new: Inserts and returns new ID with first_upload_id set

2. ✅ **`trackCertificateDuplicate()`**
   - Records source in certificate_duplicates table
   - Parameterized SQL with ON CONFLICT DO NOTHING
   - Tracks: certificate_id, upload_id, source_type, source_country, source_entry_dn, source_file_name

3. ✅ **`incrementDuplicateCount()`**
   - Updates: duplicate_count++, last_seen_upload_id, last_seen_at
   - Parameterized SQL

4. ✅ **`updateCscaExtractionStats()`**
   - Updates uploaded_file: csca_extracted_from_ml, csca_duplicates
   - Parameterized SQL

5. ✅ **`getSourceType()`**
   - Converts file format to source type identifier
   - LDIF_001/002/003 → LDIF_001/002/003
   - MASTERLIST → ML_FILE

**Build Integration:**
- ✅ CMakeLists.txt updated to include certificate_utils.cpp

---

### Phase 3: Master List Processor ✅ COMPLETE

**Files Created:**
- ✅ `services/pkd-management/src/common/masterlist_processor.h`
- ✅ `services/pkd-management/src/common/masterlist_processor.cpp`

**New Structures:**

1. ✅ **`MasterListStats` Structure**
   - Counters: mlCount, ldapMlStoredCount, cscaExtractedCount, cscaDuplicateCount, cscaNewCount, ldapCscaStoredCount

2. ✅ **`CertificateMetadata` Structure** (internal)
   - Fields: subjectDn, issuerDn, serialNumber, fingerprint, notBefore, notAfter, derData

**New Function:**

1. ✅ **`parseMasterListEntryV2()`**
   - Signature: `bool parseMasterListEntryV2(PGconn* conn, LDAP* ld, const std::string& uploadId, const LdifEntry& entry, MasterListStats& stats)`
   - Processing Steps:
     1. Parse pkdMasterListContent CMS structure
     2. Extract each CSCA certificate (loop sk_X509_value)
     3. For each CSCA:
        - Extract metadata (Subject DN, Issuer DN, Serial, Fingerprint, Validity, DER)
        - Call `saveCertificateWithDuplicateCheck()`
        - Call `trackCertificateDuplicate()` for all (NEW + DUPLICATE)
        - If DUPLICATE: Call `incrementDuplicateCount()`, skip LDAP
        - If NEW: Save to LDAP `o=csca`, increment ldapCscaStoredCount
        - Log detailed status
     4. Save original Master List CMS to `o=ml` (backup)
     5. Update statistics with `updateCscaExtractionStats()`

**Logging Format:**
```
[ML] CSCA 1/12 - NEW - fingerprint: abc123..., cert_id: 42, subject: C=KR,O=MOFA,CN=CSCA-KOREA
[ML] CSCA 2/12 - DUPLICATE - fingerprint: def456..., cert_id: 100, subject: C=KR,O=MOFA,CN=CSCA-KOREA
[ML] Extracted 12 CSCAs: 3 new, 9 duplicates
[ML] Saved Master List to LDAP o=ml: cn=abc123,o=ml,c=KR,dc=data,dc=download,dc=pkd,...
```

**Build Integration:**
- ✅ CMakeLists.txt updated to include masterlist_processor.cpp

---

### Phase 4: Main.cpp Integration ✅ COMPLETE

**Changes Completed in `main.cpp`:**

1. ✅ **Added includes:**
   ```cpp
   #include "common/certificate_utils.h"
   #include "common/masterlist_processor.h"
   ```

2. ✅ **Updated Collection 002 Processing:**
   - Located: `processLdifFileAsync()` function (line 3199-3206)
   - Replaced: `parseMasterListEntry()` → `parseMasterListEntryV2()`
   - Added: `MasterListStats mlStats;` structure for comprehensive tracking
   - Updated: Legacy counters for backward compatibility:
     ```cpp
     mlCount = mlStats.mlCount;
     ldapMlStoredCount = mlStats.ldapMlStoredCount;
     cscaCount += mlStats.cscaNewCount;  // Add new CSCAs
     ldapCertStoredCount += mlStats.ldapCscaStoredCount;
     ```

3. ✅ **Statistics are automatically updated:**
   - `updateCscaExtractionStats()` called within `parseMasterListEntryV2()`
   - No separate call needed in main.cpp

4. ✅ **Deprecated Old Function:**
   - Kept: `parseMasterListEntry()` for backward compatibility
   - Marked: `[[deprecated("Use parseMasterListEntryV2() from masterlist_processor.h instead")]]`
   - Added: Comprehensive deprecation comment
   - Future: Remove after v2.0.0 is stable in production

**External Function Declarations:**

✅ **Created `src/common/main_utils.h`** with all external functions:
- `base64Decode()` - Base64 decoding
- `computeFileHash()` - SHA-256 hash computation
- `extractCountryCodeFromDn()` - Country code extraction from DN
- `saveMasterList()` - Master List DB storage
- `saveMasterListToLdap()` - Master List LDAP storage
- `updateMasterListLdapStatus()` - LDAP status update
- `saveCertificateToLdap()` - Certificate LDAP storage

**Security Enhancements:**
- ✅ All function signatures documented with security notes
- ✅ Thread-safety status clearly marked
- ✅ Parameter validation requirements specified
- ✅ Warning annotations for sensitive operations

---

### Phase 5: PKD Relay Service Update ✅ VERIFIED

**File Verified:**
- `services/pkd-relay-service/src/main.cpp` (Lines 377-464)

**Analysis Result:**

Current `getLdapStats()` function already correctly excludes `o=ml`:

```cpp
if (dnStr.find("o=csca,") != std::string::npos) {
    stats.cscaCount++;
} else if (dnStr.find("o=dsc,") != std::string::npos) {
    stats.dscCount++;
} else if (dnStr.find("o=crl,") != std::string::npos) {
    stats.crlCount++;
}
// o=ml entries naturally excluded (no match)
```

**No code change needed** ✅ - `o=ml` is naturally excluded from statistics.

**Verification Status:**
- ✅ Confirmed o=ml entries are NOT counted in statistics
- ✅ Logic works as designed
- ⏳ Final test with actual Collection 002 data pending

---

### Phase 6: Frontend Update ✅ COMPLETE

**Date Completed**: 2026-01-23

**Files Modified:**
- ✅ `frontend/src/types/index.ts` - TypeScript type definitions (+6 lines)
- ✅ `frontend/src/pages/UploadDashboard.tsx` - Dashboard statistics section (+63 lines)
- ✅ `frontend/src/pages/UploadHistory.tsx` - Detail dialog section (+35 lines)

**Changes Implemented:**

1. ✅ **TypeScript Types:**
   - Added `cscaExtractedFromMl?: number` to `UploadedFile` interface
   - Added `cscaDuplicates?: number` to `UploadedFile` interface
   - Added same fields to `UploadStatisticsOverview` interface

2. ✅ **Upload Dashboard Statistics:**
   - New section: "Collection 002 CSCA 추출 통계"
   - Conditional rendering (only if data exists)
   - 3-column grid: 추출된 CSCA, 중복, 신규율
   - Indigo gradient background with version badge (v2.0.0)
   - Dark mode support

3. ✅ **Upload History Detail Dialog:**
   - New section: "Collection 002 CSCA 추출"
   - Conditional rendering (only if data exists)
   - Compact 3-column grid: 추출됨, 중복, 신규 %
   - Indigo border with version badge (v2.0.0)
   - Dark mode support

**Frontend Build:**
- ✅ Successfully built: `index-BX_gbcND.js` (2,185.41 kB, gzipped: 656.95 kB)
- ✅ Docker image: `docker-frontend:latest` (d896c690c3de)
- ✅ Container deployed: `icao-local-pkd-frontend`

**Documentation:**
- ✅ `docs/COLLECTION_002_PHASE_6_FRONTEND_UPDATE.md` - Complete implementation guide

**Features:**
- ✅ Backward compatible (optional fields, conditional rendering)
- ✅ Responsive design (mobile/tablet/desktop)
- ✅ Calculated metrics (duplicate rate, new rate)
- ✅ Visual consistency with existing UI

**Optional Features (Not Implemented):**
- ⏳ New Page: Duplicate Analysis
- ⏳ API Endpoint: `GET /api/certificates/duplicates`
  (Can be added in future if needed)
- `GET /api/certificates/duplicates/{certificateId}`

---

### Phase 7: Testing ⏳ PENDING

**Test Plan:**

1. ⏳ **Local Testing (Docker)**
   - Apply migration: `005_certificate_duplicates.sql`
   - Rebuild pkd-management with new code
   - Upload Collection 002 LDIF file
   - Verify:
     - CSCAs extracted and stored in `o=csca`
     - Duplicates detected correctly
     - Statistics match expected counts
     - Logs show NEW/DUPLICATE status

2. ⏳ **LDAP Verification**
   - Count `o=csca` entries: Should increase from 78 to ~500
   - Count `o=ml` entries: Should remain 27
   - Query `certificate_duplicates` table
   - Check duplicate patterns

3. ⏳ **Performance Testing**
   - Measure processing time for Collection 002 (27 entries × ~15 CSCAs = ~400 operations)
   - Expected: 2-5 seconds total (with duplicate checks)

4. ⏳ **Duplicate Analysis Queries**
   ```sql
   -- Find CSCAs in both ML_FILE and LDIF_002
   SELECT * FROM certificate_duplicate_stats
   WHERE unique_source_count >= 2
   ORDER BY duplicate_count DESC
   LIMIT 20;

   -- Get duplicate summary
   SELECT * FROM get_duplicate_summary_by_source();

   -- Expected duplicate rate: 80-90% overlap
   ```

5. ⏳ **Sync Status Verification**
   - Trigger sync check: `POST /api/sync/trigger`
   - Verify DB and LDAP counts match
   - Verify `o=ml` excluded from statistics

---

### Phase 8: Deployment ⏳ PENDING

**Deployment Steps:**

1. ⏳ **Local System**
   - Apply migration SQL
   - Test thoroughly
   - Document any issues

2. ⏳ **Luckfox Production**
   - Schedule maintenance window
   - Backup database and LDAP
   - Apply migration
   - Deploy new pkd-management binary
   - Re-upload Collection 002 LDIF
   - Verify statistics

3. ⏳ **Post-Deployment Verification**
   - Check logs for errors
   - Verify CSCA count matches expected
   - Run duplicate analysis queries
   - Monitor performance

---

## Expected Results

### Before (v1.x)

| Data Source | CSCA Count | Storage Location |
|-------------|------------|------------------|
| ICAO Master List (.ml) | 536 | DB + LDAP o=csca |
| Collection 001 LDIF | 0 | N/A |
| Collection 002 LDIF | 0 (not extracted) | LDAP o=ml only |
| **Total in o=csca** | **78** | **LDAP** |

**Issue**: Collection 002 has ~450 CSCAs but they're not extracted, causing DB-LDAP discrepancy.

### After (v2.0.0)

| Data Source | CSCA Count | Storage Location |
|-------------|------------|------------------|
| ICAO Master List (.ml) | 536 | DB + LDAP o=csca |
| Collection 001 LDIF | 0 | N/A |
| Collection 002 LDIF | ~450 (extracted) | DB + LDAP o=csca |
| **Total Unique CSCAs** | **~500** | **DB + LDAP o=csca** |
| **Duplicates** | **~400** | **Tracked in certificate_duplicates** |

**Expected Duplicate Rate**: 80-90% (most Collection 002 CSCAs already exist from .ml file)

**LDAP Structure:**
- `o=csca`: ~500 unique CSCAs (Primary, included in stats)
- `o=ml`: 27 Master List backups (Excluded from stats)

---

## Known Issues and Risks

### Issue 1: External Function Dependencies

**Problem**: `masterlist_processor.cpp` depends on functions in `main.cpp` via `extern` declarations.

**Risk**: Linkage errors if function signatures change.

**Mitigation**: Create `src/common/main_utils.h` header with all external function declarations.

**Status**: ⏳ Pending

---

### Issue 2: First Build May Be Slow

**Problem**: New vcpkg dependencies or header changes may invalidate build cache.

**Risk**: CI/CD build time increases from 10-15 minutes to 60+ minutes.

**Mitigation**:
- Update BUILD_ID in Dockerfile
- Monitor GitHub Actions build time
- Use build freshness check script

**Status**: ⏳ To be monitored

---

### Issue 3: Large Number of Duplicate Inserts

**Problem**: Processing Collection 002 after .ml file will create ~400 duplicate entries.

**Risk**: Slow processing, large certificate_duplicates table.

**Mitigation**:
- Batch processing (already implemented in main.cpp)
- Index optimization (already in migration SQL)
- Monitor processing time during testing

**Status**: ⏳ To be tested

---

## Dependencies

### Code Dependencies

- ✅ `certificate_utils.h/cpp` - Utility functions for duplicate handling
- ✅ `masterlist_processor.h/cpp` - Refactored Master List processing
- ⏳ `main_utils.h` (to be created) - External function declarations

### Database Dependencies

- ✅ Migration script ready: `005_certificate_duplicates.sql`
- ⏳ Migration not yet applied

### External Libraries

- OpenSSL (CMS, X509, BIO)
- libpq (PostgreSQL)
- LDAP (OpenLDAP)

All already in use, no new dependencies.

---

## Files Modified/Created

### Created Files ✅

1. ✅ `docker/init-scripts/005_certificate_duplicates.sql` (Migration)
2. ✅ `services/pkd-management/src/common/certificate_utils.h`
3. ✅ `services/pkd-management/src/common/certificate_utils.cpp`
4. ✅ `services/pkd-management/src/common/masterlist_processor.h`
5. ✅ `services/pkd-management/src/common/masterlist_processor.cpp`
6. ✅ `docs/COLLECTION_002_IMPLEMENTATION_STATUS.md` (This file)

### Modified Files ✅

1. ✅ `services/pkd-management/CMakeLists.txt` (Added new sources)

### Files to Modify ⏳

1. ⏳ `services/pkd-management/src/main.cpp` (Integration)
2. ⏳ `frontend/src/pages/UploadDashboard.tsx` (Statistics display)
3. ⏳ `frontend/src/pages/UploadDetail.tsx` (Extraction stats)

---

## Next Steps

### Immediate (Today)

1. ✅ Create migration SQL script
2. ✅ Implement utility functions
3. ✅ Implement Master List processor
4. ✅ Update CMakeLists.txt
5. ✅ Document implementation status

### Short Term (This Week)

1. ⏳ Create `src/common/main_utils.h` for external declarations
2. ⏳ Integrate `parseMasterListEntryV2()` into main.cpp
3. ⏳ Apply migration SQL to local database
4. ⏳ Build and test locally
5. ⏳ Fix any compilation errors

### Medium Term (Next Week)

1. ⏳ Upload Collection 002 LDIF for testing
2. ⏳ Verify CSCA extraction works correctly
3. ⏳ Analyze duplicate patterns
4. ⏳ Update frontend to display new statistics
5. ⏳ Performance testing

### Long Term (Future)

1. ⏳ Deploy to Luckfox production
2. ⏳ Monitor performance in production
3. ⏳ Implement duplicate analysis UI (optional)
4. ⏳ Remove deprecated `parseMasterListEntry()` function

---

## Completion Status

**Overall Progress**: 75% Complete ✅

| Phase | Status | Progress |
|-------|--------|----------|
| Phase 1: Database Schema | ✅ Complete | 100% |
| Phase 2: Utility Functions | ✅ Complete | 100% |
| Phase 3: Master List Processor | ✅ Complete | 100% |
| Phase 4: Main.cpp Integration | ✅ Complete | 100% |
| Phase 5: PKD Relay Service | ✅ Verified | 100% |
| Phase 6: Frontend Update | ✅ Complete | 100% |
| Phase 7: Testing | ⏳ Pending | 0% |
| Phase 8: Deployment | ⏳ Pending | 0% |

**Build Status**:
- ✅ Backend Docker image successfully built (v2.0.0)
- ✅ Frontend Docker image successfully built (index-BX_gbcND.js)

**Overall Completion**: 75% (6/8 phases)

**Ready for**: Database migration and functional testing

---

## References

- [COLLECTION_002_CSCA_EXTRACTION.md](COLLECTION_002_CSCA_EXTRACTION.md) - Detailed implementation plan
- [DATA_PROCESSING_RULES.md](DATA_PROCESSING_RULES.md) - Updated data processing rules (v2.0.0)
- [CLAUDE.md](../CLAUDE.md) - Project overview

---

**Last Updated**: 2026-01-23
**Next Review**: After Phase 4 completion
