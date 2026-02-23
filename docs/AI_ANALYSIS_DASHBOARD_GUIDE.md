# AI 인증서 분석 보고서 가이드

**Version**: v2.20.2
**Created**: 2026-02-22
**페이지**: `/ai/analysis`

---

## 개요

AI 인증서 분석 보고서는 ICAO Local PKD에 등록된 전체 인증서(CSCA, DSC, MLSC, DSC_NC)를
ML(Machine Learning) 기반으로 분석하여 이상 징후, 보안 위험, PKI 성숙도를 시각화하는 대시보드입니다.

### 분석 엔진 구성

| 엔진 | 방식 | 역할 |
|------|------|------|
| **Isolation Forest** | 비지도학습 | 전체 인증서 대비 통계적 이탈도 측정 (글로벌 이상 탐지) |
| **Local Outlier Factor** | 비지도학습 | 같은 국가·유형 인증서 대비 이탈도 측정 (로컬 이상 탐지) |
| **규칙 기반 위험 점수** | 가중 합산 | 6개 카테고리별 보안 위험도 수치화 (0~100) |
| **포렌식 분석** | 10개 카테고리 | 심층 구조·발급자·시간패턴 분석 |

### ML 특징(Feature) 수

- **v1.0**: 25개 특징 (암호, 유효기간, 준수, 확장, 국가 상대값)
- **v2.0**: 45개 특징 (+발급자 프로파일, 시간 패턴, DN 구조, 확장 프로파일, 교차 인증서)

---

## 1. 요약 카드 (Summary Cards)

페이지 상단에 4개 카드로 전체 분석 결과를 요약합니다.

### 1.1 분석 완료 (Total Analyzed)

- **아이콘**: Brain (파란색)
- **값**: `total_analyzed`
- **의미**: ML 분석이 완료된 인증서 총 수
- **예시**: 31,212 — DB에 등록된 전체 인증서가 분석됨

### 1.2 정상 (Normal)

- **아이콘**: CheckCircle (녹색)
- **값**: `normal_count`
- **의미**: ML 이상 탐지 모델이 정상 패턴으로 분류한 인증서 수
- **판단 기준**: anomaly_score가 낮은 범위에 속하며, 같은 국가·유형 인증서들과 유사한 특징 보유

### 1.3 의심 (Suspicious)

- **아이콘**: AlertTriangle (주황색)
- **값**: `suspicious_count`
- **의미**: 일부 특징이 다수 인증서와 다르지만 명확한 이상은 아닌 경계 영역
- **조치**: 추가 검토 권장. 위험 요인(risk_factors)을 확인하여 실제 문제인지 판단

### 1.4 이상 (Anomalous)

- **아이콘**: ShieldAlert (빨간색)
- **값**: `anomalous_count`
- **의미**: ML 모델이 명확한 이상 패턴을 탐지한 인증서
- **조치**: 즉시 검토 필요. 해당 인증서의 포렌식 상세 분석 확인 권장

### 분류 기준

anomaly_score (0.0~1.0)는 Isolation Forest(전체 패턴 대비 이탈도)와 LOF(같은 국가·유형 대비 이탈도)의 가중 평균입니다. 인증서 유형별로 별도 모델이 학습되어 CSCA·DSC·MLSC·DSC_NC 각각의 정상 패턴을 기준으로 이상을 판단합니다.

---

## 2. 위험 수준 분포 바 (Risk Level Distribution)

전체 인증서의 위험 수준을 4단계로 분류하여 비례 막대로 표시합니다.

### 위험 수준 정의

| 수준 | 색상 | 점수 범위 | 의미 |
|------|------|-----------|------|
| **LOW** | 녹색 | 0~25 | 표준 준수, 최신 알고리즘, 적절한 키 크기. 정상 인증서 |
| **MEDIUM** | 노란색 | 25~50 | 일부 항목이 권장사항에 미달. 예: SHA-256 사용하지만 키 크기가 최소 기준 |
| **HIGH** | 주황색 | 50~75 | 다수 항목에서 문제 발견. 예: 약한 알고리즘, 만료된 인증서, ICAO 비준수 |
| **CRITICAL** | 빨간색 | 75~100 | 심각한 보안 위험. 예: SHA-1 + RSA-1024 + 다수 ICAO 위반 동시 발생 |

### 위험 점수(risk_score) 계산

6개 카테고리의 가중 합산으로 산출됩니다 (0~100):

| 카테고리 | 최대 점수 | 측정 내용 | 높은 점수 예시 |
|----------|-----------|-----------|----------------|
| **Algorithm** | 40점 | 서명 알고리즘 강도 | SHA-1, MD5 사용 |
| **Key Size** | 40점 | 공개키 크기 적절성 | RSA-1024, EC-160 |
| **Compliance** | 20점 | ICAO 9303 표준 준수 여부 | 다수 Doc 9303 항목 위반 |
| **Validity** | 15점 | 유효기간 상태 | 만료됨, 비정상 유효기간 |
| **Extensions** | 15점 | X.509 확장 필드 규칙 위반 | 필수 확장 누락, 금지 확장 포함 |
| **Anomaly** | 15점 | ML 이상 탐지 점수 반영 | anomaly_score가 높은 경우 |

> **해석 예시**: risk_score = 65
> - Algorithm 0점 (RSA-SHA256, 양호)
> - Key Size 20점 (RSA-2048, 최소 기준)
> - Compliance 10점 (ICAO 2개 항목 위반)
> - Validity 15점 (인증서 만료됨)
> - Extensions 10점 (확장 규칙 2개 위반)
> - Anomaly 10점 (ML 이상 탐지)

### 평균 위험 점수 (avg_risk_score)

상단 우측에 표시되는 전체 인증서의 평균 위험 점수입니다. 이 값이 낮을수록 전체 PKD의 보안 상태가 양호합니다.

---

## 3. 포렌식 분석 요약 (Forensic Risk Summary)

ML 이상 탐지를 넘어 **10개 카테고리**로 세분화된 심층 분석 결과입니다.

### 표시 항목

| 항목 | 의미 |
|------|------|
| **포렌식 수준 분포** | 포렌식 종합 점수 기준 LOW/MEDIUM/HIGH/CRITICAL 비례 막대 |
| **평균 포렌식 점수** | 전체 인증서의 포렌식 위험 평균 (0~100) |
| **주요 발견 사항** (Top 5) | 가장 빈번하게 발견된 문제와 발생 건수 |

### 포렌식 10개 카테고리 상세

| # | 카테고리 | 최대 점수 | 측정 내용 |
|---|----------|-----------|-----------|
| 1 | **서명 알고리즘** (Algorithm) | 40 | SHA-1, MD5 등 취약 알고리즘 사용 여부. SHA-256 이상이면 0점 |
| 2 | **키 크기** (Key Size) | 40 | ICAO 권장 최소 키 크기 미달 여부. RSA-2048+, ECDSA-256+ 이면 0점 |
| 3 | **ICAO 준수** (Compliance) | 20 | Doc 9303 표준 체크리스트 위반 수. 전체 통과시 0점 |
| 4 | **유효기간** (Validity) | 15 | 만료/미시작 상태, 비정상 유효기간 길이. 유효하면 0점 |
| 5 | **확장 필드** (Extensions) | 15 | Key Usage, Basic Constraints 등 필수 확장 규칙 위반. 전체 준수시 0점 |
| 6 | **ML 이상 탐지** (Anomaly) | 15 | Isolation Forest + LOF 모델의 이상 점수 반영 |
| 7 | **발급자 평판** (Issuer Reputation) | 15 | 해당 발급자의 전체 인증서 품질 이력. 다른 인증서에도 문제가 많으면 높은 점수 |
| 8 | **구조 일관성** (Structural Consistency) | 20 | X.509 확장 프로파일이 같은 유형·국가 인증서와 얼마나 다른지 |
| 9 | **시간 패턴** (Temporal Pattern) | 10 | 발급일·유효기간이 같은 국가의 일반적 패턴과 다른 정도 |
| 10 | **DN 일관성** (DN Consistency) | 10 | Subject/Issuer DN 구조가 같은 국가 인증서와 일관성 있는 정도 |

### 주요 발견 사항 (Top Findings) 해석

발견 사항은 전체 인증서에서 가장 많이 나타나는 문제를 순위별로 나열합니다.

| 예시 메시지 | 의미 |
|-------------|------|
| "RSA-1024 키 크기 사용 (권장: 2048+)" | 해당 인증서들이 ICAO 최소 권장 키 크기 미달 |
| "SHA-1 서명 알고리즘 사용" | 2011년 이후 사용 중단 권고된 취약 알고리즘 |
| "ICAO 9303 Basic Constraints 규칙 위반" | 필수 X.509 확장이 누락되었거나 값이 부적절 |
| "인증서 만료됨" | 유효기간이 종료된 인증서 |

---

## 4. 발급자 프로파일 카드 (Issuer Profile)

각 인증서 발급자(주로 CSCA)별 품질을 분석합니다. 상위 15개 발급자가 수평 막대 차트로 표시됩니다.

### 데이터 필드

| 필드 | 의미 |
|------|------|
| **발급자 DN** (`issuer_dn`) | 인증서를 발급한 CA의 Distinguished Name (예: `/C=KR/O=MOGAHA/CN=KOREA CSCA`) |
| **인증서 수** (`cert_count`) | 해당 발급자가 발급한 인증서 총 수. 막대 길이로 표현 |
| **유형 다양성** (`type_diversity`) | 발급한 인증서 유형의 다양도 (CSCA, DSC 등) |
| **주요 알고리즘** (`dominant_algorithm`) | 가장 많이 사용한 서명 알고리즘 |
| **평균 키 크기** (`avg_key_size`) | 발급 인증서들의 평균 공개키 크기 |
| **준수율** (`compliance_rate`) | ICAO 9303 표준을 통과한 인증서 비율 (0~1). 1.0 = 100% 준수 |
| **만료율** (`expired_rate`) | 만료된 인증서 비율 (0~1). 높으면 갱신 필요 |
| **위험 지표** (`risk_indicator`) | LOW(녹색) / MEDIUM(주황) / HIGH(빨간) 종합 판단 |
| **국가** (`country`) | 발급자 소속 국가 코드 |

### 위험 지표 판단 기준

| 지표 | 조건 |
|------|------|
| **HIGH** (빨간색) | 준수율 낮음 + 만료율 높음 + 취약 알고리즘 사용 |
| **MEDIUM** (주황색) | 일부 항목 미달 |
| **LOW** (녹색) | 준수율 높음 + 최신 알고리즘 + 유효한 인증서 |

> HIGH 위험 발급자가 있으면 해당 국가의 PKI 인프라에 구조적 문제가 있을 수 있습니다.
> 하단 테이블에서 HIGH 위험 발급자의 상세 정보(준수율, 만료율)를 확인할 수 있습니다.

---

## 5. 확장 프로파일 규칙 위반 (Extension Compliance Checklist)

ICAO Doc 9303에서 정의한 X.509 확장 필드 규칙 위반을 검사합니다.

### 테이블 컬럼

| 컬럼 | 의미 |
|------|------|
| **인증서** | SHA-256 fingerprint 앞 16자리 (행 클릭으로 상세 펼침) |
| **유형** | CSCA / DSC / MLSC — 인증서 유형에 따라 적용 규칙이 다름 |
| **국가** | 2자리 국가 코드 |
| **심각도** | structural_score 기반 자동 분류 |
| **위반 수** | 필수 누락 + 권장 누락 + 금지 위반 + 키 사용 위반의 합계 |
| **점수** | structural_score × 100 (0~100). 높을수록 많은 규칙 위반 |

### 심각도 분류

| 심각도 | 조건 | 색상 |
|--------|------|------|
| **CRITICAL** | structural_score ≥ 0.8 | 빨간색 |
| **HIGH** | structural_score ≥ 0.5 | 주황색 |
| **MEDIUM** | structural_score ≥ 0.2 | 노란색 |
| **LOW** | structural_score < 0.2 | 녹색 |

### 펼침 상세 내용

행을 클릭하면 해당 인증서의 위반 상세가 표시됩니다:

| 항목 | 색상 | 의미 |
|------|------|------|
| **violations_detail** | 심각도별 | 개별 위반 규칙명과 심각도 |
| **필수 누락** (`missing_required`) | 빨간색 | 반드시 있어야 하는 확장이 없음 |
| **금지 위반** (`forbidden_violations`) | 주황색 | 해당 유형에 있으면 안 되는 확장이 포함됨 |
| **키 사용 위반** (`key_usage_violations`) | 노란색 | Key Usage 비트가 인증서 유형에 맞지 않음 |

### 인증서 유형별 규칙 요약

| 유형 | Key Usage 필수 비트 | Basic Constraints | 기타 |
|------|---------------------|-------------------|------|
| **CSCA** | keyCertSign + cRLSign | CA=true, pathLen=0 | 자체서명 |
| **DSC** | digitalSignature | CA=false | EKU 없어야 함 |
| **MLSC** | (유형별 상이) | (유형별 상이) | EKU OID 2.23.136.1.1.3 필수 |

---

## 6. 국가별 PKI 성숙도 (Country PKI Maturity)

각 국가의 전체 PKI 인프라 품질을 0~100으로 점수화합니다. 상위 15개국이 수평 막대 차트로 표시됩니다.

### 성숙도 점수 구성

| 항목 | 의미 | 높은 점수 조건 |
|------|------|----------------|
| **maturity_score** | 5개 차원의 가중 평균 종합 점수 (0~100) | 모든 차원에서 우수 |
| **algorithm_score** | 서명 알고리즘 강도 | SHA-256+, ECDSA, RSA-PSS 비율이 높음 |
| **key_size_score** | 공개키 크기 적절성 | RSA-2048+ 또는 ECDSA-256+ 비율이 높음 |
| **compliance_score** | ICAO 9303 표준 준수율 | 준수 인증서 비율이 높음 |
| **extension_score** | X.509 확장 필드 규칙 준수율 | 확장 규칙 위반이 적음 |
| **freshness_score** | 인증서 신선도 | 최근 발급·갱신 비율이 높고 만료 인증서가 적음 |

### 차트 툴팁

막대 위에 마우스를 올리면 해당 국가의 상세 점수가 표시됩니다:
- 국기 + 국가 코드 + 국가명
- 5개 차원별 개별 점수
- 해당 국가의 인증서 수

### 해석 예시

| 국가 | 종합 | 알고리즘 | 키 크기 | 준수 | 확장 | 신선도 | 해석 |
|------|------|----------|---------|------|------|--------|------|
| KR 92 | 95 | 90 | 98 | 85 | 90 | 전반적으로 우수한 PKI |
| XX 45 | 30 | 40 | 60 | 50 | 40 | 알고리즘 갱신 및 표준 준수 개선 필요 |

---

## 7. 알고리즘 마이그레이션 추세 (Algorithm Migration Trends)

Stacked Area Chart로 연도별 서명 알고리즘 사용 추이를 보여줍니다.

- **X축**: 인증서 발급 연도
- **Y축**: 인증서 수
- **영역**: 알고리즘별 색상 구분

### 알고리즘 종류 및 보안 수준

| 차트 표시명 | OID 이름 | 보안 수준 | 비고 |
|------------|----------|-----------|------|
| RSA-SHA256 | sha256WithRSAEncryption | 양호 | 현재 표준 |
| RSA-SHA384 | sha384WithRSAEncryption | 우수 | |
| RSA-SHA512 | sha512WithRSAEncryption | 우수 | |
| RSA-SHA1 | sha1WithRSAEncryption | **취약** | 2011년 이후 사용 중단 권고 |
| ECDSA-SHA256 | ecdsa-with-SHA256 | 우수 | ICAO 권장 |
| ECDSA-SHA384 | ecdsa-with-SHA384 | 우수 | |
| ECDSA-SHA512 | ecdsa-with-SHA512 | 우수 | |
| RSA-PSS | id-RSASSA-PSS | 우수 | 최신 표준 |

### 정상적인 마이그레이션 패턴

최근 연도로 갈수록:
- RSA-SHA1 영역이 줄어듦 (취약 알고리즘 퇴출)
- ECDSA 영역이 늘어남 (타원곡선 암호 채택 증가)
- RSA-PSS가 새로 나타남 (최신 표준 도입)

> 특정 연도에 RSA-SHA1 비율이 갑자기 증가하면 해당 국가들의 인증서 갱신이 지연되고 있음을 의미합니다.

---

## 8. 키 크기 분포 (Key Size Distribution)

Pie Chart로 상위 10개 알고리즘+키크기 조합의 비율을 보여줍니다.

### 주요 조합 및 해석

| 조합 | 보안 수준 | ICAO 준수 | 비고 |
|------|-----------|-----------|------|
| RSA 2048 | 최소 기준 | 준수 | 가장 흔한 조합 |
| RSA 3072 | 양호 | 준수 | 2030년까지 권장 |
| RSA 4096 | 우수 | 준수 | 장기 보안 |
| EC 256 (P-256) | 우수 | 준수 | RSA 3072과 동등 보안 강도 |
| EC 384 (P-384) | 매우 우수 | 준수 | RSA 7680과 동등 |
| EC 521 (P-521) | 최고 수준 | 준수 | |
| RSA 1024 | **취약** | **미달** | 즉시 교체 필요 |
| EC 160 | **취약** | **미달** | 즉시 교체 필요 |

### ICAO 최소 권장 기준

| 알고리즘 | 최소 키 크기 |
|----------|-------------|
| RSA | 2048비트 |
| ECDSA | 256비트 (P-256) |
| RSA-PSS | 2048비트 |

> RSA 1024이나 EC 160이 파이 차트에 보이면 해당 국가의 인증서를 즉시 갱신해야 합니다.

---

## 9. 인증서 분석 테이블

필터링 + 페이지네이션이 적용된 개별 인증서 분석 결과 목록입니다.

### 테이블 컬럼

| 컬럼 | 데이터 | 의미 |
|------|--------|------|
| **국가** | `country_code` | 국기 + 2자리 코드. 인증서 발급 국가 |
| **유형** | `certificate_type` | CSCA(최상위CA), DSC(문서서명), MLSC(마스터리스트서명), DSC_NC(부적합DSC) |
| **이상 점수** | `anomaly_score` | 0.000~1.000. ML 모델이 판단한 이상 정도. **0에 가까울수록 정상** |
| **이상 수준** | `anomaly_label` | 정상(NORMAL) / 의심(SUSPICIOUS) / 이상(ANOMALOUS) |
| **위험 점수** | `risk_score` | 0.0~100.0. 6개 카테고리 가중 합산. **0에 가까울수록 안전** |
| **위험 수준** | `risk_level` | LOW / MEDIUM / HIGH / CRITICAL |
| **주요 위험 요인** | `risk_factors` (상위 3개) | 가장 높은 점수의 위험 카테고리와 점수 |
| **분석 일시** | `analyzed_at` | 해당 인증서가 마지막으로 분석된 날짜 |

### 필터 옵션

| 필터 | 선택지 | 용도 |
|------|--------|------|
| **국가** | 이상 인증서가 있는 국가 목록 | 특정 국가의 인증서만 확인 |
| **유형** | CSCA / DSC / DSC_NC / MLSC | 특정 인증서 유형만 확인 |
| **이상 수준** | NORMAL / SUSPICIOUS / ANOMALOUS | ML 이상 탐지 결과 필터 |
| **위험 수준** | LOW / MEDIUM / HIGH / CRITICAL | 규칙 기반 위험도 필터 |

### 페이지네이션

- 페이지당 15건 표시
- 하단에 "X / Y pages" 및 "showing X-Y of Z results" 표시
- 필터 변경 시 1페이지로 자동 이동

### CSV 내보내기

상단 버튼으로 전체 필터링 결과를 CSV로 다운로드할 수 있습니다.
- 파일명: `ai-analysis-report-YYYY-MM-DD.csv`
- 10개 컬럼: Country, Certificate Type, Anomaly Score, Anomaly Label, Risk Score, Risk Level, Top Risk Factors, Anomaly Explanations, Analyzed At, Fingerprint
- UTF-8 BOM 포함 (Excel 호환)

---

## 핵심 개념 비교

### anomaly_score vs. risk_score

| | anomaly_score | risk_score |
|--|---------------|------------|
| **범위** | 0.0~1.0 | 0~100 |
| **산출 방식** | ML 모델 (비지도학습) | 규칙 기반 가중 합산 |
| **의미** | "다른 인증서들과 얼마나 다른가" | "보안 위험이 얼마나 높은가" |
| **높은 값** | 통계적으로 특이한 인증서 | 실제 보안 위험이 있는 인증서 |

> **주의**: 두 점수는 항상 비례하지 않습니다.
> - 독특하지만 안전한 인증서: 높은 anomaly_score + 낮은 risk_score
> - SHA-1 + RSA-1024 인증서가 흔한 국가: 낮은 anomaly_score + 높은 risk_score

### anomaly_label 분류

| 수준 | 의미 | 조치 |
|------|------|------|
| **NORMAL** (정상) | 같은 유형·국가 인증서와 유사한 패턴 | 조치 불필요 |
| **SUSPICIOUS** (의심) | 일부 특징이 비정상적 | risk_factors 확인 후 판단 |
| **ANOMALOUS** (이상) | 명확한 이상 패턴 탐지 | 포렌식 상세 분석 + 해당 국가 PKI 검토 |

### risk_level 분류

| 수준 | 점수 | 의미 | 조치 |
|------|------|------|------|
| **LOW** | 0~25 | 보안 위험 없음 | 정상 운영 |
| **MEDIUM** | 25~50 | 일부 개선 필요 | 갱신 계획 수립 |
| **HIGH** | 50~75 | 다수 보안 문제 | 우선적 갱신 권고 |
| **CRITICAL** | 75~100 | 즉각 조치 필요 | 해당 인증서 사용 중단 검토 |

---

## 분석 실행

### 수동 실행

상단 "분석 실행" 버튼으로 전체 배치 분석을 수동 트리거할 수 있습니다.

- 실행 중에는 진행 바가 표시됩니다 (처리 인증서 수 / 전체 인증서 수)
- 3초 간격으로 진행 상태를 자동 폴링합니다
- 완료 시 모든 차트와 테이블이 자동 새로고침됩니다

### 자동 스케줄

APScheduler로 매일 지정된 시각에 자동 분석이 실행됩니다.

| 환경변수 | 기본값 | 설명 |
|----------|--------|------|
| `ANALYSIS_SCHEDULE_HOUR` | 3 | 자동 분석 실행 시각 (0~23시) |
| `ANALYSIS_ENABLED` | true | 자동 분석 활성화 여부 |

---

## API 엔드포인트 참조

| 엔드포인트 | 메서드 | 용도 |
|-----------|--------|------|
| `/api/ai/health` | GET | 서비스 상태 확인 |
| `/api/ai/analyze` | POST | 전체 배치 분석 실행 |
| `/api/ai/analyze/incremental` | POST | 증분 분석 (upload_id 기반) |
| `/api/ai/analyze/status` | GET | 분석 작업 상태 조회 |
| `/api/ai/certificate/{fingerprint}` | GET | 개별 인증서 분석 결과 |
| `/api/ai/certificate/{fingerprint}/forensic` | GET | 개별 인증서 포렌식 상세 (10개 카테고리) |
| `/api/ai/anomalies` | GET | 이상 인증서 목록 (필터, 페이지네이션) |
| `/api/ai/statistics` | GET | 전체 분석 통계 |
| `/api/ai/reports/country-maturity` | GET | 국가별 PKI 성숙도 순위 |
| `/api/ai/reports/algorithm-trends` | GET | 연도별 알고리즘 추이 |
| `/api/ai/reports/key-size-distribution` | GET | 키 크기 분포 |
| `/api/ai/reports/risk-distribution` | GET | 위험 수준 분포 |
| `/api/ai/reports/country/{code}` | GET | 국가별 상세 분석 |
| `/api/ai/reports/issuer-profiles` | GET | 발급자 프로파일 목록 |
| `/api/ai/reports/forensic-summary` | GET | 포렌식 분석 요약 |
| `/api/ai/reports/extension-anomalies` | GET | 확장 규칙 위반 목록 |

모든 AI 엔드포인트는 **Public** (JWT 불필요)입니다.
