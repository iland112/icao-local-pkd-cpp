# AI Certificate Forensic Analysis Engine Enhancement

**Version**: v2.20.0 (Stage A + C-2)
**Created**: 2026-02-22
**Status**: Stage A + C-2 Complete, Pending Verification (Stage B)

---

## Overview

AI Analysis Service(v1.0, 25-feature statistical anomaly detection)를 PKI 포렌식 분석 도구로 강화합니다.

**접근 전략**: Stage A(AI 서비스만) → Stage B(검증/안정화) → Stage C(통합)

| Stage | 범위 | 상태 |
|-------|------|------|
| **Stage A** | AI 서비스 내부 강화 (Python + DB) | **완료** |
| **Stage B** | 검증 및 안정화 (실 데이터 튜닝) | **다음 단계** |
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

## Stage B: 검증 및 안정화

> Stage A 완료 후 실제 데이터(31,212 인증서)로 검증 및 튜닝

### 검증 항목

| 항목 | 기준 |
|------|------|
| False Positive | CRITICAL < 5%, HIGH < 15% |
| 유형별 모델 | ANOMALOUS 비율 5% ± 2% |
| 발급자 프로파일 | 상위 이상 발급자 타당성 |
| 확장 규칙 위반 | 실제 ICAO 미달 여부 |
| Oracle 호환 | 전체 파이프라인 정상 |
| 성능 | 배치 < 10분, 증분 < 30초 |
| 메모리 | RSS < 500MB |

### 안정화 기준 (Stage C 전환 조건)

1. False Positive 목표 이내
2. 3회 이상 일관된 결과
3. PostgreSQL + Oracle 정상
4. 성능/메모리 목표 달성

---

## Stage C-2: 프론트엔드 통합 (구현 범위)

| 파일 | 변경 |
|------|------|
| `frontend/src/api/aiAnalysisApi.ts` | 5개 API 함수 + TS 인터페이스 |
| `frontend/src/pages/AiAnalysisDashboard.tsx` | 포렌식 요약, 발급자 차트, 확장 위반 차트 |
| `frontend/src/components/certificate/CertificateDetailDialog.tsx` | "포렌식" 탭 추가 |
| `frontend/src/components/ai/ForensicAnalysisPanel.tsx` | **신규** |
| `frontend/src/components/ai/IssuerProfileCard.tsx` | **신규** |
| `frontend/src/components/ai/ExtensionComplianceChecklist.tsx` | **신규** |

---

## Stage C-1: 업로드 파이프라인 연동 (보류)

> Stage B 안정화 완료 후 별도 작업으로 진행

업로드 COMPLETED 후 fire-and-forget HTTP 콜백으로 AI 증분 분석 트리거.
`services/pkd-management/src/processing_strategy.cpp` +15줄.

---

## 영향 평가

| 항목 | 영향 |
|------|------|
| **변경 범위** | AI 서비스 12파일 + Frontend 6파일 |
| **다른 C++ 서비스** | **변경 없음** |
| **기존 API 호환** | **100% 하위 호환** |
| **기존 분석 결과** | 유지 (새 컬럼 NULL, 재분석 시 채워짐) |
| **Docker** | `ai-analysis` + `frontend` 재빌드 |
| **requirements.txt** | 변경 없음 (DBSCAN은 scikit-learn 내장) |
| **리스크** | LOW-MEDIUM |

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
| `docker/db-oracle/init/02-schema.sql` | 수정 |

### Stage C-2 (프론트엔드)

| 파일 | 액션 |
|------|------|
| `frontend/src/api/aiAnalysisApi.ts` | 수정 |
| `frontend/src/pages/AiAnalysisDashboard.tsx` | 수정 |
| `frontend/src/components/certificate/CertificateDetailDialog.tsx` | 수정 |
| `frontend/src/components/ai/ForensicAnalysisPanel.tsx` | **신규** |
| `frontend/src/components/ai/IssuerProfileCard.tsx` | **신규** |
| `frontend/src/components/ai/ExtensionComplianceChecklist.tsx` | **신규** |

---

## 구현 진행 상황

- [x] 계획 문서 작성
- [ ] A-1. Feature Engineering 확장 (25→45)
- [ ] A-2. 유형별 분리 모델
- [ ] A-3. 확장 규칙 엔진
- [ ] A-4. 발급자 프로파일링
- [ ] A-5. Forensic Risk Scoring (6→10)
- [ ] A-6. API 엔드포인트 확장
- [ ] A-7. DB 스키마 변경
- [ ] C-2. 프론트엔드 통합
- [ ] Docker 빌드 + 검증
