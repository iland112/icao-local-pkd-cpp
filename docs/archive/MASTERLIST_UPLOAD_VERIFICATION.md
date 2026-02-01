# Master List Upload Verification Guide

**Document Version**: 1.0.0
**Last Updated**: 2026-01-25
**Purpose**: Master List 업로드 처리 검증 절차 및 결과 분석

---

## 1. Overview

Master List는 ICAO PKD에서 제공하는 CSCA (Country Signing CA) 인증서 목록입니다. PKCS#7 CMS 형식으로 제공되며, 각 국가의 CSCA 인증서 및 Link Certificates를 포함합니다.

### 1.1 Master List 특징

- **파일 형식**: PKCS#7 CMS (`.ml` 확장자)
- **포함 인증서**: CSCA Only (self-signed + cross-signed link certificates)
- **용도**: DSC Trust Chain Validation의 root anchor
- **업데이트 주기**: ICAO PKD에서 주기적으로 갱신 (월별/분기별)

---

## 2. Upload Processing Flow

### 2.1 Backend Processing (AUTO Mode)

```
1. File Upload
   ↓
2. PKCS#7 CMS Parsing (OpenSSL CMS API)
   ↓
3. Certificate Extraction (536 certificates)
   ↓
4. Link Certificate Detection (60 link certificates)
   ↓
5. Database Storage (certificate table)
   ↓
6. LDAP Synchronization (pkdDownload entries)
   ↓
7. CSCA Cache Reinitialization
   ↓
8. Upload Status Update (COMPLETED)
```

### 2.2 Processing Time

```
Total Duration: ~3.4 seconds
- Parsing: ~0.4s
- DB Saving: ~2.7s
- LDAP Sync: ~0.3s
- Cache Reinit: ~0.073s
```

---

## 3. Verification Procedure

### 3.1 Upload Record Verification

```sql
-- Check upload record
SELECT
    id,
    file_name,
    file_format,
    file_size,
    status,
    processing_mode,
    total_entries,
    csca_count,
    upload_timestamp
FROM uploaded_file
WHERE file_format = 'ML'
ORDER BY upload_timestamp DESC
LIMIT 1;
```

**Expected Result**:
- `status`: COMPLETED
- `total_entries`: 536
- `csca_count`: 536

### 3.2 Database Certificate Count

```sql
-- Count certificates by type
SELECT
    certificate_type,
    COUNT(*) as total,
    COUNT(*) FILTER (WHERE subject_dn = issuer_dn) as self_signed,
    COUNT(*) FILTER (WHERE subject_dn != issuer_dn) as link_certs
FROM certificate
WHERE certificate_type = 'CSCA'
GROUP BY certificate_type;
```

**Expected Result**:
- Total: 536
- Self-signed: ~476 (88.8%)
- Link certificates: ~60 (11.2%)

### 3.3 Top Countries Verification

```sql
-- Top 10 countries by CSCA count
SELECT
    country_code,
    COUNT(*) as count
FROM certificate
WHERE certificate_type = 'CSCA'
GROUP BY country_code
ORDER BY count DESC
LIMIT 10;
```

**Expected Top Countries**:
1. CN (China): 34
2. HU (Hungary): 21
3. LV (Latvia): 16
4. NL (Netherlands): 15
5. NZ/DE: 13

### 3.4 LDAP Storage Verification

```bash
# LDAP connection test
docker exec icao-local-pkd-openldap1 \
  ldapsearch -x -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w "ldap_test_password_123" \
  -b "dc=ldap,dc=smartcoreinc,dc=com" \
  -s base namingContexts

# Count total CSCA entries
docker exec icao-local-pkd-openldap1 \
  ldapsearch -x -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w "ldap_test_password_123" \
  -b "dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  -s sub "(objectClass=pkdDownload)" dn 2>&1 | grep "^dn:" | wc -l

# Count countries with CSCA
docker exec icao-local-pkd-openldap1 \
  ldapsearch -x -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w "ldap_test_password_123" \
  -b "dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  -s one dn 2>&1 | grep "^dn:" | wc -l
```

**Expected Result**:
- Total pkdDownload entries: ~531-532 (DN duplicates merged)
- Countries with CSCA: 95

### 3.5 CSCA Cache Verification

```bash
# Check CSCA cache status via API
curl -s http://localhost:8080/api/health | jq '.cscaCache'
```

**Expected Result**:
```json
{
  "enabled": true,
  "entries": 215,
  "totalCertificates": 536,
  "hitRate": 0.0,
  "lastInitTime": "2026-01-25T02:11:16Z"
}
```

### 3.6 Log Analysis

```bash
# Master List processing logs
docker logs icao-local-pkd-management --since 1h | grep -E "Master List|MASTERLIST"

# Link certificate detection
docker logs icao-local-pkd-management --since 1h | grep "Link Certificate detected" | wc -l

# LDAP save success/failure
docker logs icao-local-pkd-management --since 1h | grep "Saved certificate to LDAP" | wc -l
docker logs icao-local-pkd-management --since 1h | grep "Failed to save certificate to LDAP"
```

**Expected Result**:
- "Saved certificate to LDAP": 536 messages
- "Failed to save certificate to LDAP": 0 messages
- "Link Certificate detected": 60 messages

---

## 4. Test Case: December 2025 Master List

### 4.1 Upload Information

```
Upload ID: 80e1b2b2-da84-4222-a65b-264aa2109c53
File Name: ICAO_ml_December2025.ml
File Size: 810,009 bytes
Processing Mode: AUTO
Status: COMPLETED
Upload Time: 2026-01-25 11:11:13
Processing Duration: 3.448 seconds
```

### 4.2 Certificate Statistics

| Metric | Count | Percentage |
|--------|-------|------------|
| Total Certificates | 536 | 100% |
| Self-Signed CSCA | 476 | 88.8% |
| Link Certificates | 60 | 11.2% |
| Unique DN+Serial | 532 | 99.3% |
| Duplicate DN+Serial | 4 | 0.7% |

### 4.3 Top 10 Countries

| Country | Code | CSCA Count | Link Certs |
|---------|------|------------|------------|
| China | CN | 34 | - |
| Hungary | HU | 21 | 8 |
| Latvia | LV | 16 | 9 |
| Netherlands | NL | 15 | 8 |
| New Zealand | NZ | 13 | 1 |
| Germany | DE | 13 | 3 |
| Switzerland | CH | 12 | - |
| Australia | AU | 12 | - |
| Singapore | SG | 11 | 2 |
| Romania | RO | 11 | - |

### 4.4 Link Certificate Examples

**Latvia (9 link certificates)**:
- Key Rotation: serialNumber 001 → 002 → 003 → 004 → 005 → 007 → 008 → 009
- Organization Change: National Security Authority → OCMA

**Hungary (8 link certificates)**:
- Organization Change: Ministry of Interior → Cabinet Office → CRO
- Certificate Evolution: CSCA HUNGARY → CSCA-HUNGARY 4 → CSCA-HUNGARY 2017 → CSCA-HUNGARY 2020

**Korea (2 link certificates)**:
- Key Rotation: CSCA-KOREA → CSCA-KOREA-2025

---

## 5. DN Duplication Analysis

### 5.1 Database vs LDAP Discrepancy

| Data Store | Total Entries | Note |
|------------|---------------|------|
| PostgreSQL | 536 | All certificates stored |
| LDAP | 531 | DN duplicates merged |
| Difference | 5 | Expected behavior |

### 5.2 Why Duplicates Exist

**Root Cause**: ICAO Master List contains re-issued certificates with identical Subject DN + Serial Number.

**Example (China)**:
```
Certificate 1: CN=China Passport Country Signing Certificate, serialNumber=434E445343410005
Certificate 2: CN=China Passport Country Signing Certificate, serialNumber=434E445343410005
                (re-issued with different fingerprint)
```

### 5.3 LDAP Duplicate Handling

**Code Logic** (`main.cpp:2813-2822`):
```cpp
int rc = ldap_add_ext_s(ld, dn.c_str(), mods, nullptr, nullptr);

if (rc == LDAP_ALREADY_EXISTS) {
    // Try to update the certificate
    LDAPMod modCertReplace;
    modCertReplace.mod_op = LDAP_MOD_REPLACE | LDAP_MOD_BVALUES;
    modCertReplace.mod_type = const_cast<char*>("userCertificate;binary");
    modCertReplace.mod_bvalues = certBvVals;

    LDAPMod* replaceMods[] = {&modCertReplace, nullptr};
    rc = ldap_modify_ext_s(ld, dn.c_str(), replaceMods, nullptr, nullptr);
}
```

**Behavior**:
1. First certificate: `ldap_add_ext_s()` succeeds → New entry created
2. Duplicate certificate: `ldap_add_ext_s()` fails with `LDAP_ALREADY_EXISTS`
3. Fallback: `ldap_modify_ext_s()` replaces `userCertificate;binary` attribute
4. Result: **Last certificate overwrites previous one**

### 5.4 Detected Duplicate Cases

```sql
-- Find duplicate DN+Serial combinations
SELECT
    subject_dn,
    serial_number,
    COUNT(*) as count,
    STRING_AGG(fingerprint_sha256, ', ') as fingerprints
FROM certificate
WHERE certificate_type = 'CSCA'
GROUP BY subject_dn, serial_number
HAVING COUNT(*) > 1;
```

**Confirmed Duplicates**:
1. China - `sn=434E445343410005`: 2 certificates
2. China - `sn=55FDDF6FB9C6369A`: 2 certificates
3. China - `sn=706E2CBC2436B1FA`: 2 certificates
4. Germany or Other - 1 additional duplicate

---

## 6. CSCA Cache Analysis

### 6.1 Cache Structure

```
Total Certificates: 536
Unique Subject DNs: 215
Average Certificates per DN: 2.49
```

**Interpretation**: Multiple certificates share the same Subject DN due to:
- Key rotation (new certificate with same Subject DN)
- Re-issuance (validity period extension)

### 6.2 Cache Purpose

The CSCA Cache is used for **DSC Trust Chain Validation**:

```
DSC Certificate
    ↓
Extract Issuer DN
    ↓
Lookup CSCA Cache by Issuer DN
    ↓
Find matching CSCA(s)
    ↓
Verify DSC signature with each CSCA public key
    ↓
Trust Chain Valid if any CSCA verifies successfully
```

### 6.3 Cache Performance

- **Initialization Time**: ~73ms for 536 certificates
- **Lookup Complexity**: O(1) hash map lookup by DN
- **Memory Usage**: ~215 unique DN keys + 536 certificate references

---

## 7. Common Issues and Troubleshooting

### 7.1 Issue: LDAP Connection Failed

**Symptom**:
```
ldap_bind: Invalid credentials (49)
```

**Solution**:
1. Check LDAP admin password from environment:
   ```bash
   docker exec icao-local-pkd-openldap1 env | grep LDAP_ADMIN_PASSWORD
   ```
2. Use correct password in ldapsearch commands

### 7.2 Issue: Zero LDAP Entries

**Symptom**:
```
docker exec ... ldapsearch ... | grep "^dn:" | wc -l
# Result: 0
```

**Possible Causes**:
1. Wrong base DN
2. Wrong objectClass filter
3. LDAP not initialized

**Diagnosis**:
```bash
# 1. Check LDAP DIT structure
docker exec icao-local-pkd-openldap1 \
  ldapsearch -x -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w "ldap_test_password_123" \
  -b "dc=ldap,dc=smartcoreinc,dc=com" \
  -s one dn

# 2. Check objectClass
docker exec icao-local-pkd-openldap1 \
  ldapsearch -x -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w "ldap_test_password_123" \
  -b "o=csca,c=LV,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  -s one objectClass
```

### 7.3 Issue: Database Count Mismatch

**Symptom**:
```sql
SELECT COUNT(*) FROM certificate WHERE certificate_type = 'CSCA';
-- Result: 0 (expected 536)
```

**Possible Causes**:
1. Processing failed silently
2. Transaction rollback
3. Wrong database

**Diagnosis**:
```bash
# 1. Check backend logs
docker logs icao-local-pkd-management --tail 500 | grep -i error

# 2. Check upload status
docker exec icao-local-pkd-postgres psql -U pkd -d localpkd \
  -c "SELECT id, status, error_message FROM uploaded_file ORDER BY upload_timestamp DESC LIMIT 1;"
```

---

## 8. Best Practices

### 8.1 Before Upload

1. **Verify File Format**: Ensure file is valid PKCS#7 CMS
   ```bash
   openssl cms -inform DER -in ICAO_ml_December2025.ml -cmsout -print
   ```

2. **Check File Size**: Typical Master List is 500KB - 1MB

3. **Backup Current Data**: Create database backup before upload
   ```bash
   ./docker-backup.sh
   ```

### 8.2 After Upload

1. **Verify Upload Status**: Check frontend Upload History for COMPLETED status

2. **Run Verification Script**:
   ```bash
   # Create verification script
   cat > verify-masterlist.sh << 'EOF'
   #!/bin/bash

   echo "=== Master List Upload Verification ==="

   # 1. Database count
   DB_COUNT=$(docker exec icao-local-pkd-postgres psql -U pkd -d localpkd -t -c \
     "SELECT COUNT(*) FROM certificate WHERE certificate_type = 'CSCA';")
   echo "DB CSCA Count: $DB_COUNT"

   # 2. LDAP count
   LDAP_COUNT=$(docker exec icao-local-pkd-openldap1 \
     ldapsearch -x -H ldap://localhost:389 \
     -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
     -w "ldap_test_password_123" \
     -b "dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
     -s sub "(objectClass=pkdDownload)" dn 2>&1 | grep "^dn:" | wc -l)
   echo "LDAP CSCA Count: $LDAP_COUNT"

   # 3. Cache status
   CACHE_COUNT=$(curl -s http://localhost:8080/api/health | jq -r '.cscaCache.totalCertificates')
   echo "CSCA Cache Count: $CACHE_COUNT"

   # 4. Link certificates
   LINK_COUNT=$(docker logs icao-local-pkd-management --since 10m | \
     grep "Link Certificate detected" | wc -l)
   echo "Link Certificates Detected: $LINK_COUNT"

   echo ""
   echo "Expected: DB ~536, LDAP ~531, Cache 536, Links ~60"
   EOF

   chmod +x verify-masterlist.sh
   ./verify-masterlist.sh
   ```

3. **Check for Errors**:
   ```bash
   docker logs icao-local-pkd-management --since 10m | grep -i error
   ```

### 8.3 Ongoing Monitoring

1. **Monitor CSCA Cache Hit Rate**:
   ```bash
   curl -s http://localhost:8080/api/health | jq '.cscaCache.hitRate'
   ```

2. **Track Upload History**:
   ```sql
   SELECT
       upload_timestamp,
       file_name,
       csca_count,
       status
   FROM uploaded_file
   WHERE file_format = 'ML'
   ORDER BY upload_timestamp DESC
   LIMIT 10;
   ```

---

## 9. Appendix

### 9.1 LDAP DIT Structure

```
dc=ldap,dc=smartcoreinc,dc=com
└── dc=pkd
    └── dc=download
        ├── dc=data
        │   └── c={COUNTRY}
        │       ├── o=csca    (CSCA certificates)
        │       ├── o=dsc     (DSC certificates)
        │       ├── o=crl     (CRL)
        │       └── o=ml      (Master Lists - future use)
        └── dc=nc-data
            └── c={COUNTRY}
                └── o=dsc     (DSC_NC - Non-Conformant)
```

### 9.2 DN Format

**Legacy DN Format** (current):
```
cn={Subject DN}+sn={Serial Number},o={Type},c={Country},dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
```

**Example**:
```
cn=CN\=CSCA Latvia\,O\=National Security Authority\,C\=LV+sn=275D,o=csca,c=LV,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
```

### 9.3 objectClass Hierarchy

```
top (abstract)
└── person (structural)
    └── organizationalPerson (structural)
        └── inetOrgPerson (structural)
            └── pkdDownload (auxiliary, ICAO PKD custom schema)
```

**Required Attributes**:
- `cn`: Subject DN (from person)
- `sn`: Serial Number (from person)
- `userCertificate;binary`: X.509 certificate DER (from inetOrgPerson)

**Optional Attributes** (pkdDownload):
- `description`: Full Subject DN + Fingerprint
- `pkdConformanceCode`: Conformance code (DSC_NC only)
- `pkdConformanceText`: Conformance text (DSC_NC only)
- `pkdVersion`: PKD version (DSC_NC only)

---

## 10. References

- **ICAO Doc 9303 Part 12**: PKI for MRTDs
- **RFC 5652**: Cryptographic Message Syntax (CMS)
- **RFC 4514**: LDAP Distinguished Names
- **OpenLDAP Admin Guide**: https://www.openldap.org/doc/admin24/

---

**Document Maintained By**: PKD Development Team
**Last Review Date**: 2026-01-25
**Next Review Date**: 2026-04-25
