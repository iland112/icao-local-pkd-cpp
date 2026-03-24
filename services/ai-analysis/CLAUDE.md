# AI Analysis Service

**Port**: 8085 (via API Gateway :8080/api/ai)
**Language**: Python 3.12, FastAPI, scikit-learn
**Role**: ML 기반 인증서 이상 탐지, 포렌식 위험 스코어링, 패턴 분석

> **납품 범위 제외**: 실험적 구현. 법무부 사업 납품에 포함하지 않음.

---

## API Endpoints

- `GET /api/ai/health` — 헬스 체크
- `POST /api/ai/analyze` — 전체 분석 트리거 (background)
- `POST /api/ai/analyze/incremental` — 증분 분석 (upload_id 기반)
- `GET /api/ai/analyze/status` — 분석 작업 상태
- `GET /api/ai/certificate/{fingerprint}` — 인증서 분석 결과
- `GET /api/ai/certificate/{fingerprint}/forensic` — 포렌식 상세 (10개 카테고리)
- `GET /api/ai/anomalies` — 이상 목록 (필터: country, type, label, risk_level, pagination)
- `GET /api/ai/statistics` — 전체 분석 통계
- `GET /api/ai/reports/country-maturity` — 국가 PKI 성숙도
- `GET /api/ai/reports/algorithm-trends` — 알고리즘 마이그레이션 트렌드
- `GET /api/ai/reports/key-size-distribution` — 키 크기 분포
- `GET /api/ai/reports/risk-distribution` — 위험 수준 분포
- `GET /api/ai/reports/country/{code}` — 국가별 상세 분석
- `GET /api/ai/reports/issuer-profiles` — 발급자 프로파일링
- `GET /api/ai/reports/forensic-summary` — 포렌식 분석 요약
- `GET /api/ai/reports/extension-anomalies` — 확장 규칙 위반 목록

모든 AI 엔드포인트는 Public (JWT 불필요).

---

## ML 모델

- **Isolation Forest + LOF**: 이중 이상 탐지 (45개 feature, 인증서 타입별 모델)
- **타입별 모델**: CSCA/DSC/DSC_NC/MLSC — 최적화된 contamination rate
- **MLSC**: 소규모 데이터셋(< 30) → MAD 기반 rule-based fallback

## 위험 스코어링

- **Composite risk**: 0~100 (6개 카테고리)
- **Forensic risk**: 0~100 (10개 카테고리: algorithm, key_size, compliance, validity, extensions, anomaly, issuer_reputation, structural_consistency, temporal_pattern, dn_consistency)
- 4단계 위험 수준: LOW / MEDIUM / HIGH / CRITICAL

## 코드 구조

```
app/
├── main.py           # FastAPI + APScheduler
├── config.py         # DB 설정 (PostgreSQL/Oracle 이중)
├── database.py       # SQLAlchemy async + sync 엔진
├── routers/          # analysis.py, reports.py
└── services/
    ├── analysis.py              # 배치 분석 파이프라인
    ├── feature_engineering.py   # 45개 ML feature 추출
    ├── risk_scorer.py           # 10개 카테고리 포렌식 스코어링
    ├── extension_rules_engine.py # ICAO Doc 9303 확장 규칙
    └── issuer_profiler.py       # DBSCAN 발급자 행동 분석
```

## 테스트

```bash
# 로컬 실행 (컨테이너 내 pytest도 설치됨)
cd services/ai-analysis && python3 -m pytest tests/ -v
# 201 tests passed
```
