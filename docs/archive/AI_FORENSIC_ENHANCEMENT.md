# AI Certificate Forensic Analysis Engine Enhancement

**Version**: v2.20.0
**Created**: 2026-02-22
**Status**: Stage A + B + C-2 Complete | Stage C-1 보류

---

## Overview

AI Analysis Service(v1.0, 25-feature statistical anomaly detection)를 PKI 포렌식 분석 도구로 강화합니다.

**접근 전략**: Stage A(AI 서비스만) → Stage B(검증/안정화) → Stage C(통합)

| Stage | 범위 | 상태 |
|-------|------|------|
| **Stage A** | AI 서비스 내부 강화 (Python + DB) | **완료** |
| **Stage B** | 검증 및 안정화 (실 데이터 튜닝) | **완료** (Oracle + PostgreSQL) |
| **Stage C-2** | 프론트엔드 통합 | **완료** |
| **Stage C-1** | 업로드 파이프라인 연동 | **보류** |

**핵심 원칙**: 다른 서비스(PKD Management, PA Service) 변경 없이 AI 서비스 + DB + Frontend만 수정

---

## Stage A: AI 서비스 내부 강화

> **변경 범위**: `services/ai-analysis/` + DB init 스크립트
> **C++ 변경**: 없음 | **nginx 변경**: 없음

### A-1. Feature Engineering 확장 (25 → 45 features)

기존 25개 특성에 20개 포렌식 특성 추가:

| 카테고리 | 특성 | 설명 |
|----------|------|------|
| **발급자 프로파일 (4)** | `issuer_cert_count` | 동일 발급자 인증서 수 |
| | `issuer_anomaly_rate` | 동일 발급자의 이상 비율 |
| | `issuer_type_diversity` | 발급자가 발급한 인증서 유형 다양성 |
| | `issuer_country_match` | issuer/subject C= 필드 일치 여부 |
| **시간 패턴 (4)** | `issuance_month` | 발급 월 (계절성 탐지) |
| | `validity_period_zscore` | 동일 유형 대비 유효기간 Z-score |
| | `issuance_rate_deviation` | 국가별 발급 속도 대비 편차 |
| | `cert_age_ratio` | 현재 시점 대비 인증서 수명 비율 |
| **DN 구조 (4)** | `subject_dn_field_count` | Subject DN 필드 수 |
| | `issuer_dn_field_count` | Issuer DN 필드 수 |
| | `dn_format_type` | DN 포맷 유형 (RFC 2253 vs slash) |
| | `subject_has_email` | Subject에 이메일 포함 여부 |
| **확장 프로파일 (4)** | `extension_set_hash` | 확장 조합 해시 (유형별 정상 패턴) |
| | `unexpected_extensions` | 유형 대비 예상 외 확장 수 |
| | `missing_required_extensions` | 유형 대비 필수 확장 누락 수 |
| | `critical_extension_count` | critical 마킹 확장 수 |
| **교차 인증서 (4)** | `same_issuer_key_size_dev` | 동일 발급자 대비 키 크기 편차 |
| | `same_issuer_algo_match` | 동일 발급자 일반 알고리즘 일치 여부 |
| | `country_peer_risk_avg` | 동일 국가 평균 리스크 |
| | `type_peer_extension_match` | 동일 유형 확장 패턴 일치도 |

**수정 파일**: `services/ai-analysis/app/services/feature_engineering.py`

### A-2. 인증서 유형별 분리 모델

단일 모델 → 유형별 독립 모델:

| 유형 | 데이터 수 | 전략 |
|------|-----------|------|
| CSCA | 845 | IF+LOF (is_ca, path_len 가중치 ↑) |
| DSC | 29,838 | IF+LOF (trust_chain, key_usage 가중치 ↑) |
| MLSC | 27 | 규칙 기반 fallback (소규모) |
| DSC_NC | 502 | non-conformant 패턴 전용 |

**수정 파일**: `services/ai-analysis/app/services/anomaly_detector.py`

### A-3. 확장 규칙 엔진 (신규)

ICAO Doc 9303 기반 인증서 유형별 확장 프로파일 규칙 → `structural_anomaly_score` 산출

**신규 파일**: `services/ai-analysis/app/services/extension_rules_engine.py`

### A-4. 발급자 프로파일링 (신규)

DBSCAN 기반 발급자별 behavioral profile → `issuer_anomaly_score` 산출

**신규 파일**: `services/ai-analysis/app/services/issuer_profiler.py`

### A-5. Forensic Risk Scoring (6 → 10 카테고리)

| # | 카테고리 | 최대 점수 | 변경 |
|---|----------|-----------|------|
| 1 | algorithm | 40 | 유지 |
| 2 | key_size | 40 | 유지 |
| 3 | compliance | 20 | 유지 |
| 4 | validity | 15 | 유지 |
| 5 | extensions | 15 | 유지 |
| 6 | anomaly | 15 | 유지 |
| 7 | **issuer_reputation** | **15** | **신규** |
| 8 | **structural_consistency** | **20** | **신규** |
| 9 | **temporal_pattern** | **10** | **신규** |
| 10 | **dn_consistency** | **10** | **신규** |

총 200점 → 정규화 0-100 유지

**수정 파일**: `services/ai-analysis/app/services/risk_scorer.py`

### A-6. API 엔드포인트 확장 (+5개, 기존 12개 하위 호환)

| 엔드포인트 | 설명 |
|-----------|------|
| `GET /api/ai/certificate/{fp}/forensic` | 포렌식 상세 (10 카테고리) |
| `POST /api/ai/analyze/incremental` | 증분 분석 |
| `GET /api/ai/reports/issuer-profiles` | 발급자 프로파일 목록 |
| `GET /api/ai/reports/forensic-summary` | 포렌식 종합 요약 |
| `GET /api/ai/reports/extension-anomalies` | 확장 규칙 위반 목록 |

### A-7. DB 스키마 변경

`ai_analysis_result` 테이블에 6개 컬럼 추가 (NULL 허용, 하위 호환):

```
forensic_risk_score, forensic_risk_level, forensic_findings,
structural_anomaly_score, issuer_anomaly_score, temporal_anomaly_score
```

---

## Stage B: 검증 결과 (Oracle 기반, 2026-02-22)

> 31,212 인증서 대상 전체 배치 분석 + API 검증 완료

### 배치 분석 성능

| 항목 | 측정값 | 목표 | 판정 |
|------|--------|------|------|
| 전체 배치 시간 | **67초** | < 600초 | PASS |
| 메모리 사용 | **291.8 MiB** | < 500MB | PASS |
| 처리 인증서 | 31,212건 | 31,212건 | PASS |

### 분석 결과 통계

| 항목 | 값 |
|------|-----|
| 정상 (NORMAL) | 25,853건 (82.8%) |
| 의심 (SUSPICIOUS) | 5,353건 (17.1%) |
| 이상 (ANOMALOUS) | 6건 (0.02%) |
| 평균 리스크 점수 | 25.3 / 100 |
| 평균 포렌식 점수 | 15.2 / 100 |

### 리스크 분포

| 레벨 | 건수 |
|------|------|
| LOW | 22,098 |
| MEDIUM | 7,723 |
| HIGH | 998 |
| CRITICAL | 393 |

### 포렌식 레벨 분포

| 레벨 | 건수 |
|------|------|
| LOW | 24,092 |
| MEDIUM | 6,701 |
| HIGH | 419 |

### 카테고리별 평균 점수

| 카테고리 | 평균 점수 |
|----------|-----------|
| key_size | 9.06 |
| algorithm | 6.57 |
| extensions | 3.88 |
| validity | 3.33 |
| issuer_reputation | 3.35 |
| anomaly | 2.47 |
| structural_consistency | 1.29 |
| temporal_pattern | 0.46 |
| dn_consistency | 0.0 |
| compliance | 0.0 |

### 주요 포렌식 발견 사항 (top findings)

| 발견 사항 | 건수 | 심각도 |
|-----------|------|--------|
| 인증서 만료됨 | 6,618 | HIGH |
| 비정상적 유효기간 패턴 | 1,801 | MEDIUM |
| 취약한 서명 알고리즘 (SHA-1) | 592 | CRITICAL |
| 취약한 키 크기 (ECDSA 224bit) | 75 | CRITICAL |
| 취약한 서명 알고리즘 (ECDSA-SHA1) | 45 | CRITICAL |
| 취약한 키 크기 (RSA 1024bit) | 43 | CRITICAL |

### ANOMALOUS 인증서 상세 (6건)

| 국가 | 유형 | 점수 | 주요 특징 |
|------|------|------|-----------|
| CM | MLSC | 0.994 | 확장 부재 (CRL DP, 예상 외 확장 5.1σ) |
| CH | MLSC | 0.835 | 확장/DN 패턴 편차, 장기 유효기간 |
| BM | CSCA | 0.794 | 발급자/주체 국가 불일치 29.1σ |
| HU | DSC_NC | 0.731 | 키 크기 편차, RSA 1024bit |
| HU | DSC_NC | 0.730 | 키 크기 편차, RSA 1024bit |
| MN | CSCA | 0.706 | OCSP Responder 16.8σ, 예상 외 확장 10.9σ |

### 추가 검증 데이터

| 항목 | 값 |
|------|-----|
| 발급자 프로파일 | 456개 |
| 확장 규칙 위반 | 50건 |
| 기존 API 호환 (12개) | **전체 200 OK** |
| 새 API (5개) | **전체 200 OK** |

### API 응답 시간

| 엔드포인트 | 응답 시간 |
|-----------|-----------|
| `certificate/{fp}/forensic` | 4ms |
| `risk-distribution` | 89ms |
| `anomalies (paginated)` | 110ms |
| `statistics` | 379ms |
| `forensic-summary` | 648ms |
| `issuer-profiles` | 1.4s |
| `country-maturity` | 1.7s |
| `extension-anomalies` | 2.9s |

---

## Stage B-2: 검증 결과 (PostgreSQL 기반, 2026-02-22)

> 31,212 인증서 대상 전체 배치 분석 + API 검증 완료 (luckfox ARM64 환경)

### Multi-DBMS 호환성 수정 사항

Oracle 기반 Stage B 검증 후, PostgreSQL(luckfox) 배포 시 발견된 3가지 문제를 수정:

| # | 문제 | 원인 | 수정 |
|---|------|------|------|
| 1 | `operator does not exist: character varying = uuid` | `validation_result.certificate_id`가 PostgreSQL에서 UUID, Oracle에서 VARCHAR2(128) | PostgreSQL JOIN: `c.fingerprint_sha256 = v.certificate_id` → `c.id = v.certificate_id` |
| 2 | `ValueError: truth value of an array is ambiguous` | LEFT JOIN 1:N 중복 행에서 `pd.isna()` 비-스칼라 호출 | `safe_isna()` 헬퍼 + `drop_duplicates()` |
| 3 | forensic-summary 불완전 응답 | PostgreSQL JSONB 전용 쿼리에서 `sev_counts`/`top_findings` 빈 값 반환 | Python-side JSON 파싱으로 통합 |

**수정 파일 (8개)**:
- `database.py` — `safe_isna()`, `safe_json_loads()` 헬퍼 추가
- `feature_engineering.py` — PostgreSQL JOIN 수정 + `drop_duplicates()`
- `analysis.py` — 6곳 `json.loads()` → `safe_json_loads()`
- `reports.py` — forensic-summary 통합 (JSONB/CLOB 분기 제거)
- `extension_rules_engine.py`, `issuer_profiler.py`, `risk_scorer.py`, `pattern_analyzer.py` — `pd.isna()` → `safe_isna()`

### 배치 분석 성능

| 항목 | PostgreSQL (luckfox ARM64) | Oracle (dev x86) | 비고 |
|------|---------------------------|------------------|------|
| 전체 배치 시간 | **277초** | **67초** | ARM64 vs x86 차이 |
| 처리 인증서 | 31,212건 | 31,212건 | 동일 |
| API 엔드포인트 | 17/17 200 OK | 17/17 200 OK | 동일 |

### 분석 결과 통계 (PostgreSQL)

| 항목 | 값 |
|------|-----|
| 정상 (NORMAL) | 25,586건 |
| 의심 (SUSPICIOUS) | 5,616건 |
| 이상 (ANOMALOUS) | 10건 |

> Oracle(6건) 대비 ANOMALOUS 4건 증가 — `c.id = v.certificate_id` JOIN 변경으로 validation 데이터 매칭 차이 발생 (정상 범위)

---

## Stage C-2: 프론트엔드 통합 (완료)

| 파일 | 변경 |
|------|------|
| `frontend/src/services/aiAnalysisApi.ts` | 5개 API 함수 + TS 인터페이스 |
| `frontend/src/pages/AiAnalysisDashboard.tsx` | 포렌식 요약, 발급자 차트, 확장 위반 차트 |
| `frontend/src/components/CertificateDetailDialog.tsx` | "포렌식" 탭 추가 (4번째 탭) |
| `frontend/src/pages/CertificateSearch.tsx` | 탭 타입 확장 |
| `frontend/src/components/ai/ForensicAnalysisPanel.tsx` | **신규** — 10카테고리 레이더 차트, 점수 바, 발견 사항 |
| `frontend/src/components/ai/IssuerProfileCard.tsx` | **신규** — 발급자별 수평 바 차트 (top 15) |
| `frontend/src/components/ai/ExtensionComplianceChecklist.tsx` | **신규** — 위반 테이블 + 확장 상세 |

---

## Stage C-1: 업로드 파이프라인 연동 (보류)

> 별도 작업으로 진행 예정. 현재 수동 `POST /api/ai/analyze` 또는 일일 스케줄러로 분석 트리거.

업로드 COMPLETED 후 fire-and-forget HTTP 콜백으로 AI 증분 분석 트리거.
`services/pkd-management/src/processing_strategy.cpp` +15줄.

---

## 영향 평가

| 항목 | 영향 |
|------|------|
| **변경 범위** | AI 서비스 12파일 + Frontend 7파일 |
| **다른 C++ 서비스** | **변경 없음** |
| **기존 API 호환** | **100% 하위 호환** (17개 전체 200 OK 검증됨) |
| **기존 분석 결과** | 유지 (새 컬럼 NULL, 재분석 시 채워짐) |
| **Docker** | `ai-analysis` + `frontend` 재빌드 |
| **requirements.txt** | 변경 없음 (DBSCAN은 scikit-learn 내장) |
| **리스크** | LOW |

---

## 파일 변경 총괄

### Stage A (AI 서비스)

| 파일 | 액션 |
|------|------|
| `services/ai-analysis/app/services/feature_engineering.py` | 수정 |
| `services/ai-analysis/app/services/anomaly_detector.py` | 수정 |
| `services/ai-analysis/app/services/risk_scorer.py` | 수정 |
| `services/ai-analysis/app/services/pattern_analyzer.py` | 수정 |
| `services/ai-analysis/app/services/extension_rules_engine.py` | **신규** |
| `services/ai-analysis/app/services/issuer_profiler.py` | **신규** |
| `services/ai-analysis/app/routers/analysis.py` | 수정 |
| `services/ai-analysis/app/routers/reports.py` | 수정 |
| `services/ai-analysis/app/schemas/analysis.py` | 수정 |
| `services/ai-analysis/app/models/analysis_result.py` | 수정 |
| `docker/db-init/11-ai-analysis.sql` | 수정 |
| `docker/db-oracle/init/11-ai-analysis.sql` | **신규** |

### Stage C-2 (프론트엔드)

| 파일 | 액션 |
|------|------|
| `frontend/src/services/aiAnalysisApi.ts` | 수정 |
| `frontend/src/pages/AiAnalysisDashboard.tsx` | 수정 |
| `frontend/src/pages/CertificateSearch.tsx` | 수정 |
| `frontend/src/components/CertificateDetailDialog.tsx` | 수정 |
| `frontend/src/components/ai/ForensicAnalysisPanel.tsx` | **신규** |
| `frontend/src/components/ai/IssuerProfileCard.tsx` | **신규** |
| `frontend/src/components/ai/ExtensionComplianceChecklist.tsx` | **신규** |

---

## 구현 진행 상황

- [x] 계획 문서 작성
- [x] A-1. Feature Engineering 확장 (25→45)
- [x] A-2. 유형별 분리 모델
- [x] A-3. 확장 규칙 엔진
- [x] A-4. 발급자 프로파일링
- [x] A-5. Forensic Risk Scoring (6→10)
- [x] A-6. API 엔드포인트 확장
- [x] A-7. DB 스키마 변경
- [x] C-2. 프론트엔드 통합
- [x] Docker 빌드 + 검증
- [x] B. Stage B 검증 (Oracle 기반, 31,212건, 67s)
- [x] B. Stage B 검증 (PostgreSQL 기반, 31,212건, 277s) — Multi-DBMS 호환성 수정 포함
- [ ] C-1. 업로드 파이프라인 연동 — **보류**
