# ICAO PKD 데이터 구조 분석 — Master List vs Collection-002

## 1. 개요

ICAO PKD(Public Key Directory)의 Collection 구조와 Master List 파일의 관계를 실측 데이터 기반으로 분석한 문서이다. Production DB에 적재된 인증서의 fingerprint(SHA-256)를 기준으로 두 소스 간 중복/고유 인증서를 정량적으로 비교하고, 둘 다 업로드해야 하는 이유와 권장 순서를 제시한다.

---

## 2. ICAO PKD Collection 구조

ICAO PKD Download Service는 LDAP 기반으로 3개 Collection을 배포한다.

| Collection | 파일 패턴 | 내용 | 규모 |
|------------|-----------|------|------|
| **Collection 001** | `icaopkd-001-*.ldif` | DSC + CRL | DSC ~30,008 + CRL 69 |
| **Collection 002** | `icaopkd-002-*.ldif` | 국가별 Master List + CSCA + MLSC | 27개국 ML, CSCA 314(고유) + MLSC 26 |
| **Collection 003** | `icaopkd-003-*.ldif` | DSC_NC 비적합 인증서 | DSC_NC 502 |

**별도 배포**: ICAO Master List (`.ml` 파일) — ICAO 포털에서 직접 다운로드하며, 전 세계 CSCA를 하나의 CMS SignedData 파일로 통합 배포한다.

---

## 3. ICAO Master List (.ml) vs Collection-002 (.ldif) 비교

### 3.1 실측 데이터 (Production DB, 2026-03-03)

분석 대상 파일:

- **ML**: `ICAO_ml_December2025.ml` (810KB, CSCA 536 + MLSC 1 = 537건)
- **Collection-002**: `icaopkd-002-complete-000338.ldif` (10.6MB, CSCA 314(고유) + MLSC 26 = 340건)

| 항목 | ICAO ML | Collection-002 |
|------|---------|----------------|
| 파일 크기 | 810 KB | 10.6 MB |
| 서명자 | ICAO (UN) | 각 국가 |
| 포함 ML 수 | 1 (통합) | 27 (국가별) |
| 저장된 CSCA | 536 | 314 (고유) |
| 저장된 MLSC | 1 | 26 |
| 국가 수 | 95 | 91 |

### 3.2 인증서 중복 분석 (Fingerprint SHA-256 기준)

핵심 발견:

| 비교 항목 | 건수 |
|-----------|------|
| ML → Collection-002 교차 중복 | 537건 (= ML 전체) |
| ML에만 있는 인증서 | **0건** |
| Collection-002에만 있는 인증서 | **340건** |

**결론: ML은 Collection-002의 완전한 부분집합(subset)이다.**

ML의 모든 인증서(537건)가 Collection-002 처리 결과에도 포함되어 있으며, ML에만 존재하는 고유 인증서는 없다. 반면 Collection-002에는 ML에 없는 340건의 인증서가 추가로 존재한다.

```
Collection-002 (877건)
┌─────────────────────────────────────────────────────┐
│  27개국 개별 ML → CSCA 314 + MLSC 26 (고유)         │
│                                                     │
│  ┌───────────────────────────────────────────┐      │
│  │  ICAO ML 전체 포함됨                      │      │
│  │  CSCA 536 + MLSC 1 = 537건               │      │
│  └───────────────────────────────────────────┘      │
│                                                     │
└─────────────────────────────────────────────────────┘
```

> **참고**: "고유" 건수는 DB에 저장된 기준이다. Collection-002 LDIF에는 ML에서 이미 추출된 인증서와 동일한 fingerprint가 별도 LDAP 엔트리로 포함되어 있으나, 중복 감지(duplicate detection)에 의해 스킵된다.

### 3.3 국가 커버리지 분석

| 구분 | 국가 수 | 대표 국가 |
|------|---------|-----------|
| ML에만 있는 국가 | **47개** | KR, JP, AE, BR, CZ, NZ, SG, TH... |
| Collection-002에만 있는 국가 | **43개** | GR, MT, PL, DK, EE, CY, LT, IL, TW... |
| 양쪽 공통 | **48개** | DE, FR, GB, US, CN, HU, AU, IT... |

공통 국가에서도 CSCA 수가 상이하다:

| 국가 | ML | Collection-002 LDIF | 차이 | 비고 |
|------|-----|---------------------|------|------|
| CN | 34 | 3 | ML +31 | ML이 이전 세대 CSCA 다수 포함 |
| HU | 21 | 14 | ML +7 | |
| BE | 10 | 12 | LDIF +2 | LDIF에 ML 미포함 인증서 존재 |
| NG | 2 | 6 | LDIF +4 | |

ML은 ICAO가 통합 서명한 전 세계 CSCA 스냅샷이므로 이전 세대(expired 포함) CSCA를 많이 포함하는 경향이 있고, Collection-002는 각 국가가 PKD에 개별 제출한 ML에서 추출하므로 제출 시점에 따라 포함 범위가 다르다.

### 3.4 왜 같은 인증서가 Collection-002에도 포함되는가?

LDIF는 인증서 단위 변경이 아닌 **LDAP 엔트리 단위 변경**을 배포한다. 하나의 국가 업데이트(delta)에는 다음과 같은 엔트리들이 함께 포함된다:

**Delta 파일 예시** (`icaopkd-002-delta-000339.ldif`, AO 앙골라):

```
LDAP 변경 (delta-000339)
├── o=ml,c=AO     → Master List 바이너리 (엔트리 #1)
├── o=ml,c=AO     → MLSC 인증서 (엔트리 #2)
└── o=csca,c=AO   → CSCA 인증서 (엔트리 #3)
```

ML 엔트리 처리 시 이미 MLSC + CSCA를 추출/저장하므로, 동일한 CSCA가 별도 LDAP 엔트리(`o=csca`)로 다시 나타나면 중복 감지에 의해 스킵된다. 이는 LDIF 배포 구조의 특성이며, 정상 동작이다.

---

## 4. 둘 다 업로드해야 하는 이유

### ML만으로 부족한 이유

1. **43개국 CSCA가 Collection-002에만 존재** (GR, MT, PL, DK, EE, CY, LT, IL, TW 등)
2. 국가별 원본 CMS 바이너리 + 개별 MLSC 26개 미확보
3. 일부 국가에서 ML보다 Collection-002에 더 많은 CSCA 보유 (BE, NG 등)

### Collection-002만으로 부족한 이유

1. **47개국 CSCA가 ML에만 존재** (KR, JP, AE, BR, CZ, NZ, SG, TH 등)
2. ICAO 서명 Trust Chain 앵커 미확보
3. ML을 PKD에 제출한 국가가 **27개국뿐** — 나머지 국가의 CSCA는 ICAO ML에서만 확보 가능

### CSCA 기여도

```
CSCA 총 850건 = ML 536건 (63%) + Collection-002 314건 (37%)
```

어느 한쪽만 업로드하면 전체 CSCA의 37~63%를 놓치게 된다. 양쪽을 모두 업로드해야 95개국 CSCA 850건을 완전히 확보할 수 있다.

---

## 5. 권장 업로드 순서

```
1. ICAO ML (.ml)           → CSCA 536건 확보 (810KB, ~6초)
2. Collection-002 (.ldif)  → 추가 CSCA 314건 (536건 중복 스킵)
3. Collection-001 (.ldif)  → DSC 30,008 + CRL 69
4. Collection-003 (.ldif)  → DSC_NC 502
```

**ML을 먼저 업로드하면** Collection-002 처리 시 이미 확보된 CSCA 536건을 fingerprint 기반으로 빠르게 스킵할 수 있어 처리 효율이 높다. Collection-001 업로드 시에도 CSCA가 선행 확보되어 있어야 DSC Trust Chain 검증이 정상 수행된다.

---

## 6. DB 현황 요약 (전체 인증서)

| 인증서 타입 | 전체 DB | ML 기여 | LDIF-002 기여 | 기타 소스 |
|-------------|---------|---------|---------------|-----------|
| CSCA | 850 | 536 | 314 | 0 |
| DSC | 30,008 | 0 | 0 | Collection-001 |
| DSC_NC | 502 | 0 | 0 | Collection-003 |
| MLSC | 28 | 1 | 26 | delta-002 (1) |
| **합계** | **31,388** | **537** | **340** | **30,511** |

> **참고**: CSCA 850건은 CLAUDE.md의 845건과 5건 차이가 있다. 이는 최근 delta 업데이트(AO 등)로 추가된 인증서에 의한 것이다.

---

## 7. 참고: ICAO 공식 문서

| 문서 | 설명 |
|------|------|
| **ICAO Doc 9303 Part 12** | PKI for Machine Readable Travel Documents |
| **ICAO PKD Download Service** | LDAP 기반 3개 Collection 배포 |
| **Master List 형식** | CMS SignedData (Doc 9303 Annex G) |
| **CSCA** | Country Signing CA — 각 국가의 최상위 여권 PKI 인증서 |
| **Link Certificate** | CSCA 키 롤오버 시 세대 연결 인증서 |
| **MLSC** | Master List Signer Certificate — Master List 서명자 인증서 |
| **DSC** | Document Signer Certificate — 여권 칩 서명 인증서 |
| **DSC_NC** | Non-Conformant DSC — ICAO 표준 미준수 DSC (2021년 이후 deprecated) |
| **CRL** | Certificate Revocation List — 인증서 폐기 목록 |

---

**작성일**: 2026-03-03
**분석 환경**: Production Oracle XE 21c, `pkd.smartcoreinc.com` (10.0.0.220)
**분석 도구**: ICAO Local PKD v2.25.9 — fingerprint SHA-256 기반 중복 분석
