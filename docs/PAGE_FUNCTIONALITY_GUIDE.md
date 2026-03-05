# ICAO Local PKD 프론트엔드 페이지 기능 상세 가이드

**프로젝트**: ICAO Local PKD v2.28.1
**작성일**: 2026-03-05
**총 페이지 수**: 24개

---

## 목차

### PKD 관리
1. [대시보드 (Dashboard)](#1-대시보드-dashboard)
2. [파일 업로드 (FileUpload)](#2-파일-업로드-fileupload)
3. [인증서 업로드 (CertificateUpload)](#3-인증서-업로드-certificateupload)
4. [업로드 이력 (UploadHistory)](#4-업로드-이력-uploadhistory)
5. [업로드 상세 (UploadDetail)](#5-업로드-상세-uploaddetail)
6. [업로드 대시보드 (UploadDashboard)](#6-업로드-대시보드-uploaddashboard)
7. [인증서 검색 (CertificateSearch)](#7-인증서-검색-certificatesearch)

### 보고서
8. [표준 부적합 DSC 보고서 (DscNcReport)](#8-표준-부적합-dsc-보고서-dscncreport)
9. [CRL 보고서 (CrlReport)](#9-crl-보고서-crlreport)
10. [DSC Trust Chain 보고서 (TrustChainValidationReport)](#10-dsc-trust-chain-보고서-trustchainvalidationreport)

### Passive Authentication
11. [PA 검증 (PAVerify)](#11-pa-검증-paverify)
12. [PA 이력 (PAHistory)](#12-pa-이력-pahistory)
13. [PA 상세 (PADetail)](#13-pa-상세-padetail)
14. [PA 대시보드 (PADashboard)](#14-pa-대시보드-padashboard)

### 동기화 & 모니터링
15. [동기화 대시보드 (SyncDashboard)](#15-동기화-대시보드-syncdashboard)
16. [ICAO 상태 (IcaoStatus)](#16-icao-상태-icaostatus)
17. [시스템 모니터링 (MonitoringDashboard)](#17-시스템-모니터링-monitoringdashboard)
18. [AI 인증서 분석 (AiAnalysisDashboard)](#18-ai-인증서-분석-aianalysisdashboard)

### 관리자 & 보안
19. [API 클라이언트 관리 (ApiClientManagement)](#19-api-클라이언트-관리-apiclientmanagement)
20. [로그인 이력 (AuditLog)](#20-로그인-이력-auditlog)
21. [운영 감사 로그 (OperationAuditLog)](#21-운영-감사-로그-operationauditlog)
22. [사용자 관리 (UserManagement)](#22-사용자-관리-usermanagement)

### 공통
23. [로그인 (Login)](#23-로그인-login)
24. [프로필 (Profile)](#24-프로필-profile)

### 부록
- [공통 패턴 요약](#공통-패턴-요약)

---

## 1. 대시보드 (Dashboard)

| 항목 | 내용 |
|------|------|
| **라우트** | `/` |
| **파일** | `frontend/src/pages/Dashboard.tsx` |
| **인증** | 불필요 (공개) |

### 기능 설명

시스템 홈 페이지로, 전체 PKD 시스템 현황을 한 눈에 파악할 수 있다.

- **실시간 시계**: 1초 간격 갱신, 현재 날짜/시간 표시
- **연결 상태 표시기**: DB(PostgreSQL/Oracle) 및 LDAP 연결 상태를 실시간 확인
- **ICAO PKD 업데이트 알림 배너**: ICAO 공식 PKD에 새 버전이 감지되면 상단에 알림 표시 (버전 차이: `+N (vX → vY)` 형식)
- **인증서 통계 카드**: CSCA, DSC, CRL, Master List 총 수량 표시
- **국가별 인증서 분포**: 상위 10개국의 인증서 수량을 진행 바 차트로 표시
- **국가별 상세 다이얼로그**: "자세히 보기" 클릭 시 전체 국가의 인증서 유형별 분류 확인

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| GET | `/api/health/database` | DB 연결 상태 확인 |
| GET | `/api/health/ldap` | LDAP 연결 상태 확인 |
| GET | `/api/upload/countries?limit=10` | 상위 10개국 통계 |
| GET | `/api/icao/status` | ICAO PKD 버전 업데이트 확인 |

### 사용자 인터랙션

- ICAO 업데이트 배너 닫기 버튼 (localStorage에 상태 저장, 새 버전 감지 시 다시 표시)
- "자세히 보기" 버튼 → `CountryStatisticsDialog` 열기

---

## 2. 파일 업로드 (FileUpload)

| 항목 | 내용 |
|------|------|
| **라우트** | `/upload` |
| **파일** | `frontend/src/pages/FileUpload.tsx` |
| **인증** | 필요 (JWT) |

### 기능 설명

LDIF 파일 및 Master List 파일 업로드 전용 페이지. SSE(Server-Sent Events)를 통해 처리 진행 상황을 실시간으로 스트리밍한다.

- **드래그 & 드롭**: `.ldif`, `.ml` 확장자 파일을 드래그하여 업로드 영역에 놓기
- **3단계 수평 스테퍼**: PARSING → VALIDATION → DB_SAVING/LDAP_SAVING 단계별 진행 시각화
- **실시간 진행 바**: 처리된 항목 수/전체 항목 수, 백분율 표시
- **EventLog 패널**: SSE 이벤트를 시간순으로 누적 표시 (자동 스크롤, 타임스탬프, 상태 점)
- **검증 통계 카드**: VALID/EXPIRED_VALID/INVALID/PENDING 분류별 수량, 검증 사유 breakdown
- **현재 처리 인증서 카드**: 실시간으로 처리 중인 인증서의 메타데이터(국가, 유형, 주제) 표시
- **오류 요약 패널**: 파싱/DB/LDAP 오류 분류별 수량 및 상세 메시지

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| POST | `/api/upload/ldif` | LDIF 파일 업로드 |
| POST | `/api/upload/masterlist` | Master List 파일 업로드 |
| GET(SSE) | `/api/progress/stream/{uploadId}` | 실시간 진행 상황 스트림 |

### SSE 이벤트 흐름

```
connected → PARSING_STARTED(10%) → PARSING_COMPLETED(50%) → VALIDATION_STARTED(55%)
→ DB_SAVING_STARTED(72%) → DB_SAVING_COMPLETED(85%) → LDAP_SAVING_STARTED(87%)
→ LDAP_SAVING_COMPLETED(100%) → COMPLETED
```

### 특수 패턴

- **SSE 재연결**: `isProcessingRef` (useRef)로 stale closure 방지 — SSE 재연결 시 최신 처리 상태 참조
- **스테퍼 깜빡임 방지**: `useRef`로 마지막 활성 단계를 기억하여 단계 전환 중에도 상세 패널 유지
- **LDAP 연결 실패 시**: DB 전용 모드로 진행, SSE "LDAP 연결 불가 - DB 전용 모드" 알림

---

## 3. 인증서 업로드 (CertificateUpload)

| 항목 | 내용 |
|------|------|
| **라우트** | `/upload/certificate` |
| **파일** | `frontend/src/pages/CertificateUpload.tsx` |
| **인증** | 필요 (JWT) |

### 기능 설명

개별 인증서 파일 업로드 페이지. **미리보기-저장 확인(Preview-before-Save)** 워크플로우를 통해 인증서 내용을 확인한 후 저장할 수 있다.

- **지원 형식**: PEM, DER, CER, P7B, DL(Deviation List), CRL
- **미리보기 단계**: 파일 파싱만 수행 (DB/LDAP 저장 없음), 인증서 메타데이터를 트리 형태로 표시
- **탭 전환**: 인증서 목록 / DL 구조 / CRL 정보 (파일 유형에 따라 표시)
- **인증서 카드 서브 탭**: 일반(메타데이터) / 상세(TreeViewer) / Doc 9303(준수 체크리스트)
- **저장 확인**: 미리보기 확인 후 "저장" 버튼으로 DB + LDAP에 실제 저장
- **중복 감지**: SHA-256 해시 기반 중복 파일 감지 (409 Conflict → 안내 메시지)

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| GET | `/api/upload/statistics` | CSCA 등록 수 확인 (경고 배너) |
| POST | `/api/upload/certificate/preview` | 파일 파싱만 수행 |
| POST | `/api/upload/certificate` | 인증서 저장 (DB + LDAP) |

### 페이지 상태 머신

```
IDLE → FILE_SELECTED → PREVIEWING → PREVIEW_READY → CONFIRMING → COMPLETED/FAILED
```

---

## 4. 업로드 이력 (UploadHistory)

| 항목 | 내용 |
|------|------|
| **라우트** | `/upload-history` |
| **파일** | `frontend/src/pages/UploadHistory.tsx` |
| **인증** | 불필요 (공개) |

### 기능 설명

전체 업로드 이력 관리 페이지. 목록 조회, 필터링, 상세 조회, 재처리, 삭제 기능을 제공한다.

- **필터**: 상태(COMPLETED/FAILED/PROCESSING/PENDING), 파일 형식(LDIF/MASTERLIST), 파일명 검색
- **자동 갱신**: PENDING/PROCESSING 상태의 업로드가 있으면 5초 간격으로 자동 갱신
- **인라인 진행 바**: PROCESSING 상태일 때 처리 진행률 인라인 표시
- **상세 다이얼로그**: 행 클릭 시 모달로 업로드 구조, 중복 인증서, 검증 요약 확인
- **재처리**: FAILED 업로드의 이어하기 재처리 (이미 처리된 인증서 스킵, fingerprint 캐시 기반)
- **재처리 확인 다이얼로그**: 파일명, 상태, 진행률, 이어하기 모드 안내 표시
- **삭제**: FAILED/PENDING 상태 업로드 삭제 (확인 후)

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| GET | `/api/upload/history` | 업로드 이력 (페이지네이션) |
| GET | `/api/upload/detail/{id}` | 업로드 상세 정보 |
| GET | `/api/upload/{id}/issues` | 중복 인증서 감지 결과 |
| GET | `/api/upload/{id}/ldif-structure` | LDIF/ASN.1 구조 |
| POST | `/api/upload/{id}/retry` | 실패 업로드 재처리 |
| DELETE | `/api/upload/{id}` | 업로드 삭제 |

---

## 5. 업로드 상세 (UploadDetail)

| 항목 | 내용 |
|------|------|
| **라우트** | `/upload/:uploadId` |
| **파일** | `frontend/src/pages/UploadDetail.tsx` |
| **인증** | 불필요 (공개) |

### 기능 설명

특정 업로드의 전체 상세 정보 페이지.

- **파일 정보 카드**: 파일명, 크기, 업로드 시간, 상태, 처리 시간
- **처리 타임라인**: 각 단계별 시작/종료 시간
- **인증서 통계**: CSCA/DSC/CRL 카운트, 유형별 분포
- **실시간 갱신**: PENDING/PROCESSING 상태일 때 3초마다 자동 갱신
- **검증 결과**: 20건/페이지 페이지네이션, 개별 검증 상세 다이얼로그
- **Trust Chain 시각화**: 인증서 체인 경로 트리 표시 (DSC → Link → CSCA)

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| GET | `/api/upload/detail/{uploadId}` | 업로드 상세 (3초 폴링) |
| GET | `/api/upload/{uploadId}/validations` | 검증 결과 (페이지네이션) |

---

## 6. 업로드 대시보드 (UploadDashboard)

| 항목 | 내용 |
|------|------|
| **라우트** | `/upload/dashboard` |
| **파일** | `frontend/src/pages/UploadDashboard.tsx` |
| **인증** | 불필요 (공개) |

### 기능 설명

업로드 및 검증 통계 대시보드. 클릭 가능한 통계 카드와 업로드 트렌드 차트를 제공한다.

- **통계 카드**: 총 인증서, VALID, EXPIRED_VALID, INVALID, PENDING, REVOKED 수량
- **클릭 가능 카드**: 각 카드 클릭 시 상세 breakdown 다이얼로그 열기
  - INVALID → 검증 실패 사유별 분류
  - PENDING → CSCA 미등록 목록
  - EXPIRED → 국가/연도별 만료 인증서
  - REVOKED → 국가별 폐기 인증서
- **업로드 트렌드 차트**: Recharts LineChart로 날짜별 업로드 추이 시각화
- **최근 변경**: 최근 업로드 변경 이력 목록

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| GET | `/api/upload/statistics` | 업로드/검증 통계 요약 |
| GET | `/api/upload/changes` | 최근 업로드 변경 이력 |

---

## 7. 인증서 검색 (CertificateSearch)

| 항목 | 내용 |
|------|------|
| **라우트** | `/pkd/certificates` |
| **파일** | `frontend/src/pages/CertificateSearch.tsx` |
| **인증** | 불필요 (공개) |

### 기능 설명

LDAP 기반 인증서 검색 및 상세 조회 페이지. 다양한 필터와 내보내기 기능을 제공한다.

- **필터**: 국가, 인증서 유형(CSCA/DSC/DSC_NC/MLSC/CRL), 유효성, 출처, 검색어(Subject DN/Issuer DN/지문)
- **검색 결과 테이블**: 국가(국기 아이콘), 유형, Subject DN, 상태, 유효기간 표시
- **4탭 상세 다이얼로그**: 행 클릭 시 열기
  - **일반**: 인증서 기본 메타데이터
  - **상세**: TreeViewer로 ASN.1 구조 시각화
  - **Doc 9303**: ICAO 9303 준수 체크리스트 (~28개 항목)
  - **포렌식**: AI 포렌식 분석 결과 (10개 카테고리 점수, 위험 소견)
- **내보내기**: 개별 PEM, 국가별 ZIP, 전체 DIT 구조 ZIP (PEM/DER)
- **통계 카드**: 검색 결과의 유효성 분포 표시

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| GET | `/api/certificates/countries` | 국가 목록 |
| GET | `/api/certificates/search` | 인증서 검색 |
| GET | `/api/certificates/validation` | 지문 기반 검증 결과 |
| GET | `/api/certificates/doc9303-checklist` | Doc 9303 체크리스트 |
| GET | `/api/certificates/export/file` | 개별 PEM/DER 내보내기 |
| GET | `/api/certificates/export/country` | 국가별 ZIP |
| GET | `/api/certificates/export/all` | 전체 DIT 구조 ZIP |

### 특수 패턴

- **AbortController**: 필터 변경 시 이전 검색 요청 자동 취소 (stale 결과 방지)
- **출처 필터 적용 시**: LDAP 검색 대신 DB 기반 검색으로 전환

---

## 8. 표준 부적합 DSC 보고서 (DscNcReport)

| 항목 | 내용 |
|------|------|
| **라우트** | `/pkd/dsc-nc` |
| **파일** | `frontend/src/pages/DscNcReport.tsx` |
| **인증** | 불필요 (공개) |

### 기능 설명

ICAO PKD에서 표준 미준수로 분류된 DSC_NC 인증서 분석 보고서 대시보드.

- **요약 카드**: 국가 수, 총 DSC_NC 수, 비준수 코드 수, 만료율
- **유효성 비율 바**: VALID/EXPIRED/NOT_YET_VALID/UNKNOWN 비율 시각화
- **5개 차트**:
  - 비준수 코드별 수평 막대 차트 (코드 hover 시 설명 표시)
  - 국가별 막대 차트 (Y축에 국기 SVG 아이콘)
  - 발급 연도별 막대 차트
  - 서명 알고리즘 원형 차트
  - 공개 키 알고리즘 원형 차트
- **필터**: 국가 드롭다운, 비준수 코드 드롭다운
- **테이블**: 국가(국기), 발급연도, 알고리즘, 키 크기(2048bit 미만 빨간 강조), 유효기간, 상태
- **CSV 내보내기**: 14개 컬럼, BOM 포함 (Excel UTF-8 호환)

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| GET | `/api/certificates/dsc-nc/report` | DSC_NC 보고서 데이터 |

---

## 9. CRL 보고서 (CrlReport)

| 항목 | 내용 |
|------|------|
| **라우트** | `/pkd/crl` |
| **파일** | `frontend/src/pages/CrlReport.tsx` |
| **인증** | 불필요 (공개) |

### 기능 설명

인증서 폐기 목록(CRL) 분석 보고서 대시보드.

- **요약 카드**: 총 CRL 수, 국가 수, 유효/만료 CRL 수, 총 폐기 인증서 수
- **상태 비율 바**: VALID/EXPIRED CRL 비율 시각화
- **3개 차트**:
  - 국가별 폐기 인증서 수 막대 차트 (전체 폭)
  - 서명 알고리즘 원형 차트
  - 폐기 사유별 막대 차트 (RFC 5280 11가지 사유 한국어 번역)
- **필터**: 국가 드롭다운, 상태(전체/유효/만료) 드롭다운
- **CRL 테이블**: 국가, 발급자(CN만 표시), 유효기간, 폐기 인증서 수, 상태
- **상세 다이얼로그**: 행 클릭 시 CRL 메타데이터 + 폐기 인증서 목록(일련번호, 날짜, 사유) 표시
- **CRL 다운로드**: `.crl` 바이너리 파일 다운로드 (DER 형식, `application/pkix-crl`)

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| GET | `/api/certificates/crl/report` | CRL 보고서 집계 |
| GET | `/api/certificates/crl/{id}` | CRL 상세 (폐기 목록) |
| GET | `/api/certificates/crl/{id}/download` | CRL 바이너리 다운로드 |

### 폐기 사유 한국어 번역

| RFC 5280 코드 | 한국어 |
|---------------|--------|
| keyCompromise | 키 손상 |
| cACompromise | CA 손상 |
| affiliationChanged | 소속 변경 |
| superseded | 대체됨 |
| cessationOfOperation | 운영 중단 |
| certificateHold | 인증서 보류 |
| removeFromCRL | CRL에서 제거 |
| privilegeWithdrawn | 권한 철회 |
| aACompromise | AA 손상 |
| unspecified | 미지정 |

---

## 10. DSC Trust Chain 보고서 (TrustChainValidationReport)

| 항목 | 내용 |
|------|------|
| **라우트** | `/pkd/trust-chain` |
| **파일** | `frontend/src/pages/TrustChainValidationReport.tsx` |
| **인증** | 불필요 (공개) |

### 기능 설명

DSC Trust Chain 검증 통계 및 샘플 인증서 빠른 조회 페이지.

- **검증 통계 카드**: 총 검증/VALID/EXPIRED_VALID/PENDING/INVALID 수량
- **체인 경로 분포 바**: 깊이별 색상 코딩 표시
  - DSC → Root CSCA (에메랄드)
  - DSC → Link → Root CSCA (파랑)
  - DSC → Link → Link → Root CSCA (보라)
  - 기타 경로
- **샘플 인증서 버튼**: 6개 국가(KR, HU, LU, NL)의 실제 VALID 인증서 — 클릭 시 자동 조회
- **QuickLookupPanel**: Subject DN 또는 지문 입력 → 사전 계산된 Trust Chain 결과 즉시 조회

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| GET | `/api/upload/statistics` | 검증 통계 |
| POST | `/api/certificates/pa-lookup` | Trust Chain 빠른 조회 |

---

## 11. PA 검증 (PAVerify)

| 항목 | 내용 |
|------|------|
| **라우트** | `/pa/verify` |
| **파일** | `frontend/src/pages/PAVerify.tsx` |
| **인증** | 불필요 (공개) |

### 기능 설명

여권 Passive Authentication(PA) 검증 페이지. ICAO 9303 Part 10 & 11 기반 8단계 검증을 수행한다.

#### 전체 검증 모드

SOD 및 DG 파일을 업로드하여 8단계 검증 수행:

1. SOD 파싱 (ASN.1 구조 해석)
2. 서명 인증서(DSC) 추출
3. DSC → CSCA Trust Chain 검증
4. CSCA 조회 (LDAP)
5. SOD 서명 검증 (DSC로 서명 확인)
6. DG 해시 검증 (SOD 내 해시와 실제 DG 해시 비교)
7. CRL 확인 (DSC 폐기 여부)
8. 최종 판정 (VALID/INVALID)

검증 완료 후 DG1(MRZ 데이터) 및 DG2(안면 이미지, JPEG2000→JPEG 자동 변환) 자동 파싱.

#### 간편 검증 모드

Subject DN 또는 SHA-256 지문으로 사전 계산된 Trust Chain 결과를 즉시 조회 (5~20ms 응답).

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| POST | `/api/pa/verify` | 전체 PA 검증 (8단계) |
| POST | `/api/pa/parse-dg1` | DG1 파싱 (MRZ 데이터) |
| POST | `/api/pa/parse-dg2` | DG2 파싱 (안면 이미지) |
| POST | `/api/certificates/pa-lookup` | 빠른 Trust Chain 조회 |

### 사용자 인터랙션

- 전체/간편 검증 모드 토글 버튼
- 파일 업로드 영역 (.bin: SOD, DG1~DG16)
- 디렉토리 전체 업로드 (파일명 패턴 자동 분류: `sod.bin`→SOD, `dg1.bin`→DG1)
- MRZ 텍스트 파일 선택 (.txt, TD3 44자×2줄 형식)
- "검증 실행" 버튼
- 8단계 검증 과정 단계별 시각화 (성공/실패 표시)

---

## 12. PA 이력 (PAHistory)

| 항목 | 내용 |
|------|------|
| **라우트** | `/pa/history` |
| **파일** | `frontend/src/pages/PAHistory.tsx` |
| **인증** | 불필요 (공개) |

### 기능 설명

PA 검증 이력 목록 페이지. 5건/페이지 기본 표시.

- **필터**: 상태(VALID/INVALID/ERROR), 국가, 날짜 범위(시작/종료)
- **테이블**: 검증 시간, 사용자(익명 시 IP 표시), 국가(국기), 결과 상태 배지
- **상세 모달**: 행 클릭 시 전체 검증 결과 표시 (DG1/DG2 데이터 포함)
- **DSC 비준수 경고**: 비준수 인증서 사용 시 경고 배너 + 코드/설명 표시
- **익명 사용자**: `anonymous (192.168.1.100)` 형식으로 클라이언트 IP 표시, User-Agent 40자 축약

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| GET | `/api/pa/history` | PA 이력 (필터, 페이지네이션) |

---

## 13. PA 상세 (PADetail)

| 항목 | 내용 |
|------|------|
| **라우트** | `/pa/:paId` |
| **파일** | `frontend/src/pages/PADetail.tsx` |
| **인증** | 불필요 (공개) |

### 기능 설명

특정 PA 검증의 전체 상세 페이지.

- **상태 배너**: 그라디언트 배경 (VALID=초록, INVALID=빨강, ERROR=노랑)
- **3개 검증 카드**:
  - 인증서 체인 검증 (DSC → Link → CSCA 경로)
  - SOD 서명 검증 (검증 결과 + 사유)
  - Data Group 해시 검증 (예상값 vs 실제값 비교 테이블)
- **우측 사이드바**: 검증 정보(시간, 요청자, 국가), 검증 요약, 빠른 작업 버튼

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| GET | `/api/pa/{paId}` | PA 검증 상세 |

---

## 14. PA 대시보드 (PADashboard)

| 항목 | 내용 |
|------|------|
| **라우트** | `/pa/dashboard` |
| **파일** | `frontend/src/pages/PADashboard.tsx` |
| **인증** | 불필요 (공개) |

### 기능 설명

PA 검증 통계 대시보드.

- **통계 카드**: 전체 검증/VALID/INVALID/ERROR 수량 (서버 통계 API 기반, 페이지별 계산 아님)
- **도넛 차트**: 검증 상태 분포 (중앙에 전체 건수 표시)
- **국가 Top-10**: 수평 진행 바 + 국기 SVG 아이콘
- **30일 트렌드**: AreaChart with gradient fill (Valid/Invalid/Error 스택)
- **다크 모드 대응**: 차트 배경/텍스트 색상 동적 계산

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| GET | `/api/pa/statistics` | PA 통계 요약 |
| GET | `/api/pa/history?page=0&size=100` | 최근 100건 (트렌드 계산) |

---

## 15. 동기화 대시보드 (SyncDashboard)

| 항목 | 내용 |
|------|------|
| **라우트** | `/sync` |
| **파일** | `frontend/src/pages/SyncDashboard.tsx` |
| **인증** | 불필요 (공개) |

### 기능 설명

DB-LDAP 동기화 상태 모니터링 및 관리 대시보드.

- **동기화 상태 카드**: DB vs LDAP 인증서 수량 비교 (유형별: CSCA/DSC/DSC_NC/CRL)
- **불일치 항목**: DB에 있으나 LDAP에 없는 인증서 수 + "재조정 실행" 버튼
- **수동 동기화 체크**: 즉시 DB-LDAP 상태 비교 트리거
- **인증서 재검증**: 3단계 파이프라인 실행
  - Step 1: 만료 상태 검사 (전체 인증서 유효기간 재확인)
  - Step 2: Trust Chain 재검증 (PENDING/INVALID DSC의 CSCA 체인 재확인)
  - Step 3: CRL 폐기 재검사 (VALID DSC의 CRL 폐기 여부 재확인)
- **재검증 결과 다이얼로그**: 3단계별 처리 수량/변경 수량/오류 수량 상세 표시
- **재검증 이력 테이블**: TC 대상, TC VALID, CRL 검사, CRL 폐기 컬럼
- **재조정 이력**: 실행 시간, 트리거(MANUAL/AUTO/DAILY), 처리/성공/실패 수량
- **설정 관리**: 일일 동기화 시간, 인증서 자동 재검증, 자동 재조정 on/off

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| GET | `/api/sync/status` | DB-LDAP 동기화 현황 |
| GET | `/api/sync/history` | 동기화 체크 이력 |
| POST | `/api/sync/check` | 수동 동기화 체크 |
| POST | `/api/sync/reconcile` | 재조정 실행 |
| POST | `/api/sync/revalidate` | 인증서 재검증 (3단계) |
| POST | `/api/sync/trigger-daily` | 일일 동기화 수동 트리거 |
| GET | `/api/sync/config` | 동기화 설정 조회 |
| PUT | `/api/sync/config` | 동기화 설정 변경 |
| GET | `/api/sync/revalidation-history` | 재검증 이력 |
| GET | `/api/sync/reconcile/history` | 재조정 이력 |
| GET | `/api/sync/reconcile/{id}` | 재조정 상세 |

---

## 16. ICAO 상태 (IcaoStatus)

| 항목 | 내용 |
|------|------|
| **라우트** | `/icao` |
| **파일** | `frontend/src/pages/IcaoStatus.tsx` |
| **인증** | 불필요 (공개) |

### 기능 설명

ICAO PKD 버전 모니터링 페이지.

- **3개 패널**: DSC/CRL, DSC_NC, CSCA Master List 각각의 ICAO 최신 버전 vs 로컬 버전 비교
- **버전 차이 표시**: `+N (vX → vY)` 형식
- **업데이트 필요 시**: ICAO 포털 다운로드 링크 표시
- **버전 이력 테이블**: 최근 10건의 버전 확인 이력 (5가지 상태: DETECTED/NOTIFIED/DOWNLOADED/IMPORTED/FAILED)
- **라이프사이클 설명**: ICAO 버전 관리 프로세스 인포 박스

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| GET | `/api/icao/status` | ICAO 버전 비교 (3개 유형) |
| GET | `/api/icao/history?limit=10` | 버전 확인 이력 |
| GET | `/api/icao/check-updates` | 수동 버전 확인 트리거 |

---

## 17. 시스템 모니터링 (MonitoringDashboard)

| 항목 | 내용 |
|------|------|
| **라우트** | `/monitoring` |
| **파일** | `frontend/src/pages/MonitoringDashboard.tsx` |
| **인증** | 불필요 (공개) |

### 기능 설명

시스템 리소스 및 서비스 상태 실시간 모니터링 대시보드. 10초 간격으로 자동 갱신.

- **시스템 메트릭 카드**: CPU 사용률, 메모리 사용률, 디스크 사용률, 네트워크 I/O
  - 진행 바 색상: <60% 초록, <80% 노랑, ≥80% 빨강
- **서비스 상태 카드**: 5개 서비스 (PKD Management, PA Service, PKD Relay, Monitoring, AI Analysis) + DB + LDAP
  - 상태: UP(초록), DEGRADED(노랑), DOWN(빨강)
  - 응답 시간 표시
- **전체 상태 배너**: 모든 서비스 정상=초록, 부분 장애=노랑
- **부가 메트릭** (선택적, 로드 실패 시 무시):
  - nginx 활성 연결 수 카드
  - 서비스별 연결 풀 차트
  - 요청률 트렌드 차트
  - 레이턴시 트렌드 차트

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| GET | `/api/monitoring/system/overview` | 시스템 메트릭 |
| GET | `/api/monitoring/services/health` | 서비스 상태 |
| GET | `/api/health/database` | DB 연결 (응답 시간 측정) |
| GET | `/api/health/ldap` | LDAP 연결 |
| GET | `/api/monitoring/load/snapshot` | nginx 연결 + 풀 |
| GET | `/api/monitoring/load/history?count=30` | 트렌드 이력 |

---

## 18. AI 인증서 분석 (AiAnalysisDashboard)

| 항목 | 내용 |
|------|------|
| **라우트** | `/ai/analysis` |
| **파일** | `frontend/src/pages/AiAnalysisDashboard.tsx` |
| **인증** | 불필요 (공개) |

### 기능 설명

ML 기반 인증서 이상 탐지 및 포렌식 분석 대시보드.

- **분석 통계 카드**: 총 분석/정상/의심/이상 수량, 평균 위험 점수, 평균 포렌식 점수
- **위험 수준 분포 바**: LOW/MEDIUM/HIGH/CRITICAL 비율 시각화
- **국가별 PKI 성숙도**: 상위 15개국 수평 막대 차트 (Y축 국기 아이콘)
- **알고리즘 마이그레이션 트렌드**: 연도별 누적 스택 AreaChart (SHA-256, SHA-384 등)
- **키 크기 분포**: 알고리즘 패밀리별 원형 차트
- **포렌식 분석 요약**: 심각도 분포 + 상위 소견
- **발급자 프로파일**: 발급자별 인증서 수, 준수율, 위험 수준 (상위 15개)
- **확장 필드 준수**: ICAO Doc 9303 확장 규칙 위반 목록
- **이상 목록 테이블**: 필터(국가/유형/이상수준/위험수준), 15건/페이지, CSV 내보내기
- **분석 실행**: "분석 실행" 버튼 → 백그라운드 배치 분석, 3초 폴링으로 진행률 표시

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| GET | `/api/ai/statistics` | 전체 분석 통계 |
| GET | `/api/ai/analyze/status` | 분석 작업 상태 |
| GET | `/api/ai/anomalies` | 이상 목록 (필터, 페이지네이션) |
| GET | `/api/ai/reports/country-maturity` | 국가별 PKI 성숙도 |
| GET | `/api/ai/reports/algorithm-trends` | 알고리즘 트렌드 |
| GET | `/api/ai/reports/risk-distribution` | 위험 수준 분포 |
| GET | `/api/ai/reports/key-size-distribution` | 키 크기 분포 |
| GET | `/api/ai/reports/forensic-summary` | 포렌식 요약 |
| GET | `/api/ai/reports/issuer-profiles` | 발급자 프로파일 |
| GET | `/api/ai/reports/extension-anomalies` | 확장 규칙 위반 |
| POST | `/api/ai/analyze` | 전체 분석 실행 |

### 특수 패턴

- **3초 폴링**: 분석 실행 중에만 활성화, COMPLETED/FAILED 시 자동 중단
- **AbortController**: 이상 목록 필터 변경 시 이전 요청 취소
- **Promise.allSettled**: 7개 통계 API 병렬 호출, 개별 실패 허용
- **stale closure 방지**: `fetchDataRef`/`fetchAnomaliesRef`로 최신 함수 참조

---

## 19. API 클라이언트 관리 (ApiClientManagement)

| 항목 | 내용 |
|------|------|
| **라우트** | `/admin/api-clients` |
| **파일** | `frontend/src/pages/ApiClientManagement.tsx` |
| **인증** | 필요 (JWT, 관리자 전용) |

### 기능 설명

외부 시스템 API Key 발급 및 접근 권한 관리 페이지.

- **클라이언트 목록**: 이름, API Key prefix, 상태, 마지막 사용 시간, 요청 수 표시
- **클라이언트 등록**: 이름, 설명, 10가지 권한 체크박스, 허용 IP(CIDR 지원), Rate Limit(분/시/일) 설정
- **API Key 발급**: 발급 직후 일회성 표시 → 이후 SHA-256 해시만 저장 (복구 불가)
  - Key 형식: `icao_{prefix}_{random}` (46자)
  - Eye/EyeOff 가시성 토글, 클립보드 복사 버튼
- **Key 재발급**: 기존 Key 무효화 + 새 Key 발급 (확인 다이얼로그)
- **사용량 조회**: 7/30/90일 기간별 엔드포인트 요청 수 막대 차트 + 상세 테이블
- **수정/비활성화**: 권한, Rate Limit, IP 화이트리스트 변경, 소프트 삭제

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| GET | `/api/auth/api-clients` | 클라이언트 목록 |
| POST | `/api/auth/api-clients` | 클라이언트 등록 |
| PUT | `/api/auth/api-clients/{id}` | 클라이언트 수정 |
| DELETE | `/api/auth/api-clients/{id}` | 클라이언트 비활성화 |
| POST | `/api/auth/api-clients/{id}/regenerate` | Key 재발급 |
| GET | `/api/auth/api-clients/{id}/usage` | 사용량 통계 |

### 10가지 API 권한

| 권한 | 설명 |
|------|------|
| cert:read | 인증서 조회 |
| cert:export | 인증서 내보내기 |
| pa:verify | PA 검증 실행 |
| pa:read | PA 이력 조회 |
| upload:read | 업로드 이력 조회 |
| upload:write | 파일 업로드 |
| report:read | 보고서 조회 |
| ai:read | AI 분석 결과 조회 |
| sync:read | 동기화 상태 조회 |
| icao:read | ICAO 상태 조회 |

---

## 20. 로그인 이력 (AuditLog)

| 항목 | 내용 |
|------|------|
| **라우트** | `/admin/audit-log` |
| **파일** | `frontend/src/pages/AuditLog.tsx` |
| **인증** | 필요 (JWT, 관리자 전용) |

### 기능 설명

사용자 인증 감사 로그 조회 페이지.

- **통계 카드**: 전체 로그/실패 로그인/24시간 내 로그/활성 사용자 수
- **필터**: 사용자명, 이벤트 타입(LOGIN/LOGIN_FAILED/LOGOUT/TOKEN_REFRESH), 성공 여부
- **테이블**: 시간, 사용자명, 이벤트(색상 배지), 성공/실패, IP 주소
- **상세 다이얼로그**: 로그 ID, 생성 시간, 사용자 ID, 이벤트 유형, IP, User-Agent, 오류 메시지

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| GET | `/api/auth/audit-log` | 인증 감사 로그 목록 |
| GET | `/api/auth/audit-log/stats` | 감사 통계 |

### 이벤트 타입 배지 색상

| 이벤트 | 색상 |
|--------|------|
| LOGIN | 초록 |
| LOGIN_FAILED | 빨강 |
| LOGOUT | 노랑 |
| TOKEN_REFRESH | 보라 |

---

## 21. 운영 감사 로그 (OperationAuditLog)

| 항목 | 내용 |
|------|------|
| **라우트** | `/admin/operation-audit` |
| **파일** | `frontend/src/pages/OperationAuditLog.tsx` |
| **인증** | 필요 (JWT, 관리자 전용) |

### 기능 설명

시스템 작업 추적 및 모니터링 페이지. 26가지 작업 유형에 대한 운영 감사 로그를 조회한다.

- **통계 카드**: 총 작업/성공/실패/활성 사용자/평균 소요시간
- **필터**: 작업 유형(26가지), 사용자명, 성공 여부, 날짜 범위
- **테이블**: 시간, 작업 유형(아이콘+한국어 라벨), 사용자, 성공/실패, 소요시간, IP
- **상세 다이얼로그**: 기본/작업/요청/결과 정보 + 메타데이터 JSON

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| GET | `/api/audit/operations` | 운영 감사 로그 목록 |
| GET | `/api/audit/operations/stats` | 운영 통계 |

### 작업 유형 (26가지)

| 분류 | 작업 유형 |
|------|----------|
| 업로드 | FILE_UPLOAD, CERT_UPLOAD, UPLOAD_DELETE, UPLOAD_RETRY |
| PA | PA_VERIFY |
| 동기화 | SYNC_CHECK, RECONCILE, REVALIDATE, TRIGGER_DAILY_SYNC, CONFIG_UPDATE |
| ICAO | ICAO_CHECK |
| API 클라이언트 | API_CLIENT_CREATE, API_CLIENT_UPDATE, API_CLIENT_DELETE, API_CLIENT_KEY_REGEN |
| 코드 마스터 | CODE_MASTER_CREATE, CODE_MASTER_UPDATE, CODE_MASTER_DELETE |
| 사용자 | USER_CREATE, USER_UPDATE, USER_DELETE, PASSWORD_CHANGE |
| 인증서 | CERTIFICATE_EXPORT, CERTIFICATE_DELETE |
| 기타 | SYSTEM_CONFIG, OTHER |

---

## 22. 사용자 관리 (UserManagement)

| 항목 | 내용 |
|------|------|
| **라우트** | `/admin/users` |
| **파일** | `frontend/src/pages/UserManagement.tsx` |
| **인증** | 필요 (JWT, 관리자 전용) |

### 기능 설명

시스템 사용자 및 권한 관리 페이지. 그리드 카드 레이아웃으로 사용자 목록을 표시한다.

- **사용자 카드**: 아바타(이니셜), 사용자명, 이름, 이메일, 권한 배지, 마지막 로그인
- **사용자 추가**: 사용자명, 비밀번호, 이름, 이메일, 관리자 토글, 권한 그리드
- **권한 그리드**: 메뉴 섹션별 그룹화된 체크박스 (선택 시 테두리 파란색 강조)
- **사용자 수정**: 사용자명 외 모든 필드 수정 가능
- **비밀번호 변경**: 자기 자신 → 현재 비밀번호 필수, 타인 → 바로 변경
- **사용자 삭제**: 확인 다이얼로그 후 삭제
- **실시간 검색**: 사용자명/이메일/이름으로 필터링

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| GET | `/api/auth/users` | 사용자 목록 |
| POST | `/api/auth/users` | 사용자 생성 |
| PUT | `/api/auth/users/{id}` | 사용자 수정 |
| DELETE | `/api/auth/users/{id}` | 사용자 삭제 |
| PUT | `/api/auth/users/{id}/password` | 비밀번호 변경 |

---

## 23. 로그인 (Login)

| 항목 | 내용 |
|------|------|
| **라우트** | `/login` |
| **파일** | `frontend/src/pages/Login.tsx` |
| **인증** | 불필요 |

### 기능 설명

시스템 진입점이자 랜딩 페이지.

- **2패널 레이아웃**: 좌측 히어로 패널 (시스템 소개) + 우측 로그인 폼
- **JWT 인증**: 로그인 성공 시 `access_token`과 `user` 정보를 localStorage에 저장
- **자동 리다이렉트**: 이미 인증된 사용자는 `/`로 자동 이동
- **다크/라이트 모드 토글**: Sun/Moon 아이콘으로 테마 전환
- **개발 환경 힌트**: `import.meta.env.DEV` 환경에서만 기본 자격증명 표시
- **슬라이드 인 애니메이션**: 로그인 폼 요소 순차 등장

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| POST | `/api/auth/login` | JWT 토큰 발급 |

---

## 24. 프로필 (Profile)

| 항목 | 내용 |
|------|------|
| **라우트** | `/profile` |
| **파일** | `frontend/src/pages/Profile.tsx` |
| **인증** | 필요 (JWT) |

### 기능 설명

현재 로그인한 사용자의 프로필 및 계정 설정 페이지.

- **사용자 정보 카드**: 사용자명, 이름, 이메일, 역할 (JWT 페이로드에서 읽기, API 호출 없음)
- **권한 카드**: 관리자 → "전체 권한(관리자)" 배너, 일반 사용자 → 그룹별 체크/엑스 아이콘
- **비밀번호 변경**: 인라인 폼 토글 (별도 모달 없음)
  - 현재 비밀번호 입력 (Eye/EyeOff 토글)
  - 새 비밀번호 입력 (8자 이상 유효성 검사)
  - 새 비밀번호 확인 (실시간 일치 여부 표시)

### 사용 API

| 메서드 | 엔드포인트 | 용도 |
|--------|-----------|------|
| PUT | `/api/auth/users/{userId}/password` | 비밀번호 변경 |

---

## 공통 패턴 요약

### 인증 및 접근 제어

| 유형 | 페이지 |
|------|--------|
| 공개 (JWT 불필요) | Dashboard, CertificateSearch, DscNcReport, CrlReport, TrustChain, PAVerify, PAHistory, PADetail, PADashboard, SyncDashboard, IcaoStatus, Monitoring, AiAnalysis, UploadHistory, UploadDetail, UploadDashboard |
| JWT 필요 | FileUpload, CertificateUpload, Profile |
| 관리자 전용 | ApiClientManagement, AuditLog, OperationAuditLog, UserManagement |

### 데이터 갱신 방식

| 방식 | 페이지 | 간격 | 조건 |
|------|--------|------|------|
| SSE (실시간) | FileUpload | 이벤트 기반 | 업로드 진행 중 |
| 폴링 | UploadHistory | 5초 | PENDING/PROCESSING 존재 시 |
| 폴링 | UploadDetail | 3초 | PENDING/PROCESSING 상태 시 |
| 폴링 | MonitoringDashboard | 10초 | 항상 |
| 폴링 | AiAnalysisDashboard | 3초 | 분석 실행 중 |
| 수동 | 나머지 페이지 | - | 사용자 액션 시 |

### 성능 최적화 패턴

| 패턴 | 적용 페이지 |
|------|------------|
| `React.lazy()` 코드 스플리팅 | 전체 22개 페이지 |
| `AbortController` (stale 요청 취소) | CertificateSearch, DscNcReport, MonitoringDashboard, AiAnalysisDashboard |
| `useRef` (stale closure 방지) | FileUpload, AiAnalysisDashboard |
| `Promise.allSettled` (병렬 호출) | AiAnalysisDashboard, Dashboard |
| `useMemo` (재계산 방지) | AiAnalysisDashboard |

### 차트 라이브러리 (Recharts)

| 차트 유형 | 사용 페이지 |
|-----------|------------|
| LineChart | UploadDashboard |
| BarChart (수직) | DscNcReport, CrlReport, AiAnalysisDashboard |
| BarChart (수평) | DscNcReport, AiAnalysisDashboard, ApiClientManagement |
| PieChart | DscNcReport, CrlReport, PADashboard, AiAnalysisDashboard |
| AreaChart | PADashboard, AiAnalysisDashboard |

### 다크 모드

- Zustand `useThemeStore` — 전역 상태 관리, localStorage 영속
- Tailwind `dark:` 접두사 — 다크 모드 스타일
- 차트 색상: `isDark` 상태에 따라 배경/텍스트 색상 동적 결정

### 국제화 (i18n)

- `i18n-iso-countries` 라이브러리 — 국가 코드 → 한국어 이름 변환
- 국기 SVG: `/svg/{countryCode}.svg` 경로
- 사용 페이지: DscNcReport, CrlReport, PADashboard, AiAnalysisDashboard, CertificateSearch

### Oracle 호환성 (프론트엔드)

| 헬퍼 | 용도 | 사용 페이지 |
|------|------|------------|
| `toBool(v)` | true/1/"1"/"true" 처리 | UserManagement |
| `parsePermissions(v)` | 배열/JSON 문자열/null 처리 | UserManagement |
| `stored_in_ldap` 문자열 | "1"/"0" boolean 처리 | CrlReport |
