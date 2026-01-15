# LDAP Query Guide

**Last Updated**: 2026-01-15
**Version**: 1.0

---

## Overview

이 문서는 ICAO Local PKD 프로젝트에서 OpenLDAP 서버를 직접 조회하는 방법을 설명합니다.

## Critical: Anonymous Bind 제한

**중요**: 우리 OpenLDAP 설정은 **Anonymous Bind를 허용하지 않습니다**.

### 실패하는 조회 방법 ❌

```bash
# Anonymous bind - 항상 "No such object" 오류 발생
ldapsearch -x -H ldap://localhost:389 \
  -b "c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  -s sub "(objectClass=*)"

# Result: 32 No such object
```

### 성공하는 조회 방법 ✅

```bash
# Authenticated bind - 정상 동작
ldapsearch -x -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w admin \
  -b "c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  -s sub "(objectClass=*)"

# Result: 227 entries found
```

## LDAP 연결 정보

### Docker 환경 (로컬 개발)

| 항목 | 값 |
|------|-----|
| **Host** | localhost:389 (HAProxy) |
| **Bind DN** | `cn=admin,dc=ldap,dc=smartcoreinc,dc=com` |
| **Password** | `admin` |
| **Base DN** | `dc=ldap,dc=smartcoreinc,dc=com` |
| **PKD Base DN** | `dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com` |

### OpenLDAP 직접 접근

```bash
# OpenLDAP1 (Primary Master)
docker exec icao-local-pkd-openldap1 ldapsearch -x -LLL \
  -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w admin \
  -b "dc=ldap,dc=smartcoreinc,dc=com" \
  "(objectClass=*)"

# OpenLDAP2 (Secondary Master)
docker exec icao-local-pkd-openldap2 ldapsearch -x -LLL \
  -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w admin \
  -b "dc=ldap,dc=smartcoreinc,dc=com" \
  "(objectClass=*)"
```

## LDAP DIT 구조

```
dc=ldap,dc=smartcoreinc,dc=com (Base DN)
└── dc=pkd
    └── dc=download
        ├── dc=data                           # Normal certificates
        │   └── c={COUNTRY}                   # 예: c=KR, c=US, c=ZZ (UN)
        │       ├── o=csca                    # CSCA certificates
        │       │   └── cn={CN}+sn={SN}       # Certificate entry
        │       ├── o=dsc                     # DSC certificates
        │       │   └── cn={CN}+sn={SN}
        │       ├── o=crl                     # Certificate Revocation Lists
        │       │   └── cn={SHA256_HASH}      # CRL entry
        │       └── o=ml                      # Master Lists
        │           └── cn={SHA256_HASH}      # ML entry
        └── dc=nc-data                        # Non-conformant certificates
            └── c={COUNTRY}
                └── o=dsc                     # DSC_NC certificates
```

## 일반적인 조회 예제

### 1. 특정 국가의 모든 인증서 조회

```bash
docker exec icao-local-pkd-openldap1 ldapsearch -x -LLL \
  -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w admin \
  -b "c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  -s sub "(objectClass=*)" dn
```

**결과 예시**:
```
dn: c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
dn: o=csca,c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
dn: o=dsc,c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
dn: o=crl,c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
dn: cn=0f6c529dfde363fd3672191b70ba52a6971732cf46a05ac7c97dc5732b6ed1a3,o=crl,c=KR,...
dn: cn=CN\3DDS0120080307 1\2CO\3Dcertificates\2CC\3DKR+sn=0D,o=dsc,c=KR,...
...
```

### 2. 특정 국가의 CSCA만 조회

```bash
docker exec icao-local-pkd-openldap1 ldapsearch -x -LLL \
  -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w admin \
  -b "o=csca,c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  -s sub "(objectClass=*)" dn
```

### 3. 특정 국가의 CRL 조회

```bash
docker exec icao-local-pkd-openldap1 ldapsearch -x -LLL \
  -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w admin \
  -b "o=crl,c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  -s sub "(objectClass=*)"
```

### 4. 특정 CRL 상세 정보 조회

```bash
docker exec icao-local-pkd-openldap1 ldapsearch -x -LLL \
  -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w admin \
  -b "cn=0f6c529dfde363fd3672191b70ba52a6971732cf46a05ac7c97dc5732b6ed1a3,o=crl,c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  -s base "(objectClass=*)"
```

**결과 예시**:
```
dn: cn=0f6c529dfde363fd3672191b70ba52a6971732cf46a05ac7c97dc5732b6ed1a3,o=crl,c=KR,...
objectClass: top
objectClass: cRLDistributionPoint
objectClass: pkdDownload
cn: 0f6c529dfde363fd3672191b70ba52a6
cn: 0f6c529dfde363fd3672191b70ba52a6971732cf46a05ac7c97dc5732b6ed1a3
certificateRevocationList;binary:: MIIDBjCCAToCAQEwQQYJKoZIhvcNAQEKMDSgDzAN...
```

### 5. ObjectClass 필터링

```bash
# PKD Download 객체만 조회 (인증서 + ML)
docker exec icao-local-pkd-openldap1 ldapsearch -x -LLL \
  -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w admin \
  -b "c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  -s sub "(objectClass=pkdDownload)" dn

# CRL Distribution Point 객체만 조회
docker exec icao-local-pkd-openldap1 ldapsearch -x -LLL \
  -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w admin \
  -b "c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  -s sub "(objectClass=cRLDistributionPoint)" dn

# 인증서 + CRL 모두 조회 (애플리케이션 방식)
docker exec icao-local-pkd-openldap1 ldapsearch -x -LLL \
  -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w admin \
  -b "c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  -s sub "(|(objectClass=pkdDownload)(objectClass=cRLDistributionPoint))" dn
```

### 6. 모든 국가 목록 조회

```bash
docker exec icao-local-pkd-openldap1 ldapsearch -x -LLL \
  -H ldap://localhost:389 \
  -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
  -w admin \
  -b "dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com" \
  -s one "(objectClass=*)" dn | grep "^dn: c="
```

**결과 예시**:
```
dn: c=LV,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
dn: c=AE,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
dn: c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
dn: c=ZZ,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
...
```

## ObjectClass 설명

### pkdDownload
- **용도**: ICAO PKD 인증서 및 Master List
- **Attributes**:
  - `userCertificate;binary` - DSC, ML 인증서 데이터
  - `cACertificate;binary` - CSCA 인증서 데이터
  - `cn` - Common Name (Subject/Serial 조합)
  - `c` - Country code

### cRLDistributionPoint
- **용도**: Certificate Revocation List
- **Attributes**:
  - `certificateRevocationList;binary` - CRL 바이너리 데이터
  - `cn` - SHA-256 fingerprint
  - `c` - Country code

## 애플리케이션 LDAP 연결

### C++ LdapCertificateRepository

애플리케이션은 다음과 같이 LDAP에 연결합니다:

```cpp
// Connection
ldap_initialize(&ldap_, "ldap://haproxy:389");
ldap_set_option(ldap_, LDAP_OPT_PROTOCOL_VERSION, &version);

// Authenticated bind
struct berval cred;
cred.bv_val = const_cast<char*>(bindPassword.c_str());
cred.bv_len = bindPassword.length();

ldap_sasl_bind_s(
    ldap_,
    "cn=admin,dc=ldap,dc=smartcoreinc,dc=com",  // Bind DN
    LDAP_SASL_SIMPLE,
    &cred,
    nullptr, nullptr, nullptr
);

// Connection test (auto-reconnect)
struct berval* authzId = nullptr;
int rc = ldap_whoami_s(ldap_, &authzId, nullptr, nullptr);
if (rc != LDAP_SUCCESS) {
    // Reconnect
}
```

### 주요 조회 함수

1. **getDnsByCountryAndType()**:
   ```cpp
   // Base DN 생성
   std::string baseDn = "c=KR,dc=data,dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com";

   // Filter
   std::string filter = "(|(objectClass=pkdDownload)(objectClass=cRLDistributionPoint))";

   // Search
   ldap_search_ext_s(ldap_, baseDn.c_str(), LDAP_SCOPE_SUBTREE, filter.c_str(), ...);
   ```

2. **getCertificateBinary()**:
   ```cpp
   // Attributes to retrieve
   const char* attrs[] = {
       "userCertificate;binary",
       "cACertificate;binary",
       "certificateRevocationList;binary",
       nullptr
   };

   // Base scope search (specific DN)
   ldap_search_ext_s(ldap_, dn.c_str(), LDAP_SCOPE_BASE, "(objectClass=*)", attrs, ...);
   ```

## 트러블슈팅

### "No such object" 오류

**증상**:
```bash
ldapsearch -x -H ldap://localhost:389 -b "c=KR,..."
# Result: 32 No such object
```

**해결**:
- ✅ **Bind DN과 Password 추가**:
  ```bash
  ldapsearch -x -H ldap://localhost:389 \
    -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" \
    -w admin \
    -b "c=KR,..."
  ```

### "Invalid credentials" 오류

**증상**:
```bash
ldapsearch -x -D "cn=admin,dc=ldap,dc=smartcoreinc,dc=com" -w wrong_password ...
# Result: 49 Invalid credentials
```

**해결**:
- ✅ 올바른 비밀번호 사용: `admin`
- ✅ 환경변수 확인:
  ```bash
  docker exec icao-local-pkd-openldap1 env | grep LDAP_ADMIN_PASSWORD
  # LDAP_ADMIN_PASSWORD=admin
  ```

### Root DSE 조회

LDAP 서버의 기본 정보 확인:

```bash
docker exec icao-local-pkd-openldap1 ldapsearch -x -LLL \
  -H ldap://localhost:389 \
  -b "" -s base "(objectClass=*)" namingContexts

# Result:
# dn:
# namingContexts: dc=ldap,dc=smartcoreinc,dc=com
```

## 외부 LDAP 브라우저 연결

### Apache Directory Studio

- **Host**: localhost
- **Port**: 389 (HAProxy) 또는 3891 (OpenLDAP1), 3892 (OpenLDAP2)
- **Bind DN**: `cn=admin,dc=ldap,dc=smartcoreinc,dc=com`
- **Bind Password**: `admin`
- **Base DN**: `dc=ldap,dc=smartcoreinc,dc=com`

### LDAP Admin (Windows)

동일한 설정 사용.

### phpLDAPadmin (Web)

Docker Compose에 추가 가능:
```yaml
services:
  phpldapadmin:
    image: osixia/phpldapadmin:latest
    ports:
      - "8090:80"
    environment:
      - PHPLDAPADMIN_LDAP_HOSTS=openldap1
```

## 성능 고려사항

### 대용량 조회

```bash
# Pagination 사용 (100개 제한)
ldapsearch ... -E pr=100/noprompt

# 특정 Attributes만 조회 (속도 향상)
ldapsearch ... dn cn c

# DN만 조회 (가장 빠름)
ldapsearch ... 1.1
```

### 애플리케이션 최적화

- ✅ Connection pooling (HAProxy 로드밸런싱)
- ✅ Auto-reconnect (ldap_whoami_s 테스트)
- ✅ Batch operations (한 번에 여러 DN 조회)
- ✅ Attribute filtering (필요한 것만 요청)

## 참고 문서

- [OpenLDAP Administrator's Guide](https://www.openldap.org/doc/admin24/)
- [LDAP C API RFC 1823](https://datatracker.ietf.org/doc/html/rfc1823)
- [ICAO PKD LDAP Profile](https://www.icao.int/Security/FAL/PKD/Pages/LDAP.aspx)

---

**마지막 업데이트**: 2026-01-15
**작성자**: Development Team
