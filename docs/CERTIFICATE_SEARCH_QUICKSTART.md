# Certificate Search - Quick Start Guide

**Feature**: PKD Certificate Search & Export
**Version**: 1.6.0
**Status**: ✅ Production Ready
**Date**: 2026-01-15

---

## 🚀 Quick Access

### Frontend UI
**URL**: http://localhost:3000/pkd/certificates

**Navigation**: Sidebar → PKD Management → "인증서 조회"

### API Endpoint
**Base URL**: http://localhost:8080/api/certificates

---

## 📖 Quick Examples

### 1. Search All Certificates
```bash
curl "http://localhost:8080/api/certificates/search?limit=10"
```
**Returns**: First 10 certificates from 30,226 total

### 2. Search by Country
```bash
curl "http://localhost:8080/api/certificates/search?country=KR&limit=10"
```
**Returns**: First 10 certificates from Korea (227 total)

### 3. Search by Country + Type
```bash
curl "http://localhost:8080/api/certificates/search?country=KR&certType=DSC&limit=10"
```
**Returns**: DSC certificates from Korea only

### 4. Pagination
```bash
curl "http://localhost:8080/api/certificates/search?country=KR&limit=10&offset=20"
```
**Returns**: Certificates 21-30 from Korea

### 5. Get Certificate Detail
```bash
curl "http://localhost:8080/api/certificates/detail?dn={FULL_DN}"
```
**Returns**: Full certificate metadata

### 6. Export Single Certificate (DER)
```bash
curl "http://localhost:8080/api/certificates/export/file?dn={DN}&format=der" -o cert.crt
```
**Downloads**: Binary DER certificate file

### 7. Export Single Certificate (PEM)
```bash
curl "http://localhost:8080/api/certificates/export/file?dn={DN}&format=pem" -o cert.pem
```
**Downloads**: Base64 PEM certificate file

### 8. Export Country Certificates (ZIP)
```bash
curl "http://localhost:8080/api/certificates/export/country?country=KR&format=pem" -o KR_certs.zip
```
**Downloads**: ZIP archive with all Korea certificates

---

## 🎯 Search Parameters

| Parameter | Type | Required | Values | Description |
|-----------|------|----------|--------|-------------|
| `country` | string | No | ISO 3166-1 alpha-2 | Filter by country code (e.g., KR, US) |
| `certType` | string | No | CSCA, DSC, DSC_NC, CRL, ML | Filter by certificate type |
| `validity` | string | No | VALID, EXPIRED, NOT_YET_VALID, all | Filter by validity status (default: all) |
| `searchTerm` | string | No | any text | Search in Common Name (CN) |
| `limit` | integer | No | 1-200 | Results per page (default: 50) |
| `offset` | integer | No | 0+ | Pagination offset (default: 0) |

---

## 🔍 Response Format

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

## ⚠️ Important Notes

### CertType Filter Limitation
**Issue**: Filtering by `certType` alone (without `country`) does not effectively filter by type.

**Example**:
```bash
# This does NOT filter by type effectively
curl "http://localhost:8080/api/certificates/search?certType=CSCA&limit=10"
```

**Reason**: LDAP hierarchy requires country for type-based filtering (`o=csca,c=XX,...`).

**Solution**: Always specify country when filtering by type:
```bash
# This works correctly
curl "http://localhost:8080/api/certificates/search?country=KR&certType=CSCA&limit=10"
```

### Export Format
- **DER**: Binary format (`.crt` or `.der` extension)
- **PEM**: Base64 encoded (`.pem` extension)
- **ZIP**: Multiple certificates in archive (`.zip` extension)

### Performance
- **Search**: < 500ms for filtered queries
- **Export (single)**: < 100ms
- **Export (country ZIP)**: Varies by country size (1-5s)

---

## 🧪 Frontend Testing Checklist

Visit: http://localhost:3000/pkd/certificates

- [ ] **Page loads** without errors
- [ ] **Search all** - Click "검색" without filters
- [ ] **Country filter** - Select a country (e.g., Korea), click "검색"
- [ ] **Type filter** - Select country + type (e.g., Korea + DSC)
- [ ] **Pagination** - Navigate to next/previous page
- [ ] **Page size** - Change results per page (10, 25, 50, 100, 200)
- [ ] **Certificate detail** - Click file icon on any row
- [ ] **Export single** - Click download icon on any row
- [ ] **Export country (DER)** - Enter country code, click DER export
- [ ] **Export country (PEM)** - Enter country code, click PEM export

---

## 🐛 Troubleshooting

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

## 📚 Additional Documentation

- **Implementation Guide**: [CERTIFICATE_SEARCH_IMPLEMENTATION.md](./CERTIFICATE_SEARCH_IMPLEMENTATION.md)
- **Complete Status Report**: [CERTIFICATE_SEARCH_STATUS.md](./CERTIFICATE_SEARCH_STATUS.md)
- **Design Document**: [PKD_CERTIFICATE_SEARCH_DESIGN.md](./PKD_CERTIFICATE_SEARCH_DESIGN.md)

---

## 🎊 Quick Stats

- **Total Certificates**: 30,226
- **Countries**: 150+
- **CSCA**: ~525
- **DSC**: ~29,610
- **Response Time**: < 500ms
- **Architecture**: Clean Architecture (4 layers)
- **Build**: v1.6.0 CERTIFICATE-SEARCH-CLEAN-ARCH

---

**Status**: ✅ Fully Operational
**Last Updated**: 2026-01-15
**Maintainer**: kbjung
