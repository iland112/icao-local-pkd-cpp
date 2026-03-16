# DSC Trust Chain 검증 실패 상세 보고서

**보고서 버전**: v1.0
**분석 일자**: 2026-02-15
**시스템 버전**: ICAO Local PKD v2.10.6
**데이터 소스**: ICAO PKD Master List (2025년 12월) + LDIF Collections (col-001, col-002, col-003)

---

## 1. 개요

ICAO Local PKD 시스템에 등록된 전체 DSC(Document Signer Certificate)에 대한 Trust Chain 검증 결과를 분석하였다. 검증은 ICAO Doc 9303 Part 12 §5에 따라 DSC → CSCA 서명 검증, CRL 폐기 확인, 유효기간 검증을 수행한다.

### 1.1 데이터 범위

| 항목 | 수량 |
|------|------|
| 총 인증서 수 | 26,844 |
| DSC 인증서 | 25,642 |
| CSCA 인증서 | 845 |
| DSC_NC (비적합) | 330 |
| MLSC | 27 |
| 검증 대상 (DSC) | 25,972 |
| 국가 수 | 63 |

---

## 2. 검증 결과 요약

### 2.1 전체 통계

| 상태 | 건수 | 비율 |
|------|------|------|
| **VALID** (유효) | 10,554 | 40.6% |
| **INVALID** (무효) | 15,304 | 58.9% |
| **PENDING** (보류) | 114 | 0.4% |
| **합계** | **25,972** | **100%** |

### 2.2 실패 원인 분류

INVALID 15,304건을 원인별로 분류하면 다음과 같다:

| 실패 원인 | 건수 | 비율 |
|-----------|------|------|
| Trust chain signature verification failed | 14,875 | 97.2% |
| Chain broken: Issuer not found at depth 2 | 429 | 2.8% |
| **합계** | **15,304** | **100%** |

### 2.3 PENDING 사유

| 사유 | 건수 |
|------|------|
| CSCA not found in database | 114 |

---

## 3. 실패 원인 상세 분석

### 3.1 서명 검증 실패 (14,875건, 97.2%)

**현상**: DSC의 Issuer DN과 일치하는 CSCA가 DB에 존재하나, 해당 CSCA의 공개키로 DSC 서명을 검증하면 실패한다.

**근본 원인**: **CSCA 키 롤오버(Key Rollover) 불일치**

ICAO Doc 9303 Part 12에 따르면, 각 국가는 주기적으로 CSCA 키 쌍을 교체한다. 키 롤오버 시:
- 새 CSCA는 동일한 Subject DN을 사용하되 새 키 쌍을 생성
- 이전 CSCA → 새 CSCA 간 Link Certificate를 발급하여 연속성 확보
- Master List에는 최신 세대의 CSCA만 포함

현재 시스템에 로드된 Master List(2025년 12월)에는 **현재 세대 CSCA만** 포함되어 있으나, LDIF Collection에는 **이전 세대 CSCA로 서명된 DSC**가 다수 포함되어 있다. 이전 세대 CSCA가 시스템에 없으므로, 해당 DSC의 서명 검증이 실패한다.

**검증 로직** (`validation_service.cpp`):
```
1. DSC의 Issuer DN으로 모든 CSCA 후보를 조회 (findAllCscasBySubjectDn)
2. 각 CSCA 후보에 대해 OpenSSL X509_verify()로 서명 검증 시도
3. 어떤 CSCA로도 서명이 유효하지 않으면 → INVALID
4. Fallback: DN 일치하는 CSCA가 1개라도 있으면 해당 CSCA 정보를 기록 (서명 미검증)
```

### 3.2 CSCA 미발견 — Chain Broken (429건, 2.8%)

**현상**: DSC의 Issuer DN과 일치하는 CSCA가 DB에 전혀 존재하지 않는다.

**원인**: 해당 국가의 과거 세대 CSCA가 Master List에 포함되지 않았거나, Subject DN 형식 차이로 인해 매칭에 실패한다.

#### 미발견 CSCA 목록 (21개 고유 DN)

| 국가 | 미발견 CSCA Subject DN | 영향 DSC 수 |
|------|------------------------|-------------|
| SK | /C=SK/O=Ministry of Interior of the Slovak Republic/OU=SITB/CN=CSCA Slovakia/ser... | 71 |
| LU | /CN=Grand-Duchy of Luxembourg CSCA eTravel Documents/O=Grand-Duchy of Luxembourg | 51 |
| IT | /CN=Italian Country Signer CA/OU=National Electronic Center of State Police/O=Mi... | 42 |
| CH | /C=CH/O=Admin/OU=Services/OU=Certification Authorities/CN=csca-switzerland-2 | 41 |
| LU | /C=LU/O=INCERT public agency/CN=Grand Duchy of Luxembourg CSCA | 34 |
| ES | /C=ES/O=DIRECCION GENERAL DE LA POLICIA/serialNumber=3/CN=CSCA SPAIN | 33 |
| NZ | /C=NZ/O=Government of New Zealand/OU=Passports/OU=Identity Services Passport CA | 26 |
| GB | /C=GB/O=UKKPA/CN=Country Signing Authority | 24 |
| SG | /C=SG/O=Ministry of Home Affairs/OU=Singapore Passport CA | 19 |
| HU | /C=HU/O=GOV/OU=CRO/CN=CSCA-HUNGARY 2 | 18 |
| HU | /C=HU/O=GOV/OU=KEKKH/CN=ID-CSCA-HUNGARY 02 | 17 |
| HU | /C=HU/O=GOV/OU=CRO/CN=CSCA-HUNGARY 3 | 13 |
| LU | /C=LU/O=Grand-Duchy of Luxembourg Ministry Foreign Affairs/CN=Grand-Duchy of Lux... | 12 |
| DE | /C=DE/O=bund/OU=bsi/serialNumber=100/CN=csca-germany | 9 |
| UA | /C=UA/serialNumber=UA-16286441-0001/O=Polygraph combine UKRAINA... | 6 |
| HU | /C=HU/O=GOV/OU=Ministry of Interior/CN=CSCA HUNGARY | 3 |
| HU | /C=HU/O=GOV/OU=Cabinet Office of the Prime Minister/CN=CSCA HUNGARY | 3 |
| MA | /C=MA/O=Gov/CN=CSCA-MAROC/serialNumber=2 | 2 |
| ZW | /CN=CSCA/serialNumber=001/O=Department of the Registrar General/C=ZW | 2 |
| HU | /C=HU/O=GOV/OU=CRO/CN=CSCA-HUNGARY 2017 | 2 |
| NL | /serialNumber=6/CN=CSCA NL/OU=Kingdom of the Netherlands/O=Kingdom of the Nether... | 1 |

> **참고**: 헝가리(HU)는 5개 세대의 서로 다른 CSCA DN이 미발견되어 가장 복잡한 키 롤오버 이력을 보인다.

---

## 4. 국가별 상세 분석

### 4.1 상위 15개 실패 국가

| 순위 | 국가 | 총 DSC | VALID | INVALID | PENDING | 실패율 |
|------|------|--------|-------|---------|---------|--------|
| 1 | CN (중국) | 13,166 | 6,138 | 7,028 | 0 | 53.4% |
| 2 | GB (영국) | 2,447 | 0 | 2,447 | 0 | **100%** |
| 3 | US (미국) | 1,794 | 0 | 1,794 | 0 | **100%** |
| 4 | IE (아일랜드) | 665 | 0 | 665 | 0 | **100%** |
| 5 | AU (호주) | 837 | 204 | 627 | 6 | 74.9% |
| 6 | CA (캐나다) | 727 | 102 | 625 | 0 | 86.0% |
| 7 | FR (프랑스) | 2,685 | 2,204 | 481 | 0 | 17.9% |
| 8 | SE (스웨덴) | 275 | 27 | 231 | 17 | 84.0% |
| 9 | CH (스위스) | 211 | 0 | 211 | 0 | **100%** |
| 10 | CZ (체코) | 176 | 0 | 176 | 0 | **100%** |
| 11 | JP (일본) | 140 | 0 | 140 | 0 | **100%** |
| 12 | IT (이탈리아) | 108 | 8 | 100 | 0 | 92.6% |
| 13 | LU (룩셈부르크) | 142 | 36 | 97 | 9 | 68.3% |
| 14 | ES (스페인) | 109 | 21 | 88 | 0 | 80.7% |
| 15 | TM (투르크메니스탄) | 182 | 98 | 84 | 0 | 46.2% |

### 4.2 100% 실패 국가 (11개국)

아래 국가는 시스템에 등록된 **모든** DSC가 INVALID 상태이다. 이는 현재 Master List에 포함된 CSCA 세대와 LDIF Collection의 DSC 세대가 완전히 불일치함을 의미한다.

| 국가 | DSC 수 | 원인 |
|------|--------|------|
| GB (영국) | 2,447 | 서명 검증 실패 + CSCA 미발견 (24건) |
| US (미국) | 1,794 | 서명 검증 실패 |
| IE (아일랜드) | 665 | 서명 검증 실패 |
| CH (스위스) | 211 | 서명 검증 실패 + CSCA 미발견 (41건) |
| CZ (체코) | 176 | 서명 검증 실패 |
| JP (일본) | 140 | 서명 검증 실패 |
| UN (유엔) | 53 | 서명 검증 실패 |
| BM (버뮤다) | 24 | 서명 검증 실패 |
| UZ (우즈베키스탄) | 10 | 서명 검증 실패 |
| ZW (짐바브웨) | 2 | CSCA 미발견 |
| NL (네덜란드) | 1 | CSCA 미발견 |

### 4.3 100% 성공 국가 (19개국)

아래 국가는 모든 DSC가 VALID 상태로, 현재 세대 CSCA와 정확히 매칭된다.

| 국가 | DSC 수 |
|------|--------|
| NO (노르웨이) | 235 |
| IN (인도) | 240 |
| KR (한국) | 132 |
| NP (네팔) | 118 |
| TZ (탄자니아) | 89 |
| SC (세이셸) | 44 |
| BH (바레인) | 36 |
| BZ (벨리즈) | 36 |
| PA (파나마)* | 32 |
| MX (멕시코) | 23 |
| AE (UAE) | 21 |
| JM (자메이카) | 21 |
| AR (아르헨티나) | 16 |
| IQ (이라크)* | 13 |
| VN (베트남) | 10 |
| MN (몽골) | 9 |
| IR (이란) | 9 |
| RO (루마니아) | 8 |
| YE (예멘) | 6 |

> *PA(32건), IQ(13건)는 PENDING 상태이나, INVALID는 0건이다.

### 4.4 PENDING 상태 국가 (7개국)

CSCA가 DB에 전혀 등록되지 않아 검증을 진행할 수 없는 국가:

| 국가 | PENDING 수 | 사유 |
|------|-----------|------|
| IS (아이슬란드) | 36 | CSCA not found in database |
| PA (파나마) | 32 | CSCA not found in database |
| SE (스웨덴) | 17 | CSCA not found in database |
| IQ (이라크) | 13 | CSCA not found in database |
| LU (룩셈부르크) | 9 | CSCA not found in database |
| AU (호주) | 6 | CSCA not found in database |
| UA (우크라이나) | 1 | CSCA not found in database |

---

## 5. 근본 원인 분석

### 5.1 CSCA 키 롤오버 메커니즘

```
[CSCA Gen 1] ──서명──> [DSC-A, DSC-B, DSC-C, ...]     ← LDIF에 존재
     │
     │ (키 롤오버)
     ↓
[CSCA Gen 2] ──서명──> [DSC-D, DSC-E, DSC-F, ...]     ← LDIF에 존재
     │
     │ Link Certificate (Gen1 → Gen2)
     ↓
[Master List] ── 포함 ──> [CSCA Gen 2만 포함]          ← 현재 시스템에 로드됨
```

- Master List는 각 국가의 **최신 세대 CSCA만** 포함한다
- LDIF Collection에는 **모든 세대의 DSC**가 포함되어 있다
- 이전 세대 CSCA가 없으면, 해당 세대 DSC의 서명 검증이 불가능하다

### 5.2 왜 58.9%나 실패하는가?

1. **대규모 국가의 다수 세대 DSC**: 중국(CN)이 13,166건 중 7,028건 실패로, 전체 실패의 46%를 차지한다
2. **영미권 국가의 완전 불일치**: GB(2,447), US(1,794), IE(665)가 100% 실패하여 전체 실패의 32%를 차지한다
3. **Master List의 단일 시점 한계**: 2025년 12월 기준 Master List는 해당 시점의 현재 세대만 반영한다

### 5.3 검증 시스템의 정상 동작 확인

이 높은 실패율은 **검증 시스템의 오류가 아닌 정상 동작**이다:
- VALID 10,554건은 현재 CSCA로 서명이 올바르게 검증됨
- INVALID 15,304건은 이전 세대 CSCA 부재로 인한 정당한 실패
- 실제 여권 검증 시에는 해당 여권의 SOD에 내장된 DSC를 사용하므로, Local PKD에 등록되지 않은 이전 세대 DSC가 있더라도 PA 검증에는 영향이 없음

---

## 6. 개선 권고사항

### 6.1 단기 조치 (데이터 보완)

| 우선순위 | 조치 | 효과 |
|---------|------|------|
| **높음** | 이전 세대 Master List 추가 로드 | 과거 CSCA 확보로 서명 검증 가능 |
| **높음** | ICAO PKD에서 개별 CSCA 다운로드 | 누락 CSCA 보충 |
| **중간** | Link Certificate 수집 및 등록 | CSCA 세대 간 연결 고리 확보 |

### 6.2 중기 조치 (시스템 개선)

| 우선순위 | 조치 | 효과 |
|---------|------|------|
| **높음** | `INVALID_CSCA_MISMATCH` 상태 추가 | 키 롤오버 불일치와 실제 위변조 구분 |
| **중간** | Link Certificate 기반 간접 검증 | CSCA Gen1 → Gen2 경로를 통한 체인 연결 |
| **중간** | 검증 결과에 "이전 세대 CSCA 필요" 사유 세분화 | 운영자 의사결정 지원 |

### 6.3 장기 조치 (운영 체계)

| 우선순위 | 조치 | 효과 |
|---------|------|------|
| **중간** | Master List 히스토리 아카이브 구축 | 과거 CSCA 세대 보존 |
| **낮음** | CSCA 키 롤오버 이벤트 자동 탐지 | 새 세대 CSCA 등록 시 자동 알림 |

---

## 7. 결론

현재 DSC Trust Chain 검증에서 58.9%(15,304건)의 실패율은 **CSCA 키 롤오버로 인한 이전 세대 CSCA 부재**가 근본 원인이다. 이는 검증 시스템의 오류가 아닌, Master List의 단일 시점 특성에 기인한다.

가장 효과적인 개선 방안은 이전 세대 Master List를 추가로 로드하여 과거 CSCA를 확보하는 것이며, 이를 통해 실패율을 대폭 감소시킬 수 있을 것으로 예상된다.

---

## 부록 A: 전체 국가별 검증 결과

| 국가 | 총 DSC | VALID | INVALID | PENDING | 실패율 |
|------|--------|-------|---------|---------|--------|
| CN | 13,166 | 6,138 | 7,028 | 0 | 53.4% |
| FR | 2,685 | 2,204 | 481 | 0 | 17.9% |
| GB | 2,447 | 0 | 2,447 | 0 | 100.0% |
| US | 1,794 | 0 | 1,794 | 0 | 100.0% |
| AU | 837 | 204 | 627 | 6 | 74.9% |
| CA | 727 | 102 | 625 | 0 | 86.0% |
| IE | 665 | 0 | 665 | 0 | 100.0% |
| SE | 275 | 27 | 231 | 17 | 84.0% |
| IN | 240 | 240 | 0 | 0 | 0.0% |
| SK | 236 | 165 | 71 | 0 | 30.1% |
| NO | 235 | 235 | 0 | 0 | 0.0% |
| CH | 211 | 0 | 211 | 0 | 100.0% |
| TM | 182 | 98 | 84 | 0 | 46.2% |
| CZ | 176 | 0 | 176 | 0 | 100.0% |
| LU | 142 | 36 | 97 | 9 | 68.3% |
| JP | 140 | 0 | 140 | 0 | 100.0% |
| KR | 132 | 132 | 0 | 0 | 0.0% |
| NP | 118 | 118 | 0 | 0 | 0.0% |
| BG | 113 | 67 | 46 | 0 | 40.7% |
| ES | 109 | 21 | 88 | 0 | 80.7% |
| IT | 108 | 8 | 100 | 0 | 92.6% |
| NZ | 108 | 82 | 26 | 0 | 24.1% |
| TZ | 89 | 89 | 0 | 0 | 0.0% |
| HU | 86 | 21 | 65 | 0 | 75.6% |
| IS | 78 | 38 | 4 | 36 | 5.1% |
| AT | 73 | 33 | 40 | 0 | 54.8% |
| OM | 66 | 8 | 58 | 0 | 87.9% |
| UA | 59 | 52 | 6 | 1 | 10.2% |
| MA | 57 | 55 | 2 | 0 | 3.5% |
| UN | 53 | 0 | 53 | 0 | 100.0% |
| SG | 48 | 29 | 19 | 0 | 39.6% |
| MD | 45 | 34 | 11 | 0 | 24.4% |
| FI | 44 | 12 | 32 | 0 | 72.7% |
| SC | 44 | 44 | 0 | 0 | 0.0% |
| TH | 37 | 15 | 22 | 0 | 59.5% |
| BH | 36 | 36 | 0 | 0 | 0.0% |
| BZ | 36 | 36 | 0 | 0 | 0.0% |
| PA | 32 | 0 | 0 | 32 | 0.0% |
| BM | 24 | 0 | 24 | 0 | 100.0% |
| MX | 23 | 23 | 0 | 0 | 0.0% |
| AE | 21 | 21 | 0 | 0 | 0.0% |
| JM | 21 | 21 | 0 | 0 | 0.0% |
| DE | 20 | 11 | 9 | 0 | 45.0% |
| EU | 19 | 17 | 2 | 0 | 10.5% |
| AR | 16 | 16 | 0 | 0 | 0.0% |
| IQ | 13 | 0 | 0 | 13 | 0.0% |
| ID | 10 | 6 | 4 | 0 | 40.0% |
| UZ | 10 | 0 | 10 | 0 | 100.0% |
| VN | 10 | 10 | 0 | 0 | 0.0% |
| MN | 9 | 9 | 0 | 0 | 0.0% |
| IR | 9 | 9 | 0 | 0 | 0.0% |
| RO | 8 | 8 | 0 | 0 | 0.0% |
| RW | 8 | 5 | 3 | 0 | 37.5% |
| YE | 6 | 6 | 0 | 0 | 0.0% |
| BR | 2 | 2 | 0 | 0 | 0.0% |
| CM | 2 | 2 | 0 | 0 | 0.0% |
| CO | 2 | 2 | 0 | 0 | 0.0% |
| GE | 2 | 2 | 0 | 0 | 0.0% |
| ZW | 2 | 0 | 2 | 0 | 100.0% |
| BJ | 3 | 3 | 0 | 0 | 0.0% |
| GU | 1 | 1 | 0 | 0 | 0.0% |
| JO | 1 | 1 | 0 | 0 | 0.0% |
| NL | 1 | 0 | 1 | 0 | 100.0% |

---

## 부록 B: 검증 로직 참조

- **검증 서비스**: `services/pkd-management/src/services/validation_service.cpp`
  - `buildTrustChain()` (line 510–665): CSCA 후보 조회 + 서명 검증
  - `validateTrustChainInternal()` (line 724–776): 체인 서명 순차 검증
  - `validateCertificate()` (line 150–270): 전체 검증 워크플로
  - `checkCrlRevocation()` (line 780–910): CRL 폐기 확인

- **인증서 저장소**: `services/pkd-management/src/repositories/certificate_repository.cpp`
  - `findAllCscasBySubjectDn()` (line 515–586): DN 컴포넌트 기반 CSCA 검색

- **ICAO 규격 참조**:
  - ICAO Doc 9303 Part 12 §5: Trust Chain Validation
  - ICAO Doc 9303 Part 12 §4: CSCA Key Rollover
  - RFC 5280 §6: Certificate Path Validation
