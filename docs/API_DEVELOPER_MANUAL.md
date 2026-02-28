# ICAO Local PKD — REST API 개발자 메뉴얼

**Version**: 2.25.5
**Last Updated**: 2026-03-01
**대상**: 외부 클라이언트 애플리케이션 개발자
**Copyright**: SmartCore Inc.

---

## 목차

1. [개요](#1-개요)
2. [시작하기](#2-시작하기)
3. [인증 및 권한](#3-인증-및-권한)
4. [공통 규약](#4-공통-규약)
5. [인증서 검색 및 조회](#5-인증서-검색-및-조회)
6. [CRL 보고서](#6-crl-보고서)
7. [DSC_NC 비준수 인증서 보고서](#7-dsc_nc-비준수-인증서-보고서)
8. [PA 검증 (Passive Authentication)](#8-pa-검증-passive-authentication)
9. [AI 인증서 분석](#9-ai-인증서-분석)
10. [동기화 모니터링](#10-동기화-모니터링)
11. [ICAO PKD 버전 모니터링](#11-icao-pkd-버전-모니터링)
12. [업로드 통계 및 이력](#12-업로드-통계-및-이력)
13. [코드 마스터 (참조 데이터)](#13-코드-마스터-참조-데이터)
14. [시스템 모니터링](#14-시스템-모니터링)
15. [연동 예제](#15-연동-예제)
16. [에러 코드 참조](#16-에러-코드-참조)
17. [FAQ / 트러블슈팅](#17-faq--트러블슈팅)
18. [부록](#18-부록)

---

## 1. 개요

### ICAO Local PKD란

ICAO Local PKD(Public Key Directory)는 전자여권 검증에 필요한 인증서(CSCA, DSC, CRL)를 중앙 관리하는 시스템입니다. 외부 클라이언트 애플리케이션은 REST API를 통해 다음 기능을 사용할 수 있습니다:

| # | 활용 사례 | 주요 엔드포인트 |
|---|----------|---------------|
| 1 | **여권 진위 검증** (Passive Authentication) | `POST /api/pa/verify` |
| 2 | **인증서 검색 및 내보내기** | `GET /api/certificates/search` |
| 3 | **AI 기반 인증서 이상 탐지** | `GET /api/ai/certificate/{fingerprint}` |
| 4 | **CRL/비준수 인증서 보고서** | `GET /api/certificates/crl/report` |
| 5 | **DB-LDAP 동기화 모니터링** | `GET /api/sync/status` |

### 용어 정의

| 용어 | 설명 |
|------|------|
| **CSCA** | Country Signing Certificate Authority — 국가 최상위 서명 인증서 |
| **DSC** | Document Signer Certificate — 여권 서명 인증서 |
| **DSC_NC** | Non-Conformant DSC — ICAO Doc 9303 기술 사양 비준수 DSC |
| **MLSC** | Master List Signer Certificate — Master List 서명 인증서 |
| **CRL** | Certificate Revocation List — 인증서 폐기 목록 |
| **SOD** | Security Object Document — 전자여권 보안 객체 (CMS 서명 구조) |
| **DG** | Data Group — 전자여권 데이터 그룹 (DG1=MRZ, DG2=얼굴 이미지) |
| **MRZ** | Machine Readable Zone — 여권 기계 판독 영역 |
| **PA** | Passive Authentication — ICAO 9303 수동 인증 (서명 검증) |
| **Trust Chain** | DSC → CSCA 인증서 신뢰 체인 |
| **Fingerprint** | 인증서 SHA-256 해시값 (hex, 64자) |
| **LDAP DN** | Lightweight Directory Access Protocol Distinguished Name |

---

## 2. 시작하기

### 2.1 Base URL

| 환경 | URL | 비고 |
|------|-----|------|
| **HTTPS (운영, 권장)** | `https://pkd.smartcoreinc.com/api` | Private CA 인증서 필요 |
| **HTTP (내부 네트워크)** | `http://pkd.smartcoreinc.com/api` | |
| **개발 환경** | `http://localhost/api` | Docker 로컬 환경 |

> **HTTPS 사용 시** Private CA 인증서(`ca.crt`)를 클라이언트에 설치해야 합니다. 관리자에게 인증서 파일을 요청하세요. 설정 방법은 [15.5 HTTPS Private CA 설정](#155-https-private-ca-설정)을 참조하세요.

### 2.2 API Key 발급

API Key는 시스템 관리자가 발급합니다. 관리자에게 다음 정보를 전달하세요:

- **클라이언트 이름**: 시스템/서비스 이름 (예: "출입국관리 Agent")
- **필요한 기능**: PA 검증, 인증서 검색 등
- **접속 IP**: 클라이언트 서버의 IP 주소 또는 CIDR 대역
- **예상 사용량**: 분당/시간당/일일 예상 요청 수

발급된 API Key 형식:
```
icao_{prefix}_{random}
 │      │        │
 │      │        └── 32자 랜덤 문자열 (Base62)
 │      └── 8자 키 접두사 (관리 화면에서 식별용)
 └── 고정 접두사

예: icao_NggoCnqh_a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6
```

- 총 길이: 46자, 대소문자 구분
- API Key는 발급 시 **한 번만 표시**되며, 시스템에는 SHA-256 해시만 저장됩니다
- 분실 시 관리자에게 **재발급** 요청 (기존 키 즉시 무효화)

### 2.3 첫 번째 요청

```bash
# 1. 헬스 체크 (인증 불필요)
curl -s https://pkd.smartcoreinc.com/api/health | jq .

# 2. 인증서 검색 (API Key 사용)
curl -s "https://pkd.smartcoreinc.com/api/certificates/search?country=KR&limit=5" \
  -H "X-API-Key: icao_NggoCnqh_a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6" | jq .
```

### 2.4 인증 방식 요약

| 방식 | 용도 | 헤더 |
|------|------|------|
| **API Key** | 외부 클라이언트 M2M 연동 (권장) | `X-API-Key: icao_...` |
| **JWT Bearer** | 웹 UI 사용자 (내부용) | `Authorization: Bearer eyJ...` |
| **인증 없음** | 대부분의 조회 API는 Public | 헤더 불필요 |

> 대부분의 조회 엔드포인트는 **인증 없이 접근 가능**합니다. API Key는 사용량 추적과 Rate Limiting 개별 관리를 위해 사용합니다.

---

## 3. 인증 및 권한

### 3.1 API Key 인증

모든 API 요청에 `X-API-Key` 헤더를 포함합니다:

```
X-API-Key: icao_NggoCnqh_a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6
```

인증 흐름:
```
외부 Client                              ICAO Local PKD
   |                                          |
   |  HTTP Request                            |
   |  X-API-Key: icao_XXXXXXXX_...  ------→   |
   |                                          | → SHA-256 해시 비교
   |                                          | → Permission 확인
   |                                          | → IP 화이트리스트 확인
   |                                          | → Rate Limit 확인
   |  ←------  200 OK (JSON Response)         |
```

### 3.2 권한 (Permissions)

API Key에 할당 가능한 10개 권한:

| Permission | 설명 | 접근 가능 엔드포인트 |
|------------|------|---------------------|
| `cert:read` | 인증서 검색/조회 | `/api/certificates/search`, `/api/certificates/pa-lookup`, `/api/certificates/validation` |
| `cert:export` | 인증서 내보내기 | `/api/certificates/export/*` |
| `pa:verify` | PA 검증 수행 | `/api/pa/verify`, `/api/pa/parse-*` |
| `pa:read` | PA 이력/통계 조회 | `/api/pa/history`, `/api/pa/{id}`, `/api/pa/statistics` |
| `upload:read` | 업로드 이력 조회 | `/api/upload/history`, `/api/upload/statistics` |
| `upload:write` | 파일 업로드 | `/api/upload/ldif`, `/api/upload/masterlist`, `/api/upload/certificate` |
| `report:read` | 보고서 조회 | `/api/certificates/crl/report`, `/api/certificates/dsc-nc/report` |
| `ai:read` | AI 분석 결과 조회 | `/api/ai/certificate/*`, `/api/ai/anomalies`, `/api/ai/reports/*` |
| `sync:read` | 동기화 상태 조회 | `/api/sync/status`, `/api/sync/stats` |
| `icao:read` | ICAO 버전 조회 | `/api/icao/status`, `/api/icao/latest` |

### 3.3 Rate Limiting

API Key 사용 시 3-tier 슬라이딩 윈도우 방식의 Rate Limiting이 적용됩니다:

| 구간 | 기본 한도 | 설명 |
|------|----------|------|
| 분당 | 60 requests | 1분 슬라이딩 윈도우 |
| 시간당 | 1,000 requests | 1시간 슬라이딩 윈도우 |
| 일당 | 10,000 requests | 24시간 슬라이딩 윈도우 |

Rate Limit 초과 시 응답:
```
HTTP/1.1 429 Too Many Requests
Retry-After: 45
X-RateLimit-Limit: 60
X-RateLimit-Remaining: 0
X-RateLimit-Reset: 1709056500
```

```json
{
  "error": "Rate limit exceeded",
  "message": "Per-minute rate limit exceeded (60/min)",
  "retryAfter": 45
}
```

| 응답 헤더 | 설명 |
|-----------|------|
| `Retry-After` | 다음 요청까지 대기 시간 (초) |
| `X-RateLimit-Limit` | 현재 구간 최대 요청 수 |
| `X-RateLimit-Remaining` | 현재 구간 남은 요청 수 |
| `X-RateLimit-Reset` | 제한 초기화 시각 (Unix timestamp) |

> API Key **없이** 호출하면 Rate Limiting이 적용되지 않습니다.

### 3.4 IP 화이트리스트

관리자가 API Key에 IP 화이트리스트를 설정한 경우, 허용되지 않은 IP에서의 요청은 `403 Forbidden`을 반환합니다. CIDR 표기법을 지원합니다 (예: `10.0.0.0/8`, `192.168.1.0/24`).

### 3.5 PA 엔드포인트 특이사항

PA Service 엔드포인트(`/api/pa/*`)는 Public API이므로:
- 미등록/유효하지 않은 API Key를 전송해도 **401이 발생하지 않고 정상 처리**됩니다
- 등록된 유효한 API Key인 경우에만 사용량 추적이 적용됩니다
- 이는 기존 클라이언트의 하위 호환성을 보장합니다

### 3.6 공개 엔드포인트

다음 엔드포인트는 **인증 없이** 접근 가능합니다:

- 헬스 체크: `/api/health`, `/api/health/database`, `/api/health/ldap`
- 인증서 검색: `/api/certificates/search`, `/api/certificates/countries`, `/api/certificates/validation`
- 인증서 내보내기: `/api/certificates/export/*`
- PA 간편 조회: `/api/certificates/pa-lookup`
- 보고서: `/api/certificates/crl/report`, `/api/certificates/dsc-nc/report`, `/api/certificates/doc9303-checklist`
- PA 검증: `/api/pa/verify`, `/api/pa/parse-*`, `/api/pa/history`, `/api/pa/statistics`
- AI 분석: `/api/ai/*` (모든 엔드포인트)
- 동기화: `/api/sync/status`, `/api/sync/stats`
- ICAO: `/api/icao/*`
- 업로드 통계: `/api/upload/statistics`, `/api/upload/history`, `/api/upload/countries`
- 코드 마스터: `GET /api/code-master`

**인증 필수 엔드포인트** (JWT 또는 API Key with `upload:write`):
- 파일 업로드: `POST /api/upload/ldif`, `POST /api/upload/masterlist`, `POST /api/upload/certificate`
- 업로드 삭제: `DELETE /api/upload/{id}`
- 코드 마스터 수정: `POST/PUT/DELETE /api/code-master`

---

## 4. 공통 규약

### 4.1 요청 형식

| Content-Type | 용도 |
|-------------|------|
| `application/json` | POST/PUT 요청 본문 |
| `multipart/form-data` | 파일 업로드 |
| (없음) | GET 요청 |

문자 인코딩: **UTF-8**

### 4.2 응답 형식

**성공 응답** (200/201):
```json
{
  "success": true,
  "data": { ... },
  "message": "설명 (선택)"
}
```

**에러 응답** (400/401/403/404/500):
```json
{
  "success": false,
  "error": "에러 유형",
  "message": "상세 설명"
}
```

> 일부 엔드포인트는 `data` 래퍼 없이 최상위에 필드를 직접 반환합니다. 각 엔드포인트의 응답 예시를 참고하세요.

### 4.3 페이지네이션

**Offset 기반** (대부분의 엔드포인트):
```
GET /api/certificates/search?limit=50&offset=0
```

```json
{
  "success": true,
  "total": 1542,
  "limit": 50,
  "offset": 0,
  "data": [...]
}
```

**Page 기반** (PA 이력, AI 이상 목록):
```
GET /api/pa/history?page=0&size=20
GET /api/ai/anomalies?page=1&size=20
```

```json
{
  "total": 150,
  "items": [...]
}
```

| 파라미터 | 기본값 | 최대값 | 설명 |
|---------|--------|--------|------|
| `limit` | 50 | 200 | 결과 수 (offset 방식) |
| `offset` | 0 | - | 시작 위치 (offset 방식) |
| `page` | 0 또는 1 | - | 페이지 번호 |
| `size` | 20 | 100 | 페이지 크기 |

### 4.4 날짜/시간 형식

ISO 8601 형식: `2026-02-28T10:30:00` (UTC 또는 서버 로컬 시간)

### 4.5 국가 코드

ISO 3166-1 alpha-2 (대문자 2자리): `KR`, `US`, `JP`, `DE`, `FR` 등 (95+ 국가)

### 4.6 인증서 유형 코드

| 코드 | 설명 |
|------|------|
| `CSCA` | Country Signing Certificate Authority |
| `DSC` | Document Signer Certificate |
| `DSC_NC` | Non-Conformant DSC (ICAO 비준수) |
| `MLSC` | Master List Signer Certificate |
| `CRL` | Certificate Revocation List |
| `ML` | Master List |

### 4.7 HTTP 상태 코드

| 코드 | 설명 | 대응 |
|------|------|------|
| `200` | 성공 | - |
| `201` | 생성 성공 (업로드) | - |
| `400` | 잘못된 요청 | 요청 파라미터 확인 |
| `401` | 인증 실패 | API Key 또는 JWT 확인 |
| `403` | 권한 부족 / IP 차단 | 관리자에게 권한 확인 요청 |
| `404` | 리소스 없음 | ID/fingerprint 확인 |
| `409` | 중복 파일 | 이미 업로드된 파일 |
| `413` | 파일 크기 초과 | LDIF 100MB / ML 30MB 제한 |
| `429` | Rate Limit 초과 | `Retry-After` 만큼 대기 |
| `500` | 서버 오류 | 관리자에게 보고 |
| `503` | 서비스 불가 | 잠시 후 재시도 |

---

## 5. 인증서 검색 및 조회

### 5.1 인증서 검색

`GET /api/certificates/search`

| 파라미터 | 타입 | 필수 | 설명 |
|---------|------|------|------|
| `country` | string | 아니오 | 국가 코드 (예: KR, US) |
| `certType` | string | 아니오 | 인증서 유형: CSCA, DSC, DSC_NC, CRL, ML |
| `validity` | string | 아니오 | 유효성: VALID, EXPIRED, NOT_YET_VALID, all (기본: all) |
| `searchTerm` | string | 아니오 | CN 검색어 |
| `source` | string | 아니오 | 출처: LDIF_PARSED, ML_PARSED, FILE_UPLOAD, PA_EXTRACTED, DL_PARSED |
| `limit` | integer | 아니오 | 결과 수 (기본 50, 최대 200) |
| `offset` | integer | 아니오 | 시작 위치 (기본 0) |

> **주의**: `certType`만 지정하고 `country`를 지정하지 않으면 LDAP 계층 구조 특성상 유형 필터링이 효과적이지 않습니다. **항상 `country`와 함께 사용**하세요.

```bash
# 한국 DSC 인증서 검색
curl -s "https://pkd.smartcoreinc.com/api/certificates/search?country=KR&certType=DSC&limit=10" \
  -H "X-API-Key: $API_KEY" | jq .
```

**응답**:
```json
{
  "success": true,
  "total": 227,
  "limit": 10,
  "offset": 0,
  "certificates": [
    {
      "dn": "cn=a1b2c3...,o=dsc,c=KR,dc=data,...",
      "cn": "DS0120080307 1",
      "sn": "0D",
      "country": "KR",
      "certType": "DSC",
      "subjectDn": "CN=DS0120080307 1,O=certificates,C=KR",
      "issuerDn": "CN=CSCA,OU=MOFA,O=Government,C=KR",
      "fingerprint": "a1b2c3d4...",
      "isSelfSigned": false,
      "validFrom": "2008-03-07T00:00:00Z",
      "validTo": "2018-03-07T23:59:59Z",
      "validity": "EXPIRED",
      "version": 3,
      "signatureAlgorithm": "RSA",
      "signatureHashAlgorithm": "SHA-256",
      "publicKeyAlgorithm": "RSA",
      "publicKeySize": 2048,
      "isCA": false
    }
  ]
}
```

### 5.2 국가 목록

`GET /api/certificates/countries`

```bash
curl -s "https://pkd.smartcoreinc.com/api/certificates/countries" | jq .
```

### 5.3 검증 결과 조회

`GET /api/certificates/validation?fingerprint={sha256}`

Trust Chain 검증 결과를 fingerprint로 조회합니다.

### 5.4 PA 간편 조회 (Lightweight PA Lookup)

`POST /api/certificates/pa-lookup`

SOD/DG 파일 없이, DSC의 Subject DN 또는 Fingerprint만으로 기존 Trust Chain 검증 결과를 DB에서 즉시 조회합니다 (5~20ms 응답).

| 전체 검증 (`/api/pa/verify`) | 간편 조회 (`/api/certificates/pa-lookup`) |
|--------------------------|---------------------------------------|
| SOD + Data Groups (Base64) 필요 | Subject DN 또는 Fingerprint만 필요 |
| CMS 파싱, 서명 검증, 해시 비교 | DB 단순 조회 |
| 100~500ms | 5~20ms |
| 최초 여권 검증 | 이미 알려진 DSC 상태 확인 |

**요청**:
```json
{
  "subjectDn": "/C=KR/O=Government of Korea/CN=Document Signer 1234"
}
```
또는:
```json
{
  "fingerprint": "a1b2c3d4e5f6789012345678901234567890123456789012345678901234abcd"
}
```

> `subjectDn`과 `fingerprint` 중 하나만 제공하면 됩니다. 둘 다 제공 시 `subjectDn`이 우선입니다.

```bash
curl -s -X POST "https://pkd.smartcoreinc.com/api/certificates/pa-lookup" \
  -H "Content-Type: application/json" \
  -H "X-API-Key: $API_KEY" \
  -d '{"subjectDn": "/C=KR/O=Government of Korea/CN=Document Signer 1234"}' | jq .
```

**응답 (결과 존재)**:
```json
{
  "success": true,
  "validation": {
    "id": "660e8400-e29b-41d4-a716-446655440001",
    "certificateType": "DSC",
    "countryCode": "KR",
    "subjectDn": "/C=KR/O=Government of Korea/CN=Document Signer 1234",
    "issuerDn": "/C=KR/O=Government of Korea/CN=Country Signing CA KR",
    "serialNumber": "1A2B3C4D",
    "validationStatus": "VALID",
    "trustChainValid": true,
    "trustChainPath": "DSC → CSCA",
    "cscaFound": true,
    "cscaSubjectDn": "/C=KR/O=Government of Korea/CN=Country Signing CA KR",
    "signatureValid": true,
    "signatureAlgorithm": "SHA256withRSA",
    "validityPeriodValid": true,
    "notBefore": "2024-01-01 00:00:00",
    "notAfter": "2029-12-31 23:59:59",
    "revocationStatus": "not_revoked",
    "crlChecked": true,
    "fingerprintSha256": "a1b2c3d4e5f6...",
    "validatedAt": "2026-02-14T10:30:00"
  }
}
```

**응답 (결과 없음)**:
```json
{
  "success": true,
  "validation": null,
  "message": "No validation result found for the given subjectDn"
}
```

| 필드 | 설명 |
|------|------|
| `validationStatus` | `VALID`, `EXPIRED_VALID`, `INVALID`, `PENDING`, `ERROR` |
| `trustChainValid` | DSC → CSCA 신뢰 체인 검증 성공 여부 |
| `trustChainPath` | 체인 경로 (예: "DSC → Link → CSCA") |
| `cscaFound` | CSCA 인증서 검색 성공 여부 |
| `revocationStatus` | `not_revoked`, `revoked`, `unknown` |

### 5.5 Doc 9303 적합성 체크리스트

`GET /api/certificates/doc9303-checklist?fingerprint={sha256}`

인증서의 ICAO Doc 9303 표준 적합성을 ~28개 항목으로 검사합니다.

```bash
curl -s "https://pkd.smartcoreinc.com/api/certificates/doc9303-checklist?fingerprint=a1b2c3d4..." | jq .
```

### 5.6 인증서 내보내기

| 엔드포인트 | 설명 |
|-----------|------|
| `GET /api/certificates/export/file?dn={DN}&format=pem` | 단일 인증서 내보내기 (PEM/DER) |
| `GET /api/certificates/export/country?country=KR&format=pem` | 국가별 ZIP |
| `GET /api/certificates/export/all?format=pem` | 전체 LDAP 데이터 DIT 구조 ZIP |

```bash
# 단일 인증서 PEM 다운로드
curl -s "https://pkd.smartcoreinc.com/api/certificates/export/file?dn={DN}&format=pem" \
  -H "X-API-Key: $API_KEY" -o cert.pem

# 한국 인증서 전체 ZIP
curl -s "https://pkd.smartcoreinc.com/api/certificates/export/country?country=KR&format=pem" \
  -H "X-API-Key: $API_KEY" -o KR_certs.zip

# 전체 인증서 ZIP (DIT 구조: data/{country}/{type}/)
curl -s "https://pkd.smartcoreinc.com/api/certificates/export/all?format=der" \
  -H "X-API-Key: $API_KEY" -o all_certs.zip
```

---

## 6. CRL 보고서

### 6.1 CRL 보고서 조회

`GET /api/certificates/crl/report`

| 파라미터 | 타입 | 설명 |
|---------|------|------|
| `country` | string | 국가 코드 필터 |
| `page` | integer | 페이지 번호 |
| `size` | integer | 페이지 크기 |

```bash
curl -s "https://pkd.smartcoreinc.com/api/certificates/crl/report" | jq .
```

**응답**:
```json
{
  "success": true,
  "summary": {
    "totalCrls": 69,
    "totalCountries": 67,
    "validCount": 45,
    "expiredCount": 24,
    "totalRevoked": 170
  },
  "byCountry": [{"country": "KR", "count": 2}, ...],
  "bySignatureAlgorithm": [{"algorithm": "SHA256withRSA", "count": 40}, ...],
  "byRevocationReason": [{"reason": "keyCompromise", "count": 5}, ...],
  "crls": [
    {
      "id": "...",
      "countryCode": "KR",
      "issuerDn": "CN=CSCA KR,...",
      "thisUpdate": "2026-02-01T00:00:00",
      "nextUpdate": "2026-03-01T00:00:00",
      "signatureAlgorithm": "SHA256withRSA",
      "revokedCount": 3,
      "status": "VALID"
    }
  ]
}
```

### 6.2 CRL 상세 조회

`GET /api/certificates/crl/{id}`

폐기된 인증서 목록과 폐기 사유를 포함합니다.

**RFC 5280 폐기 사유 코드**:

| 코드 | 설명 |
|------|------|
| `unspecified` | 미지정 |
| `keyCompromise` | 키 손상 |
| `cACompromise` | CA 손상 |
| `affiliationChanged` | 소속 변경 |
| `superseded` | 대체됨 |
| `cessationOfOperation` | 운영 중단 |
| `certificateHold` | 인증서 보류 |
| `removeFromCRL` | CRL에서 제거 |
| `privilegeWithdrawn` | 권한 철회 |
| `aACompromise` | AA 손상 |

### 6.3 CRL 바이너리 다운로드

`GET /api/certificates/crl/{id}/download`

```bash
curl -s "https://pkd.smartcoreinc.com/api/certificates/crl/{id}/download" \
  -o certificate.crl
```

Content-Type: `application/pkix-crl`

---

## 7. DSC_NC 비준수 인증서 보고서

`GET /api/certificates/dsc-nc/report`

ICAO Doc 9303 기술 사양을 준수하지 않는 DSC 인증서 분석 보고서입니다.

| 파라미터 | 타입 | 설명 |
|---------|------|------|
| `country` | string | 국가 코드 필터 |
| `conformanceCode` | string | 비준수 사유 코드 필터 (접두사 매칭) |
| `page` | integer | 페이지 번호 |
| `size` | integer | 페이지 크기 |

```bash
curl -s "https://pkd.smartcoreinc.com/api/certificates/dsc-nc/report" | jq .
```

**응답 (요약)**:
```json
{
  "success": true,
  "summary": {
    "totalDscNc": 502,
    "countryCount": 45,
    "conformanceCodeCount": 12,
    "expirationRate": 0.85
  },
  "conformanceCodes": [{"code": "ERR:CSCA.CDP.14", "count": 120}, ...],
  "byCountry": [{"country": "DE", "total": 45, "valid": 5, "expired": 40}, ...],
  "certificates": [...]
}
```

---

## 8. PA 검증 (Passive Authentication)

### 8.1 개요

PA 검증은 ICAO 9303 Part 10/11 표준에 따른 전자여권 진위 검증입니다. 전자여권 판독기에서 읽은 SOD와 Data Groups를 전송하면 **8단계 검증 프로세스**를 수행합니다.

| Step | 이름 | 설명 |
|------|------|------|
| 1 | **SOD Parse** | SOD에서 CMS 구조, 해시 알고리즘, DG 해시 추출 |
| 2 | **DSC Extract** | SOD의 SignedData에서 DSC 인증서 추출 |
| 3 | **Trust Chain** | DSC → CSCA 신뢰 체인 검증 (공개키 서명 검증) |
| 4 | **CSCA Lookup** | LDAP에서 CSCA 인증서 검색 (Link Certificate 포함) |
| 5 | **SOD Signature** | SOD 서명 유효성 검증 |
| 6 | **DG Hash** | Data Group 해시값 검증 (SOD 내 기대값과 비교) |
| 7 | **CRL Check** | CRL 유효기간 확인 + DSC 인증서 폐지 여부 확인 |
| 8 | **DSC Registration** | 신규 DSC를 Local PKD에 자동 등록 |

### 8.2 전체 검증

`POST /api/pa/verify`

**요청**:
```json
{
  "sod": "<Base64 encoded SOD>",
  "dataGroups": {
    "1": "<Base64 encoded DG1>",
    "2": "<Base64 encoded DG2>"
  },
  "issuingCountry": "KR",
  "documentNumber": "M12345678",
  "requestedBy": "agent-system"
}
```

| 필드 | 타입 | 필수 | 설명 |
|------|------|------|------|
| `sod` | string | **필수** | Base64 인코딩된 SOD |
| `dataGroups` | object | **필수** | DG 번호 → Base64 데이터 맵 |
| `issuingCountry` | string | 선택 | 국가 코드 (DSC `C=` → DG1 MRZ 순으로 자동 추출) |
| `documentNumber` | string | 선택 | 여권 번호 (DG1 MRZ에서 자동 추출 가능) |
| `requestedBy` | string | 선택 | 요청자 식별 (기본: `anonymous`) |

> `dataGroups` 키는 `"1"`, `"2"` (숫자 문자열) 또는 `"DG1"`, `"DG2"` 형식 모두 지원됩니다.

```bash
curl -s -X POST "https://pkd.smartcoreinc.com/api/pa/verify" \
  -H "Content-Type: application/json" \
  -H "X-API-Key: $API_KEY" \
  -d '{
    "sod": "'$(base64 -w0 sod.bin)'",
    "dataGroups": {
      "1": "'$(base64 -w0 dg1.bin)'",
      "2": "'$(base64 -w0 dg2.bin)'"
    }
  }' | jq .
```

**응답 (VALID)**:
```json
{
  "success": true,
  "data": {
    "verificationId": "550e8400-e29b-41d4-a716-446655440000",
    "status": "VALID",
    "verificationTimestamp": "2026-02-28T10:30:00",
    "processingDurationMs": 245,
    "issuingCountry": "KR",
    "documentNumber": "M12345678",

    "certificateChainValidation": {
      "valid": true,
      "dscSubject": "/C=KR/O=Ministry of Foreign Affairs/CN=Document Signer KR 01",
      "dscSerialNumber": "1A2B3C4D5E6F",
      "cscaSubject": "/C=KR/O=Ministry of Foreign Affairs/CN=Country Signing CA KR",
      "cscaFingerprint": "SHA256:ABCD1234...",
      "countryCode": "KR",
      "notBefore": "2024-01-01T00:00:00Z",
      "notAfter": "2029-12-31T23:59:59Z",
      "crlStatus": "NOT_REVOKED",
      "crlThisUpdate": "2026-02-01T00:00:00",
      "crlNextUpdate": "2026-03-01T00:00:00",
      "dscExpired": false,
      "cscaExpired": false,
      "validAtSigningTime": true,
      "expirationStatus": "VALID"
    },

    "sodSignatureValidation": {
      "valid": true,
      "hashAlgorithm": "SHA-256",
      "signatureAlgorithm": "SHA256withRSA"
    },

    "dataGroupValidation": {
      "totalGroups": 2,
      "validGroups": 2,
      "invalidGroups": 0,
      "details": {
        "DG1": { "valid": true, "expectedHash": "abc123...", "actualHash": "abc123..." },
        "DG2": { "valid": true, "expectedHash": "def456...", "actualHash": "def456..." }
      }
    },

    "dscAutoRegistration": {
      "registered": true,
      "newlyRegistered": false,
      "certificateId": "660e8400-e29b-41d4-a716-446655440099",
      "fingerprint": "a1b2c3d4e5f6...",
      "countryCode": "KR"
    }
  }
}
```

**응답 (INVALID)**:
```json
{
  "success": true,
  "data": {
    "verificationId": "550e8400-e29b-41d4-a716-446655440001",
    "status": "INVALID",
    "certificateChainValidation": {
      "valid": false,
      "message": "CSCA certificate not found for issuer",
      "errorCode": "CSCA_NOT_FOUND"
    },
    "sodSignatureValidation": { "valid": true },
    "dataGroupValidation": { "totalGroups": 2, "validGroups": 2, "invalidGroups": 0 }
  }
}
```

**응답 (에러)**:
```json
{
  "success": false,
  "error": "SOD parsing failed: Invalid CMS structure"
}
```

#### Certificate Chain Validation 필드

| 필드 | 타입 | 설명 |
|------|------|------|
| `valid` | boolean | 인증서 체인 검증 성공 여부 |
| `dscSubject` | string | DSC Subject DN |
| `cscaSubject` | string | CSCA Subject DN |
| `countryCode` | string | 국가 코드 |
| `notBefore` / `notAfter` | string | DSC 유효 기간 |
| `crlStatus` | string | `NOT_REVOKED`, `REVOKED`, `CRL_EXPIRED`, `UNKNOWN` |
| `dscExpired` / `cscaExpired` | boolean | 만료 여부 |
| `validAtSigningTime` | boolean | 여권 서명 당시 유효 여부 (Point-in-Time Validation) |
| `dscNonConformant` | boolean | ICAO PKD 비준수 DSC인 경우 `true` (해당 시에만 포함) |
| `pkdConformanceCode` | string | 비준수 사유 코드 (해당 시에만 포함) |

> **Point-in-Time Validation**: ICAO 9303 표준에 따라, 인증서가 현재 만료되었더라도 여권 서명 당시에 유효했다면 `validAtSigningTime`이 `true`로 설정됩니다.

#### DSC Auto-Registration 필드

| 필드 | 타입 | 설명 |
|------|------|------|
| `registered` | boolean | DSC 등록 성공 여부 |
| `newlyRegistered` | boolean | `true`: 신규 등록, `false`: 이미 존재 |
| `fingerprint` | string | DSC SHA-256 지문 (hex, 64자) |

### 8.3 SOD 파싱

`POST /api/pa/parse-sod`

```json
{ "sod": "<Base64 encoded SOD>" }
```

**응답**: SOD 메타데이터 (해시 알고리즘, DSC 정보, 포함된 DG 해시 목록)

### 8.4 DG1 파싱 (MRZ)

`POST /api/pa/parse-dg1`

```json
{ "dg1": "<Base64 encoded DG1>" }
```

**응답**:
```json
{
  "success": true,
  "documentType": "P",
  "issuingCountry": "KOR",
  "surname": "KIM",
  "givenNames": "MINHO",
  "fullName": "KIM MINHO",
  "documentNumber": "M12345678",
  "nationality": "KOR",
  "dateOfBirth": "1990-05-15",
  "sex": "M",
  "dateOfExpiry": "2030-05-14"
}
```

### 8.5 DG2 파싱 (얼굴 이미지)

`POST /api/pa/parse-dg2`

```json
{ "dg2": "<Base64 encoded DG2>" }
```

**응답**:
```json
{
  "success": true,
  "faceCount": 1,
  "faceImages": [
    {
      "index": 1,
      "imageFormat": "JPEG",
      "originalFormat": "JPEG2000",
      "width": 480,
      "height": 640,
      "imageDataUrl": "data:image/jpeg;base64,/9j/4AAQ..."
    }
  ]
}
```

> JPEG2000 이미지는 서버에서 자동으로 JPEG로 변환됩니다.

### 8.6 MRZ 텍스트 파싱

`POST /api/pa/parse-mrz-text`

```json
{ "mrz": "P<KORKIM<<MINHO<<<<<<<<<<<<<<<<<<<<<<<<<<<<M123456784KOR9005151M3005148<<<<<<<<<<<<<<02" }
```

### 8.7 검증 이력

`GET /api/pa/history?page=0&size=20&status=VALID&issuingCountry=KR`

### 8.8 검증 상세

`GET /api/pa/{verificationId}`

### 8.9 Data Groups 상세

`GET /api/pa/{verificationId}/datagroups`

DG1 MRZ 파싱 결과와 DG2 얼굴 이미지(JPEG)를 포함합니다.

### 8.10 검증 통계

`GET /api/pa/statistics`

```json
{
  "totalVerifications": 1500,
  "successRate": 90.0,
  "byCountry": [{"country": "KR", "count": 500}, ...],
  "byStatus": {"VALID": 1350, "INVALID": 150}
}
```

### 8.11 Data Group 참조

| DG | 이름 | 내용 | PA 필요도 |
|----|------|------|----------|
| DG1 | MRZ | Machine Readable Zone | **권장** |
| DG2 | Face | 얼굴 이미지 (JPEG/JPEG2000) | **권장** |
| DG3 | Finger | 지문 | 선택 |
| DG14 | Security | Active Auth / PACE 정보 | 선택 |
| DG15 | AA Key | Active Authentication 공개키 | 선택 |

> PA 검증 최소 요구: **SOD + 1개 이상의 DG**

---

## 9. AI 인증서 분석

ML 기반 인증서 이상 탐지 및 패턴 분석 서비스입니다. Isolation Forest + Local Outlier Factor 이중 모델로 45개 특성을 분석합니다. **모든 AI 엔드포인트는 인증 불필요(Public)입니다.**

### 9.1 개별 인증서 분석 결과

`GET /api/ai/certificate/{fingerprint}`

```bash
curl -s "https://pkd.smartcoreinc.com/api/ai/certificate/a1b2c3d4..." | jq .
```

**응답**:
```json
{
  "fingerprint": "a1b2c3d4...",
  "certificate_type": "DSC",
  "country_code": "KR",
  "anomaly_score": 0.12,
  "anomaly_label": "NORMAL",
  "risk_score": 15.0,
  "risk_level": "LOW",
  "risk_factors": {
    "algorithm": 5,
    "key_size": 10,
    "compliance": 0,
    "validity": 0,
    "extensions": 0,
    "anomaly": 0
  },
  "anomaly_explanations": [
    "국가 평균 대비 유효기간 편차: 평균 대비 1.2σ 낮음",
    "키 크기: 평균 대비 0.8σ 낮음"
  ],
  "analyzed_at": "2026-02-28T03:00:05"
}
```

| 필드 | 설명 |
|------|------|
| `anomaly_score` | 이상 점수 0.0 (정상) ~ 1.0 (이상) |
| `anomaly_label` | `NORMAL` (<0.3), `SUSPICIOUS` (0.3~0.7), `ANOMALOUS` (>=0.7) |
| `risk_score` | 위험 점수 0~100 (복합 점수) |
| `risk_level` | `LOW` (0~25), `MEDIUM` (26~50), `HIGH` (51~75), `CRITICAL` (76~100) |
| `risk_factors` | 6개 카테고리별 위험 기여 점수 |
| `anomaly_explanations` | 상위 5개 기여 피처와 sigma 편차 (한국어) |

### 9.2 포렌식 상세

`GET /api/ai/certificate/{fingerprint}/forensic`

10개 포렌식 카테고리별 상세 분석:

| 카테고리 | 설명 |
|---------|------|
| `algorithm` | 서명 알고리즘 적합성 |
| `key_size` | 키 크기 적합성 |
| `compliance` | ICAO 준수성 |
| `validity` | 유효 기간 적절성 |
| `extensions` | 확장 필드 적합성 |
| `anomaly` | 이상치 수준 |
| `issuer_reputation` | 발급자 신뢰도 |
| `structural_consistency` | 구조적 일관성 |
| `temporal_pattern` | 시간적 패턴 |
| `dn_consistency` | DN 일관성 |

### 9.3 이상 인증서 목록

`GET /api/ai/anomalies?country=KR&label=ANOMALOUS&risk_level=HIGH&page=1&size=20`

| 파라미터 | 설명 |
|---------|------|
| `country` | 국가 코드 필터 |
| `type` | 인증서 유형 필터 (CSCA, DSC, DSC_NC, MLSC) |
| `label` | 이상 수준 필터 (NORMAL, SUSPICIOUS, ANOMALOUS) |
| `risk_level` | 위험 수준 필터 (LOW, MEDIUM, HIGH, CRITICAL) |
| `page` | 페이지 번호 (1부터) |
| `size` | 페이지 크기 (최대 100) |

### 9.4 전체 분석 통계

`GET /api/ai/statistics`

```json
{
  "total_analyzed": 31212,
  "normal_count": 27305,
  "suspicious_count": 3905,
  "anomalous_count": 2,
  "risk_distribution": {"LOW": 22396, "MEDIUM": 7405, "HIGH": 919, "CRITICAL": 492},
  "avg_risk_score": 24.75,
  "last_analysis_at": "2026-02-28T03:00:05"
}
```

### 9.5 분석 실행/상태

**분석 실행**: `POST /api/ai/analyze` (비동기 백그라운드, 이미 실행 중이면 `409 Conflict`)

**증분 분석**: `POST /api/ai/analyze/incremental` (upload_id 기반)

**진행 상태**: `GET /api/ai/analyze/status`

| status | 설명 |
|--------|------|
| `IDLE` | 미실행/초기 상태 |
| `RUNNING` | 분석 진행 중 |
| `COMPLETED` | 분석 완료 |
| `FAILED` | 분석 실패 |

### 9.6 리포트 API

| 엔드포인트 | 설명 |
|-----------|------|
| `GET /api/ai/reports/country-maturity` | 국가별 PKI 성숙도 순위 |
| `GET /api/ai/reports/algorithm-trends` | 연도별 알고리즘 사용 추이 |
| `GET /api/ai/reports/key-size-distribution` | 알고리즘 군별 키 크기 분포 |
| `GET /api/ai/reports/risk-distribution` | 위험 수준별 분포 |
| `GET /api/ai/reports/country/{code}` | 특정 국가 상세 분석 |
| `GET /api/ai/reports/issuer-profiles` | 발급자별 프로파일 분석 |
| `GET /api/ai/reports/forensic-summary` | 포렌식 분석 요약 |
| `GET /api/ai/reports/extension-anomalies` | 확장 필드 규칙 위반 목록 |

### 9.7 PA + AI 연동 워크플로우

PA 검증 후 DSC fingerprint로 AI 분석 결과를 조합하여 종합 신뢰도를 평가합니다:

```
1. POST /api/pa/verify         → PA 검증 수행, DSC fingerprint 획득
2. GET /api/ai/certificate/{fp} → 해당 DSC의 AI 이상 탐지 결과 조회
3. PA 결과 (VALID/INVALID) + AI 위험 수준 (LOW~CRITICAL) 종합 판단
```

---

## 10. 동기화 모니터링

DB와 LDAP 간 인증서 동기화 상태를 모니터링합니다.

| 엔드포인트 | Method | 설명 |
|-----------|--------|------|
| `/api/sync/status` | GET | DB-LDAP 동기화 상태 (IN_SYNC / OUT_OF_SYNC) |
| `/api/sync/stats` | GET | 동기화 통계 (성공/실패 횟수, DB/LDAP 카운트) |
| `/api/sync/check` | POST | 수동 동기화 체크 실행 |
| `/api/sync/discrepancies` | GET | 현재 불일치 목록 |
| `/api/sync/reconcile` | POST | 누락 LDAP 엔트리 보정 실행 |
| `/api/sync/reconcile/history` | GET | 보정 이력 |
| `/api/sync/reconcile/{id}` | GET | 보정 상세 |
| `/api/sync/reconcile/stats` | GET | 보정 통계 |

```bash
# 동기화 상태 확인
curl -s "https://pkd.smartcoreinc.com/api/sync/status" | jq .
```

**응답**:
```json
{
  "status": "IN_SYNC",
  "databaseCount": 31212,
  "ldapCount": 31212,
  "lastChecked": "2026-02-28T03:00:00",
  "byType": {
    "CSCA": {"db": 845, "ldap": 845},
    "DSC": {"db": 29838, "ldap": 29838},
    "CRL": {"db": 69, "ldap": 69}
  }
}
```

---

## 11. ICAO PKD 버전 모니터링

ICAO PKD 최신 버전과 로컬 버전을 비교합니다.

| 엔드포인트 | Method | 설명 |
|-----------|--------|------|
| `/api/icao/status` | GET | 버전 비교 상태 (업데이트 필요 여부) |
| `/api/icao/latest` | GET | 최신 ICAO 버전 정보 |
| `/api/icao/history` | GET | 버전 체크 이력 |
| `/api/icao/check-updates` | GET | 수동 버전 체크 실행 |

---

## 12. 업로드 통계 및 이력

인증서 업로드 통계를 조회합니다 (읽기 전용).

| 엔드포인트 | Method | 설명 |
|-----------|--------|------|
| `/api/upload/statistics` | GET | 업로드 통계 요약 (유형별, 출처별 카운트) |
| `/api/upload/history` | GET | 업로드 이력 (페이지네이션) |
| `/api/upload/detail/{id}` | GET | 업로드 상세 |
| `/api/upload/countries` | GET | 국가별 통계 |
| `/api/upload/countries/detailed` | GET | 국가별 상세 카운트 |

---

## 13. 코드 마스터 (참조 데이터)

시스템에서 사용하는 코드/상태/열거형 값을 조회합니다 (읽기 전용). 21개 카테고리, ~150개 코드.

| 엔드포인트 | Method | 설명 |
|-----------|--------|------|
| `/api/code-master?category=CERTIFICATE_TYPE` | GET | 카테고리별 코드 목록 |
| `/api/code-master/categories` | GET | 카테고리 목록 |
| `/api/code-master/{id}` | GET | 코드 상세 |

주요 카테고리: `CERTIFICATE_TYPE`, `VALIDATION_STATUS`, `CRL_STATUS`, `CRL_REVOCATION_REASON`, `UPLOAD_STATUS`, `OPERATION_TYPE`, `PA_ERROR_CODE` 등

---

## 14. 시스템 모니터링

| 엔드포인트 | 서비스 | 설명 |
|-----------|--------|------|
| `GET /api/health` | PKD Management | 서비스 상태 |
| `GET /api/health/database` | PKD Management | DB 연결 상태 |
| `GET /api/health/ldap` | PKD Management | LDAP 연결 상태 |
| `GET /api/sync/health` | PKD Relay | Relay 서비스 상태 |
| `GET /api/ai/health` | AI Analysis | AI 서비스 상태 |
| `GET /api/monitoring/health` | Monitoring | 모니터링 서비스 상태 |

```json
{
  "service": "pkd-management",
  "status": "UP",
  "version": "2.25.5",
  "timestamp": "2026-02-28T10:30:00Z"
}
```

---

## 15. 연동 예제

### 15.1 Python

```python
"""
ICAO Local PKD — Python SDK 클래스
pip install requests
"""
import requests
import base64
import time


class IcaoPkdClient:
    def __init__(self, base_url="https://pkd.smartcoreinc.com/api",
                 api_key=None, ca_cert=None):
        self.base_url = base_url
        self.headers = {"Content-Type": "application/json"}
        if api_key:
            self.headers["X-API-Key"] = api_key
        self.verify = ca_cert if ca_cert else True  # ca.crt 경로 또는 True

    def _get(self, path, params=None):
        resp = requests.get(f"{self.base_url}{path}",
                            params=params, headers=self.headers,
                            verify=self.verify)
        if resp.status_code == 429:
            retry_after = int(resp.headers.get("Retry-After", 60))
            time.sleep(retry_after)
            return self._get(path, params)
        resp.raise_for_status()
        return resp.json()

    def _post(self, path, json_data):
        resp = requests.post(f"{self.base_url}{path}",
                             json=json_data, headers=self.headers,
                             verify=self.verify)
        if resp.status_code == 429:
            retry_after = int(resp.headers.get("Retry-After", 60))
            time.sleep(retry_after)
            return self._post(path, json_data)
        resp.raise_for_status()
        return resp.json()

    # --- 인증서 검색 ---
    def search_certificates(self, country=None, cert_type=None,
                            validity=None, limit=50, offset=0):
        params = {"limit": limit, "offset": offset}
        if country: params["country"] = country
        if cert_type: params["certType"] = cert_type
        if validity: params["validity"] = validity
        return self._get("/certificates/search", params)

    # --- PA 간편 조회 ---
    def pa_lookup(self, subject_dn=None, fingerprint=None):
        body = {}
        if subject_dn: body["subjectDn"] = subject_dn
        elif fingerprint: body["fingerprint"] = fingerprint
        return self._post("/certificates/pa-lookup", body)

    # --- PA 전체 검증 ---
    def pa_verify(self, sod_bytes, data_groups):
        """
        Args:
            sod_bytes: SOD 바이너리 (bytes)
            data_groups: {1: dg1_bytes, 2: dg2_bytes}
        """
        return self._post("/pa/verify", {
            "sod": base64.b64encode(sod_bytes).decode(),
            "dataGroups": {
                str(k): base64.b64encode(v).decode()
                for k, v in data_groups.items()
            }
        })

    # --- AI 분석 ---
    def get_ai_analysis(self, fingerprint):
        return self._get(f"/ai/certificate/{fingerprint}")

    def get_ai_statistics(self):
        return self._get("/ai/statistics")


# 사용 예시
if __name__ == "__main__":
    client = IcaoPkdClient(
        api_key="icao_NggoCnqh_a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6",
        ca_cert="/path/to/ca.crt"  # HTTPS 사용 시
    )

    # 인증서 검색
    result = client.search_certificates(country="KR", cert_type="DSC", limit=5)
    print(f"총 {result.get('total', 0)}건")

    # PA 간편 조회
    lookup = client.pa_lookup(
        subject_dn="/C=KR/O=Government of Korea/CN=Document Signer 1234")
    if lookup.get("validation"):
        print(f"Status: {lookup['validation']['validationStatus']}")

    # PA 전체 검증 (SOD/DG 바이너리 필요)
    # result = client.pa_verify(sod_bytes, {1: dg1_bytes, 2: dg2_bytes})

    # AI 분석 결합
    # ai = client.get_ai_analysis(fingerprint)
    # print(f"Risk: {ai['risk_level']}, Score: {ai['risk_score']}")
```

### 15.2 Java (JDK 11+)

```java
import java.net.URI;
import java.net.http.*;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.Base64;

public class IcaoPkdClient {
    private static final String BASE_URL = "https://pkd.smartcoreinc.com/api";
    private final String apiKey;
    private final HttpClient client;

    public IcaoPkdClient(String apiKey) {
        this.apiKey = apiKey;
        // Private CA 사용 시 SSLContext 설정 필요
        this.client = HttpClient.newBuilder()
                .version(HttpClient.Version.HTTP_1_1)
                .build();
    }

    private HttpRequest.Builder baseRequest(String path) {
        var builder = HttpRequest.newBuilder()
                .uri(URI.create(BASE_URL + path));
        if (apiKey != null) {
            builder.header("X-API-Key", apiKey);
        }
        return builder;
    }

    /** 인증서 검색 */
    public String searchCertificates(String country, String certType, int limit)
            throws Exception {
        var request = baseRequest(String.format(
                "/certificates/search?country=%s&certType=%s&limit=%d",
                country, certType, limit))
                .GET().build();
        return client.send(request, HttpResponse.BodyHandlers.ofString()).body();
    }

    /** PA 간편 조회 */
    public String paLookup(String subjectDn) throws Exception {
        var body = String.format("{\"subjectDn\":\"%s\"}", subjectDn);
        var request = baseRequest("/certificates/pa-lookup")
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(body)).build();
        return client.send(request, HttpResponse.BodyHandlers.ofString()).body();
    }

    /** PA 전체 검증 */
    public String paVerify(Path sodPath, Path dg1Path) throws Exception {
        var sodB64 = Base64.getEncoder().encodeToString(Files.readAllBytes(sodPath));
        var dg1B64 = Base64.getEncoder().encodeToString(Files.readAllBytes(dg1Path));
        var body = String.format(
                "{\"sod\":\"%s\",\"dataGroups\":{\"1\":\"%s\"}}", sodB64, dg1B64);
        var request = baseRequest("/pa/verify")
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(body)).build();
        return client.send(request, HttpResponse.BodyHandlers.ofString()).body();
    }

    /** AI 분석 결과 */
    public String getAiAnalysis(String fingerprint) throws Exception {
        var request = baseRequest("/ai/certificate/" + fingerprint)
                .GET().build();
        return client.send(request, HttpResponse.BodyHandlers.ofString()).body();
    }
}
```

### 15.3 C# (.NET 6+)

```csharp
using System.Net.Http;
using System.Net.Http.Json;
using System.Text;
using System.Text.Json;

public class IcaoPkdClient
{
    private const string BaseUrl = "https://pkd.smartcoreinc.com/api";
    private readonly HttpClient _client;

    public IcaoPkdClient(string apiKey = null)
    {
        // Private CA 사용 시 HttpClientHandler.ServerCertificateCustomValidationCallback 설정
        _client = new HttpClient();
        if (!string.IsNullOrEmpty(apiKey))
            _client.DefaultRequestHeaders.Add("X-API-Key", apiKey);
    }

    public async Task<string> SearchCertificatesAsync(
        string country, string certType = "DSC", int limit = 10)
    {
        var resp = await _client.GetAsync(
            $"{BaseUrl}/certificates/search?country={country}&certType={certType}&limit={limit}");
        resp.EnsureSuccessStatusCode();
        return await resp.Content.ReadAsStringAsync();
    }

    public async Task<string> PaLookupAsync(string subjectDn)
    {
        var content = new StringContent(
            JsonSerializer.Serialize(new { subjectDn }),
            Encoding.UTF8, "application/json");
        var resp = await _client.PostAsync($"{BaseUrl}/certificates/pa-lookup", content);
        resp.EnsureSuccessStatusCode();
        return await resp.Content.ReadAsStringAsync();
    }

    public async Task<string> PaVerifyAsync(string sodPath, string dg1Path)
    {
        var sodB64 = Convert.ToBase64String(await File.ReadAllBytesAsync(sodPath));
        var dg1B64 = Convert.ToBase64String(await File.ReadAllBytesAsync(dg1Path));
        var content = new StringContent(JsonSerializer.Serialize(new {
            sod = sodB64,
            dataGroups = new Dictionary<string, string> { { "1", dg1B64 } }
        }), Encoding.UTF8, "application/json");
        var resp = await _client.PostAsync($"{BaseUrl}/pa/verify", content);
        resp.EnsureSuccessStatusCode();
        return await resp.Content.ReadAsStringAsync();
    }

    public async Task<string> GetAiAnalysisAsync(string fingerprint)
    {
        var resp = await _client.GetAsync($"{BaseUrl}/ai/certificate/{fingerprint}");
        resp.EnsureSuccessStatusCode();
        return await resp.Content.ReadAsStringAsync();
    }
}
```

### 15.4 curl 명령어 모음

```bash
API_KEY="icao_NggoCnqh_a1b2c3d4e5f6g7h8i9j0k1l2m3n4o5p6"
BASE="https://pkd.smartcoreinc.com"

# ===== 헬스 체크 (인증 불필요) =====
curl -s "$BASE/api/health" | jq .
curl -s "$BASE/api/health/database" | jq .

# ===== 인증서 검색 =====
curl -s "$BASE/api/certificates/search?country=KR&certType=DSC&limit=5" \
  -H "X-API-Key: $API_KEY" | jq .

# ===== 국가 목록 =====
curl -s "$BASE/api/certificates/countries" | jq .

# ===== PA 간편 조회 =====
curl -s -X POST "$BASE/api/certificates/pa-lookup" \
  -H "Content-Type: application/json" \
  -H "X-API-Key: $API_KEY" \
  -d '{"subjectDn": "/C=KR/O=Government of Korea/CN=Document Signer 1234"}' | jq .

# ===== PA 전체 검증 =====
curl -s -X POST "$BASE/api/pa/verify" \
  -H "Content-Type: application/json" \
  -H "X-API-Key: $API_KEY" \
  -d '{
    "sod": "'$(base64 -w0 sod.bin)'",
    "dataGroups": {"1": "'$(base64 -w0 dg1.bin)'", "2": "'$(base64 -w0 dg2.bin)'"}
  }' | jq .

# ===== 인증서 내보내기 =====
curl -s "$BASE/api/certificates/export/all?format=pem" \
  -H "X-API-Key: $API_KEY" -o all_certs.zip

# ===== AI 분석 =====
curl -s "$BASE/api/ai/certificate/a1b2c3d4..." | jq .
curl -s "$BASE/api/ai/statistics" | jq .
curl -s "$BASE/api/ai/anomalies?label=ANOMALOUS&page=1&size=10" | jq .
curl -s "$BASE/api/ai/reports/country-maturity" | jq .
curl -s "$BASE/api/ai/reports/country/KR" | jq .

# ===== 동기화 =====
curl -s "$BASE/api/sync/status" | jq .

# ===== CRL 보고서 =====
curl -s "$BASE/api/certificates/crl/report" | jq .

# ===== ICAO 버전 =====
curl -s "$BASE/api/icao/status" | jq .
```

### 15.5 HTTPS Private CA 설정

ICAO Local PKD는 Private CA 기반 TLS를 사용합니다. 관리자에게 `ca.crt` 파일을 받아 아래와 같이 설정하세요.

**curl**:
```bash
curl --cacert /path/to/ca.crt -H "X-API-Key: $API_KEY" $URL
```

**Python (requests)**:
```python
requests.get(url, headers=headers, verify="/path/to/ca.crt")
```

**Java (keytool)**:
```bash
keytool -import -alias pkd-ca -file ca.crt -keystore cacerts -storepass changeit
```

**C# (.NET)**:
```csharp
var handler = new HttpClientHandler();
handler.ServerCertificateCustomValidationCallback = (msg, cert, chain, errors) => {
    // CA 인증서 검증 로직 또는 개발 환경에서 true 반환
    return true;
};
var client = new HttpClient(handler);
```

### 15.6 에러 처리 패턴

```python
import time

def call_with_retry(client, func, *args, max_retries=3):
    """Rate Limit + 네트워크 에러 처리"""
    for attempt in range(max_retries):
        try:
            return func(*args)
        except requests.exceptions.HTTPError as e:
            if e.response.status_code == 429:
                retry_after = int(e.response.headers.get("Retry-After", 60))
                print(f"Rate limit. {retry_after}초 대기... ({attempt+1}/{max_retries})")
                time.sleep(retry_after)
            elif e.response.status_code >= 500:
                print(f"서버 오류. 5초 대기... ({attempt+1}/{max_retries})")
                time.sleep(5)
            else:
                raise
        except requests.exceptions.ConnectionError:
            print(f"연결 실패. 10초 대기... ({attempt+1}/{max_retries})")
            time.sleep(10)
    raise Exception("최대 재시도 횟수 초과")
```

---

## 16. 에러 코드 참조

### HTTP 에러 응답

| 코드 | 에러 | 원인 | 대응 |
|------|------|------|------|
| `401` | Unauthorized | API Key가 없거나 유효하지 않음 | API Key 확인 (PA 엔드포인트에서는 미발생) |
| `403` | Forbidden | 비활성 클라이언트, IP 차단, 권한 부족 | 관리자에게 상태 확인 요청 |
| `429` | Rate Limit Exceeded | 분/시/일 Rate Limit 초과 | `Retry-After` 대기 후 재시도 |

### PA 에러 코드

| 코드 | 심각도 | 설명 |
|------|--------|------|
| `MISSING_SOD` | CRITICAL | SOD 데이터 누락 |
| `INVALID_SOD` | CRITICAL | SOD 파싱 실패 (CMS 구조 오류) |
| `DSC_EXTRACTION_FAILED` | CRITICAL | SOD에서 DSC 추출 실패 |
| `CSCA_NOT_FOUND` | CRITICAL | CSCA 인증서를 찾을 수 없음 |
| `CSCA_DN_MISMATCH` | CRITICAL | CSCA Subject DN 불일치 |
| `CSCA_SELF_SIGNATURE_FAILED` | CRITICAL | CSCA 자가 서명 검증 실패 |
| `TRUST_CHAIN_INVALID` | HIGH | 신뢰 체인 검증 실패 |
| `SOD_SIGNATURE_INVALID` | HIGH | SOD 서명 검증 실패 |
| `DG_HASH_MISMATCH` | HIGH | Data Group 해시 불일치 |
| `CERTIFICATE_REVOKED` | HIGH | CRL에 의해 인증서 폐지됨 |
| `CERTIFICATE_EXPIRED` | MEDIUM | 인증서 유효기간 만료 |
| `CRL_EXPIRED` | MEDIUM | CRL 유효기간 만료 |

### 검증 상태 코드

| 상태 | 설명 |
|------|------|
| `VALID` | 인증서 유효, Trust Chain 검증 성공 |
| `EXPIRED_VALID` | 인증서 만료되었으나 서명 당시 유효 (Point-in-Time) |
| `INVALID` | 검증 실패 (서명, Trust Chain 등) |
| `PENDING` | CSCA 미등록으로 검증 보류 |
| `ERROR` | 검증 중 오류 발생 |

### CRL 상태 코드

| 상태 | 설명 |
|------|------|
| `NOT_REVOKED` | 폐기되지 않음 |
| `REVOKED` | 인증서 폐기됨 |
| `CRL_EXPIRED` | CRL 유효기간 만료 (nextUpdate 경과) |
| `UNKNOWN` | CRL 미확인 |

---

## 17. FAQ / 트러블슈팅

### Q. API Key를 분실했습니다.

API Key는 발급 시 한 번만 표시됩니다. 관리자에게 **키 재발급**을 요청하세요. 기존 키는 즉시 무효화됩니다.

### Q. 403 Forbidden 오류가 발생합니다.

가능한 원인:
1. 클라이언트 비활성화 (관리자가 비활성화)
2. API Key 만료 (`expires_at` 설정된 경우)
3. IP 화이트리스트 불일치 (현재 IP가 `allowed_ips`에 없음)
4. Permission 부족 (요청 엔드포인트에 대한 권한 없음)
5. 엔드포인트 제한 (`allowed_endpoints` 패턴에 불일치)

### Q. 429 Too Many Requests가 자주 발생합니다.

요청 빈도를 줄이는 방법:
- `limit` 파라미터를 늘려 페이지 수를 줄이기
- PA 간편 조회(`/api/certificates/pa-lookup`)로 전체 검증 호출 줄이기
- 응답 결과를 로컬 캐싱
- 관리자에게 Rate Limit 상향 요청

### Q. HTTPS 연결 시 인증서 오류가 발생합니다.

Private CA 기반 TLS를 사용하므로 `ca.crt` 설치가 필요합니다. [15.5 HTTPS Private CA 설정](#155-https-private-ca-설정)을 참조하세요.

### Q. PA 검증이 INVALID로 반환됩니다.

주요 원인:
1. **CSCA_NOT_FOUND**: 해당 국가 CSCA가 Local PKD에 미등록 → 관리자에게 Master List 업로드 요청
2. **TRUST_CHAIN_INVALID**: DSC → CSCA 체인 실패 → CSCA 유효성, Link Certificate 확인
3. **DG_HASH_MISMATCH**: Data Group 무결성 오류 → NFC 통신 재시도

### Q. 미등록 API Key로 PA 요청을 보내면?

PA 엔드포인트(`/api/pa/*`)에서는 미등록/유효하지 않은 API Key를 전송해도 **정상 처리**됩니다 (v2.22.1+). 등록된 유효한 키만 사용량 추적이 적용됩니다.

> PKD Management 엔드포인트(`/api/certificates/*` 등)에서는 미등록 API Key가 `401`을 반환합니다.

### Q. certType 필터가 작동하지 않습니다.

LDAP 계층 구조 특성상, `certType`만 지정하고 `country`를 지정하지 않으면 유형 필터링이 효과적이지 않습니다. 반드시 `country`와 함께 사용하세요.

### Q. Base64 인코딩 오류가 발생합니다.

Standard Base64 인코딩을 사용하세요 (RFC 4648). URL-safe Base64가 아닌 표준 Base64입니다.

---

## 18. 부록

### A. 전체 엔드포인트 Quick Reference

| Method | Path | Auth | Permission | 설명 |
|--------|------|------|-----------|------|
| **인증서 검색** |||||
| `GET` | `/api/certificates/search` | Public | `cert:read` | 인증서 검색 |
| `GET` | `/api/certificates/countries` | Public | `cert:read` | 국가 목록 |
| `GET` | `/api/certificates/validation` | Public | `cert:read` | 검증 결과 조회 |
| `POST` | `/api/certificates/pa-lookup` | Public | `cert:read` | PA 간편 조회 |
| `GET` | `/api/certificates/doc9303-checklist` | Public | `report:read` | Doc 9303 체크리스트 |
| `GET` | `/api/certificates/export/file` | Public | `cert:export` | 단일 인증서 내보내기 |
| `GET` | `/api/certificates/export/country` | Public | `cert:export` | 국가별 내보내기 |
| `GET` | `/api/certificates/export/all` | Public | `cert:export` | 전체 내보내기 |
| **CRL 보고서** |||||
| `GET` | `/api/certificates/crl/report` | Public | `report:read` | CRL 보고서 |
| `GET` | `/api/certificates/crl/{id}` | Public | `report:read` | CRL 상세 |
| `GET` | `/api/certificates/crl/{id}/download` | Public | `report:read` | CRL 다운로드 |
| **DSC_NC 보고서** |||||
| `GET` | `/api/certificates/dsc-nc/report` | Public | `report:read` | 비준수 DSC 보고서 |
| **PA 검증** |||||
| `POST` | `/api/pa/verify` | Public | `pa:verify` | PA 전체 검증 |
| `POST` | `/api/pa/parse-sod` | Public | `pa:verify` | SOD 파싱 |
| `POST` | `/api/pa/parse-dg1` | Public | `pa:verify` | DG1 파싱 |
| `POST` | `/api/pa/parse-dg2` | Public | `pa:verify` | DG2 파싱 |
| `POST` | `/api/pa/parse-mrz-text` | Public | `pa:verify` | MRZ 파싱 |
| `GET` | `/api/pa/history` | Public | `pa:read` | 검증 이력 |
| `GET` | `/api/pa/{id}` | Public | `pa:read` | 검증 상세 |
| `GET` | `/api/pa/{id}/datagroups` | Public | `pa:read` | DG 상세 |
| `GET` | `/api/pa/statistics` | Public | `pa:read` | 검증 통계 |
| **AI 분석** |||||
| `GET` | `/api/ai/health` | Public | `ai:read` | AI 서비스 상태 |
| `GET` | `/api/ai/certificate/{fp}` | Public | `ai:read` | 인증서 분석 결과 |
| `GET` | `/api/ai/certificate/{fp}/forensic` | Public | `ai:read` | 포렌식 상세 |
| `GET` | `/api/ai/anomalies` | Public | `ai:read` | 이상 인증서 목록 |
| `GET` | `/api/ai/statistics` | Public | `ai:read` | 분석 통계 |
| `POST` | `/api/ai/analyze` | Public | `ai:read` | 전체 분석 실행 |
| `POST` | `/api/ai/analyze/incremental` | Public | `ai:read` | 증분 분석 실행 |
| `GET` | `/api/ai/analyze/status` | Public | `ai:read` | 분석 진행 상태 |
| `GET` | `/api/ai/reports/country-maturity` | Public | `ai:read` | 국가 PKI 성숙도 |
| `GET` | `/api/ai/reports/algorithm-trends` | Public | `ai:read` | 알고리즘 추이 |
| `GET` | `/api/ai/reports/key-size-distribution` | Public | `ai:read` | 키 크기 분포 |
| `GET` | `/api/ai/reports/risk-distribution` | Public | `ai:read` | 위험 수준 분포 |
| `GET` | `/api/ai/reports/country/{code}` | Public | `ai:read` | 국가 상세 |
| `GET` | `/api/ai/reports/issuer-profiles` | Public | `ai:read` | 발급자 프로파일 |
| `GET` | `/api/ai/reports/forensic-summary` | Public | `ai:read` | 포렌식 요약 |
| `GET` | `/api/ai/reports/extension-anomalies` | Public | `ai:read` | 확장 위반 목록 |
| **동기화 모니터링** |||||
| `GET` | `/api/sync/status` | Public | `sync:read` | 동기화 상태 |
| `GET` | `/api/sync/stats` | Public | `sync:read` | 동기화 통계 |
| `POST` | `/api/sync/check` | Public | `sync:read` | 수동 체크 |
| `GET` | `/api/sync/discrepancies` | Public | `sync:read` | 불일치 목록 |
| `POST` | `/api/sync/reconcile` | Public | `sync:read` | 보정 실행 |
| `GET` | `/api/sync/reconcile/history` | Public | `sync:read` | 보정 이력 |
| `GET` | `/api/sync/reconcile/{id}` | Public | `sync:read` | 보정 상세 |
| `GET` | `/api/sync/reconcile/stats` | Public | `sync:read` | 보정 통계 |
| **ICAO 모니터링** |||||
| `GET` | `/api/icao/status` | Public | `icao:read` | 버전 비교 |
| `GET` | `/api/icao/latest` | Public | `icao:read` | 최신 버전 |
| `GET` | `/api/icao/history` | Public | `icao:read` | 체크 이력 |
| `GET` | `/api/icao/check-updates` | Public | `icao:read` | 수동 체크 |
| **업로드 통계** |||||
| `GET` | `/api/upload/statistics` | Public | `upload:read` | 업로드 통계 |
| `GET` | `/api/upload/history` | Public | `upload:read` | 업로드 이력 |
| `GET` | `/api/upload/detail/{id}` | Public | `upload:read` | 업로드 상세 |
| `GET` | `/api/upload/countries` | Public | `upload:read` | 국가별 통계 |
| **코드 마스터** |||||
| `GET` | `/api/code-master` | Public | - | 코드 목록 |
| `GET` | `/api/code-master/categories` | Public | - | 카테고리 목록 |
| `GET` | `/api/code-master/{id}` | Public | - | 코드 상세 |
| **헬스 체크** |||||
| `GET` | `/api/health` | Public | - | PKD Management 상태 |
| `GET` | `/api/health/database` | Public | - | DB 연결 상태 |
| `GET` | `/api/health/ldap` | Public | - | LDAP 연결 상태 |
| `GET` | `/api/sync/health` | Public | - | PKD Relay 상태 |
| `GET` | `/api/ai/health` | Public | - | AI Service 상태 |

### B. OpenAPI 스펙 참조

- **Swagger UI**: `https://pkd.smartcoreinc.com/api-docs`
- OpenAPI YAML:
  - PKD Management: `docs/openapi/pkd-management.yaml`
  - PA Service: `docs/openapi/pa-service.yaml`
  - PKD Relay: `docs/openapi/pkd-relay-service.yaml`

### C. 변경 이력

| 버전 | 날짜 | 변경 내용 |
|------|------|----------|
| 2.25.5 | 2026-03-01 | 초기 작성 — 전체 공개 API 종합 개발자 메뉴얼 |

---

**기술 지원**: support@smartcoreinc.com
**Copyright 2026 SmartCore Inc. All rights reserved.**
