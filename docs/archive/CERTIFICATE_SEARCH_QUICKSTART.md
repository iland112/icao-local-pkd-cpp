# Certificate Search - Quick Start Guide

**Feature**: PKD Certificate Search & Export
**Version**: 2.37.0
**Status**: Production Ready
**Date**: 2026-03-18

---

## Quick Access

### Frontend UI
**URL**: http://localhost:13080/pkd/certificates

**Navigation**: Sidebar > Certificate Management > "인증서 조회"

### API Endpoint
**Base URL**: http://localhost:18080/api/certificates

---

## Authentication

### JWT (Browser / Internal)
Certificate search endpoints are **public** (no JWT required).

### API Key (External / M2M)
External clients can authenticate using the `X-API-Key` header (added in v2.21.0). The API key requires the **`cert:read`** permission for search endpoints and **`cert:export`** for export endpoints.

```bash
curl -H "X-API-Key: icao_xxxx_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" \
  "http://localhost:18080/api/certificates/search?limit=10"
```

---

## Quick Examples

### 1. Search All Certificates
```bash
curl "http://localhost:18080/api/certificates/search?limit=10"
```
**Returns**: First 10 certificates from 31,212 total

### 2. Search by Country
```bash
curl "http://localhost:18080/api/certificates/search?country=KR&limit=10"
```
**Returns**: First 10 certificates from Korea (227 total)

### 3. Search by Country + Type
```bash
curl "http://localhost:18080/api/certificates/search?country=KR&certType=DSC&limit=10"
```
**Returns**: DSC certificates from Korea only

### 4. Pagination
```bash
curl "http://localhost:18080/api/certificates/search?country=KR&limit=10&offset=20"
```
**Returns**: Certificates 21-30 from Korea

### 5. Filter by Source Type (v2.29.0+)
```bash
curl "http://localhost:18080/api/certificates/search?source=PA_EXTRACTED&limit=10"
```
**Returns**: Certificates auto-registered from PA verification

### 6. Get Certificate Detail
```bash
curl "http://localhost:18080/api/certificates/detail?dn={FULL_DN}"
```
**Returns**: Full certificate metadata

### 7. Export Single Certificate (DER)
```bash
curl "http://localhost:18080/api/certificates/export/file?dn={DN}&format=der" -o cert.crt
```
**Downloads**: Binary DER certificate file

### 8. Export Single Certificate (PEM)
```bash
curl "http://localhost:18080/api/certificates/export/file?dn={DN}&format=pem" -o cert.pem
```
**Downloads**: Base64 PEM certificate file

### 9. Export Country Certificates (ZIP)
```bash
curl "http://localhost:18080/api/certificates/export/country?country=KR&format=pem" -o KR_certs.zip
```
**Downloads**: ZIP archive with all Korea certificates

### 10. Export All Certificates (DIT-structured ZIP)
```bash
curl "http://localhost:18080/api/certificates/export/all?format=pem" -o all_certs.zip
```
**Downloads**: Full LDAP DIT-structured ZIP archive with all certificates, CRLs, and Master Lists

### 11. Using X-API-Key (v2.21.0+)
```bash
curl -H "X-API-Key: icao_xxxx_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx" \
  "http://localhost:18080/api/certificates/search?country=KR&certType=DSC&limit=10"
```
**Returns**: Same as example 3, authenticated via API key (requires `cert:read` permission)

---

## Search Parameters

| Parameter | Type | Required | Values | Description |
|-----------|------|----------|--------|-------------|
| `country` | string | No | ISO 3166-1 alpha-2 | Filter by country code (e.g., KR, US) |
| `certType` | string | No | CSCA, DSC, DSC_NC, CRL, ML | Filter by certificate type |
| `validity` | string | No | VALID, EXPIRED, NOT_YET_VALID, all | Filter by validity status (default: all) |
| `source` | string | No | LDIF_PARSED, ML_PARSED, FILE_UPLOAD, PA_EXTRACTED, DL_PARSED | Filter by certificate source type (v2.29.0+) |
| `searchTerm` | string | No | any text | Search in Common Name (CN) |
| `limit` | integer | No | 1-200 | Results per page (default: 50) |
| `offset` | integer | No | 0+ | Pagination offset (default: 0) |

### Source Type Values

| Value | Description |
|-------|-------------|
| `LDIF_PARSED` | Extracted from LDIF file upload |
| `ML_PARSED` | Extracted from Master List file upload |
| `FILE_UPLOAD` | Individual certificate file upload (PEM, DER, P7B, DL, CRL) |
| `PA_EXTRACTED` | Auto-registered from PA (Passive Authentication) verification |
| `DL_PARSED` | Extracted from Deviation List file |

**Note**: When a `source` filter is applied, the search uses DB-based querying instead of LDAP search.

---

## Required Permissions (API Key)

| Endpoint | Permission |
|----------|------------|
| `GET /api/certificates/search` | `cert:read` |
| `GET /api/certificates/countries` | `cert:read` |
| `GET /api/certificates/validation` | `cert:read` |
| `GET /api/certificates/doc9303-checklist` | `cert:read` |
| `GET /api/certificates/export/*` | `cert:export` |

The 12 available API key permissions are: `cert:read`, `cert:export`, `pa:verify`, `pa:read`, `pa:stats`, `upload:read`, `upload:write`, `report:read`, `ai:read`, `sync:read`, `icao:read`, `api-client:manage`.

---

## Response Format

### Search Response
```json
{
  "success": true,
  "total": 227,
  "limit": 10,
  "offset": 0,
  "certificates": [
    {
      "dn": "cn=...,o=dsc,c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com",
      "cn": "DS0120080307 1",
      "sn": "0D",
      "country": "KR",
      "certType": "DSC",
      "subjectDn": "CN=DS0120080307 1,O=certificates,C=KR",
      "issuerDn": "CN=CSCA,OU=MOFA,O=Government,C=KR",
      "fingerprint": "a1b2c3d4...",
      "validFrom": "2008-03-07T00:00:00Z",
      "validTo": "2018-03-07T23:59:59Z",
      "validity": "EXPIRED",
      "isSelfSigned": false
    }
  ]
}
```

### Detail Response
```json
{
  "success": true,
  "dn": "...",
  "cn": "...",
  "sn": "...",
  "country": "KR",
  "certType": "DSC",
  "subjectDn": "...",
  "issuerDn": "...",
  "fingerprint": "...",
  "validFrom": "2008-03-07T00:00:00Z",
  "validTo": "2018-03-07T23:59:59Z",
  "validity": "EXPIRED",
  "isSelfSigned": false
}
```

---

## Important Notes

### CertType Filter Limitation
**Issue**: Filtering by `certType` alone (without `country`) does not effectively filter by type.

**Example**:
```bash
# This does NOT filter by type effectively
curl "http://localhost:18080/api/certificates/search?certType=CSCA&limit=10"
```

**Reason**: LDAP hierarchy requires country for type-based filtering (`o=csca,c=XX,...`).

**Solution**: Always specify country when filtering by type:
```bash
# This works correctly
curl "http://localhost:18080/api/certificates/search?country=KR&certType=CSCA&limit=10"
```

### Source Filter Uses DB Search
When the `source` parameter is specified, the search bypasses LDAP and queries the database directly. This means DN format in results may differ (parsed from DB fields rather than LDAP DN).

### Export Format
- **DER**: Binary format (`.crt` or `.der` extension)
- **PEM**: Base64 encoded (`.pem` extension)
- **ZIP**: Multiple certificates in archive (`.zip` extension)

### Performance
- **Search**: < 500ms for filtered queries
- **Export (single)**: < 100ms
- **Export (country ZIP)**: Varies by country size (1-5s)

---

## Frontend Testing Checklist

Visit: http://localhost:13080/pkd/certificates

- [ ] **Page loads** without errors
- [ ] **Search all** - Click "검색" without filters
- [ ] **Country filter** - Select a country (e.g., Korea), click "검색"
- [ ] **Type filter** - Select country + type (e.g., Korea + DSC)
- [ ] **Source filter** - Select a source type (e.g., PA_EXTRACTED)
- [ ] **Pagination** - Navigate to next/previous page
- [ ] **Page size** - Change results per page (10, 25, 50, 100, 200)
- [ ] **Certificate detail** - Click file icon on any row
- [ ] **Export single** - Click download icon on any row
- [ ] **Export country (DER)** - Enter country code, click DER export
- [ ] **Export country (PEM)** - Enter country code, click PEM export
- [ ] **Export all (PEM)** - Click full export button
- [ ] **Export all (DER)** - Click full export button

---

## Troubleshooting

### Backend Not Responding
```bash
# Check service status
docker ps | grep pkd-management

# Check logs
docker logs icao-local-pkd-management --tail 50

# Restart service
docker compose -f docker/docker-compose.yaml restart pkd-management
```

### LDAP Connection Issues
```bash
# Check LDAP status
docker logs icao-local-pkd-haproxy --tail 20

# Test LDAP directly
docker exec icao-local-pkd-management ldapsearch -H ldap://haproxy:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" -w admin \
  -b "dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  "(objectClass=*)" dn | head -20
```

### Frontend Not Loading
```bash
# Check frontend status
docker logs icao-local-pkd-frontend --tail 20

# Restart frontend
docker compose -f docker/docker-compose.yaml restart frontend

# Force browser refresh
# Ctrl+Shift+R (Windows/Linux) or Cmd+Shift+R (Mac)
```

---

## Additional Documentation

- **Implementation Guide**: [CERTIFICATE_SEARCH_IMPLEMENTATION.md](./CERTIFICATE_SEARCH_IMPLEMENTATION.md)
- **Complete Status Report**: [CERTIFICATE_SEARCH_STATUS.md](./CERTIFICATE_SEARCH_STATUS.md)
- **Design Document**: [PKD_CERTIFICATE_SEARCH_DESIGN.md](./PKD_CERTIFICATE_SEARCH_DESIGN.md)
- **API Client Admin Guide**: [API_CLIENT_ADMIN_GUIDE.md](./API_CLIENT_ADMIN_GUIDE.md)
- **API Client User Guide**: [API_CLIENT_USER_GUIDE.md](./API_CLIENT_USER_GUIDE.md)

---

## Quick Stats

- **Total Certificates**: 31,212
- **Countries**: 150+
- **CSCA**: ~845
- **DSC**: ~29,838
- **DSC_NC**: ~502
- **CRL**: 69
- **Response Time**: < 500ms
- **Architecture**: Clean Architecture (4 layers)
- **Build**: v2.37.0

---

**Status**: Fully Operational
**Last Updated**: 2026-03-18
**Maintainer**: kbjung
