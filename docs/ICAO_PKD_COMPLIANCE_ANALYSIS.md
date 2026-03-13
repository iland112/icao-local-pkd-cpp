# ICAO PKD 인증서 Doc 9303 준수 현황 분석

**작성일**: 2026-03-13
**버전**: 1.0
**분석 대상**: ICAO PKD Download collection-003 (31,212 인증서)

---

## 1. 개요

ICAO PKD에서 공식 배포하는 인증서들을 업로드 후 Doc 9303 Part 12 기술 규격 준수 여부를 검사한 결과, 상당수 인증서에서 미준수 항목이 발견되었다. 이 문서는 그 원인을 분석하고 운영 시 고려사항을 정리한다.

## 2. 분석 결과 요약

| 검증 카테고리 | 위반 유형 | 심각도 | 주요 원인 |
|---|---|---|---|
| 알고리즘 | SHA-1 해시 알고리즘 사용 | HIGH | 2010년 이전 발급 인증서 |
| 키 크기 | RSA 1024/1536비트 | HIGH | 초기 ePassport 시기 인증서 |
| Key Usage | digitalSignature 누락(CSCA) | MEDIUM | Doc 9303 해석 차이 |
| Basic Constraints | 확장 필드 누락 | MEDIUM | 일부 국가 PKI 구현 차이 |
| 유효기간 | CSCA 15년/DSC 3년 초과 | LOW | SHOULD 권고 미준수 |
| DN 형식 | Country(C) 속성 누락 | LOW | 초기 인증서 형식 |

## 3. SHALL vs SHOULD 구분

Doc 9303 Part 12는 RFC 2119 용어를 사용하여 요구사항 수준을 구분한다:

### 3.1 SHALL (필수) 위반 — 보안 위험

- **SHA-1 해시 알고리즘**: Doc 9303은 SHA-224 이상을 **SHALL** 로 요구. SHA-1은 충돌 공격 가능성이 입증되어 보안 위험 존재
- **RSA 1024비트 키**: 최소 2048비트 **SHALL** 요구. 1024비트는 현재 기술로 인수분해 가능
- **Key Usage 확장**: CSCA는 `keyCertSign` + `cRLSign`, DSC는 `digitalSignature` **SHALL** 포함

### 3.2 SHOULD (권고) 미준수 — 운영 참고

- **CSCA 유효기간 15년 초과**: Doc 9303 Section 7.1.1에서 SHOULD로 권고. 일부 국가는 20~30년 유효기간 사용
- **DSC 유효기간 3년 초과**: Section 7.1.2에서 SHOULD로 권고. 실제 5~10년 사용 사례 존재
- **Basic Constraints 확장**: CSCA에서 CA=TRUE, pathLenConstraint=0이 SHOULD. 일부 초기 인증서에서 누락

## 4. ICAO PKD의 역할과 한계

### 4.1 PKD는 배포 플랫폼

ICAO PKD는 각국이 제출한 인증서를 **있는 그대로 배포**하는 플랫폼이다. PKD가 인증서의 기술 준수 여부를 검증하여 거부하지 않는다.

> "The PKD is a distribution mechanism, not a regulatory enforcement tool."

### 4.2 dc=data vs dc=nc-data

- **dc=data**: 정규 인증서 저장소. 기본적으로 적합한 인증서로 분류되나, 기술 세부사항에서 Doc 9303 권고와 차이가 있을 수 있음
- **dc=nc-data**: ICAO가 **명시적으로** 표준 미준수로 분류한 DSC 인증서 (502건). `pkdConformanceCode`로 사유 기록

### 4.3 2021년 정책 변경

ICAO는 2021년부터 nc-data 분류를 폐기하는 방향으로 정책 변경:
- PKD를 규제 도구가 아닌 배포 플랫폼으로 재정의
- 인증서 적합성 판단은 **검증 시스템(Inspection System)**의 책임으로 이관
- 실제로 nc-data에 새로운 인증서가 추가되지 않고 있음

## 5. 미준수 발생 원인

### 5.1 레거시 인증서

ePassport 시스템은 2004년부터 운영되었으나, Doc 9303 Part 12의 기술 프로파일은 이후 여러 차례 개정되었다. 초기 발급 인증서는 당시 기준에는 적합했으나 현재 기준에서는 미준수로 판정될 수 있다.

- **SHA-1 인증서**: 2015년 이전 발급된 CSCA/DSC에서 주로 발견
- **RSA 1024비트**: 2010년 이전 발급 인증서
- **RSA 1536비트**: 과도기(2008-2012) 인증서

### 5.2 국가별 PKI 구현 차이

195+ 국가가 독립적으로 PKI를 운영하며, Doc 9303 해석과 구현에 차이가 존재한다:

- **Key Usage 해석**: 일부 국가는 CSCA에 `keyCertSign`만 설정하고 `cRLSign` 누락
- **Basic Constraints**: DSC에 확장 자체를 포함하지 않는 국가 존재 (Doc 9303은 CA=FALSE 권고)
- **유효기간**: 국가 보안 정책에 따라 ICAO 권고보다 긴 유효기간 채택

### 5.3 SHOULD 해석의 유연성

Doc 9303의 SHOULD 항목은 강제가 아닌 권고이므로, 미준수가 곧 "잘못된 인증서"를 의미하지 않는다. 각국의 보안 정책과 운영 환경에 따라 합리적인 편차가 존재한다.

## 6. 운영 시 권장사항

### 6.1 검증 시스템에서의 처리

| 위반 유형 | 권장 처리 |
|---|---|
| SHA-1 알고리즘 | WARNING — 보안 위험 경고, 거부하지는 않음 |
| RSA < 2048비트 | WARNING — 키 강도 부족 경고 |
| Key Usage 누락 | WARNING — 확장 필드 미비 |
| 유효기간 초과 | INFO — 참고 정보 (SHOULD 항목) |
| Basic Constraints 누락 | INFO — 참고 정보 |

### 6.2 Passive Authentication에서의 영향

- SHA-1/RSA-1024 인증서라도 PA 검증 자체는 가능 (서명 알고리즘만 지원하면 됨)
- PA 검증 결과에 알고리즘 강도 경고를 추가 정보로 포함
- 최종 판단은 검증 시스템 운영 정책에 따름

## 7. 참고 자료

- ICAO Doc 9303 Part 12 — PKI for Machine Readable Travel Documents
- ICAO PKD Board Technical Committee — PKD Data Classification Policy (2021)
- RFC 2119 — Key words for use in RFCs to Indicate Requirement Levels
- ZeroPass Non-Conformancy Report — ICAO PKD DSC Analysis

---

## 부록: 검증 카테고리별 세부 체크 항목

### A. 알고리즘 검증
- 서명 해시 알고리즘: SHA-224, SHA-256, SHA-384, SHA-512 (ICAO 승인)
- 공개키 알고리즘: RSA, ECDSA, RSA-PSS (ICAO 승인)

### B. 키 크기 검증
- RSA: 최소 2048비트 (SHALL), 최대 4096비트 (권고)
- ECDSA: 최소 224비트, 승인 곡선 (P-256, P-384, P-521, brainpool)

### C. Key Usage 검증
- CSCA: keyCertSign + cRLSign (SHALL)
- DSC: digitalSignature (SHALL)
- MLSC: digitalSignature (SHALL)

### D. 확장 필드 검증
- Basic Constraints 존재 여부
- Key Usage 확장 존재 여부
- CSCA: CA=TRUE (SHOULD)
- DSC: CA=FALSE (SHOULD)

### E. 유효기간 검증
- CSCA: 15년 이하 (SHOULD)
- DSC: 3년 이하 (SHOULD)

### F. DN 형식 검증
- Subject DN 존재 여부
- Country(C) 속성 포함 여부
