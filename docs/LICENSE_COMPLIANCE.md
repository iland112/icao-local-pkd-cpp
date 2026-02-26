# 오픈소스 라이선스 준수 보고서 (License Compliance Report)

**Version**: 1.0.0
**Last Updated**: 2026-02-26
**대상**: 상용 배포 라이선스 검토
**프로젝트**: ICAO Local PKD v2.22.1

---

## 1. 개요

본 문서는 ICAO Local PKD 프로젝트의 상용(Commercial) 배포 시 사용되는 모든 오픈소스 및 서드파티 라이브러리의 라이선스 현황과 준수 사항을 정리한 것이다.

### 1.1 배포 구성 전제

| 항목 | 내용 |
|------|------|
| **데이터베이스** | 고객사 Oracle Enterprise DB 서버 사용 — 우리 제품에 DB 서버 미포함 |
| **LDAP** | 고객사 상용 LDAP 서버 사용 — 우리 제품에 LDAP 서버 미포함 |
| **Oracle Client** | Oracle Instant Client를 Docker 이미지에 포함하여 배포 (클라이언트만) |
| **LDAP Client** | libldap (OpenLDAP 클라이언트 라이브러리)를 Docker 이미지에 포함하여 배포 |
| **Python Oracle Driver** | `oracledb` Thin 모드 (순수 Python, Oracle Client 불필요) |
| **배포 형태** | Docker 컨테이너 이미지 |
| **개발/테스트 전용** | PostgreSQL, OpenLDAP 서버 — 상용 배포 미포함 |

### 1.2 위험도 총괄 요약

| 위험도 | 항목 수 | 해당 라이브러리 |
|--------|---------|---------------|
| **HIGH** | 0개 | — |
| **MEDIUM** | 2개 | Oracle Instant Client (OTN), psycopg2-binary (LGPL-3.0) |
| **LOW** | 1개 | Preline UI (MIT + Fair Use) |
| **NONE** | ~54개 | 전부 Permissive (MIT / BSD / Apache / ISC 등) |

**GPL/AGPL 라이선스 라이브러리: 0개** — 카피레프트 오염 위험 없음

---

## 2. C++ Backend 라이브러리 (4개 서비스 공통)

패키지 관리: vcpkg + system packages | 빌드: CMake 3.20+, C++20

### 2.1 핵심 라이브러리

| # | 라이브러리 | 라이선스 (SPDX) | 유형 | 상용 배포 | 주요 의무사항 |
|---|-----------|----------------|------|----------|-------------|
| 1 | Drogon | MIT | Permissive | **안전** | 저작권 고지 포함 |
| 2 | Trantor (Drogon 의존) | BSD-3-Clause | Permissive | **안전** | 저작권 고지 포함 |
| 3 | OpenSSL 3.0+ | Apache-2.0 | Permissive | **안전** | NOTICE 파일 포함, 변경 사항 명시 |
| 4 | libpq (PostgreSQL) | PostgreSQL License | Permissive | **안전** | 저작권 고지 포함 |
| 5 | **Oracle Instant Client** | **OTN License** | **Proprietary** | **조건부** | EULA에 Oracle 조항 필수 (§3 참조) |
| 6 | nlohmann/json | MIT | Permissive | **안전** | 저작권 고지 포함 |
| 7 | jsoncpp | MIT / Public Domain | Permissive | **안전** | 저작권 고지 포함 |
| 8 | spdlog | MIT | Permissive | **안전** | 저작권 고지 포함 |
| 9 | fmt | MIT (with fmt-exception) | Permissive | **안전** | 바이너리 배포 시 고지 면제 |
| 10 | libzip | BSD-3-Clause | Permissive | **안전** | 저작권 고지 포함 |
| 11 | zlib | Zlib | Permissive | **안전** | 저작 변경 시 명시 |
| 12 | OpenLDAP (libldap, liblber) | OLDAP-2.8 | Permissive | **안전** | 저작권 고지 + 라이선스 사본 포함 |
| 13 | libuuid | BSD-3-Clause | Permissive | **안전** | 저작권 고지 포함 |
| 14 | curl / libcurl | curl (MIT-derivative) | Permissive | **안전** | 저작권 고지 포함 |
| 15 | c-ares | MIT | Permissive | **안전** | 저작권 고지 포함 |
| 16 | brotli | MIT | Permissive | **안전** | 저작권 고지 포함 |
| 17 | OpenJPEG (libopenjp2) | BSD-2-Clause | Permissive | **안전** | 저작권 고지 포함 |
| 18 | libjpeg-turbo | IJG + BSD-3-Clause | Permissive | **안전** | IJG 귀속 문구 필수 (§4.1 참조) |

### 2.2 테스트 프레임워크 (운영 배포 미포함)

| # | 라이브러리 | 라이선스 (SPDX) | 비고 |
|---|-----------|----------------|------|
| 1 | Catch2 v3 | BSL-1.0 | 테스트 전용 — 바이너리 배포 시 귀속 불필요 |
| 2 | Google Test | BSD-3-Clause | 테스트 전용 |

---

## 3. Oracle Instant Client — 재배포 조건 (MEDIUM RISK)

### 3.1 라이선스 정보

| 항목 | 내용 |
|------|------|
| **라이선스명** | OTN Development and Distribution License |
| **유형** | Proprietary Freeware (오픈소스 아님) |
| **사용료** | 무료 (개발 + 운영 + 재배포) |
| **재배포** | 허용 (조건부) |

### 3.2 제품 EULA 필수 포함 조항

Oracle Instant Client를 Docker 이미지에 포함하여 배포할 경우, 제품의 최종사용자 라이선스 계약(EULA)에 아래 4개 조항을 **반드시** 포함해야 한다:

| # | 필수 EULA 조항 | 원문 요지 |
|---|---------------|----------|
| 1 | **사용 범위 제한** | 최종사용자의 비즈니스 운영(internal business operations) 목적으로만 사용 가능 |
| 2 | **재배포 금지** | 최종사용자가 Oracle 프로그램을 제3자에게 재배포할 수 없음 |
| 3 | **역공학 금지** | Oracle 프로그램의 역공학, 디컴파일, 디스어셈블리 금지 |
| 4 | **소유권 귀속** | Oracle 프로그램의 소유권(title)은 Oracle Corporation에 귀속 |

### 3.3 추가 제한사항

- GPL 라이선스 코드와 동일 바이너리에서 결합 불가 (본 프로젝트에 GPL 코드 없으므로 해당 없음)
- Oracle Instant Client 자체를 수정할 수 없음
- Oracle 상표를 제품 마케팅에 사용 불가

### 3.4 Python oracledb 드라이버

| 항목 | 내용 |
|------|------|
| **라이선스** | UPL-1.0 OR Apache-2.0 (이중 라이선스, 선택 가능) |
| **유형** | Permissive |
| **동작 모드** | Thin 모드 (순수 Python, Oracle Client 불필요) |
| **상용 배포** | **안전** — 추가 조건 없음 |

> Thin 모드 확인: AI Analysis Service에서 `oracledb.init_oracle_client()`를 호출하지 않으므로 Thin 모드로만 동작한다. OTN License 대상인 Oracle Instant Client를 사용하지 않는다.

---

## 4. 특별 주의 라이브러리

### 4.1 libjpeg-turbo — IJG 귀속 문구

PA Service에서 DG2 여권 얼굴 이미지의 JPEG2000→JPEG 변환에 사용된다.

**바이너리 배포 시 문서에 아래 문구를 포함해야 한다:**

> "This software is based in part on the work of the Independent JPEG Group."

### 4.2 psycopg2-binary — LGPL-3.0 (MEDIUM RISK)

| 항목 | 내용 |
|------|------|
| **라이선스** | LGPL-3.0-or-later (OpenSSL 예외 포함) |
| **유형** | Weak Copyleft |
| **사용 방식** | SQLAlchemy 표준 DB-API를 통한 간접 사용 |

#### 안전한 이유

1. Python의 `import psycopg2`는 **동적 링킹**에 해당하므로, 응용프로그램은 "Library를 사용하는 저작물(work that uses the Library)"이지 파생 저작물이 아니다
2. psycopg2 소스코드를 수정하지 않는다
3. psycopg2 원저자(Daniele Varrazzo)가 공식적으로 "DB-API 인터페이스를 통한 사용은 파생 저작물이 아님"을 확인한 바 있다
4. pip 설치 방식이므로 사용자가 다른 버전으로 교체 가능 (LGPL 재링킹 요건 자동 충족)

#### 준수 사항

| # | 의무사항 | 충족 여부 |
|---|---------|----------|
| 1 | LGPL-3.0 라이선스 전문을 THIRD-PARTY-NOTICES에 포함 | 필요 |
| 2 | psycopg2를 정적 링킹하지 않을 것 | pip 설치이므로 자동 충족 |
| 3 | 사용자가 psycopg2를 다른 버전으로 교체할 수 있을 것 | pip이므로 자동 충족 |

#### 대안

LGPL이 우려될 경우, 프로젝트에 이미 포함된 `asyncpg` (Apache-2.0)로 통합하여 psycopg2-binary를 제거할 수 있다.

### 4.3 Preline UI — Fair Use License (LOW RISK)

| 항목 | 내용 |
|------|------|
| **라이선스** | MIT + Preline UI Fair Use License (이중 라이선스) |
| **경쟁금지 조항** | Preline UI와 직접 경쟁하는 UI 컴포넌트 라이브러리 제작 금지 |
| **라이선스 해지** | 위반 시 저작자가 라이선스 철회 가능 |

#### 안전한 이유

ICAO Local PKD는 PKD 관리 시스템이지 UI 컴포넌트 라이브러리가 아니므로, Fair Use License가 명시적으로 허용하는 "End-User Products" 카테고리에 해당한다:

> "Applications, websites, or software that use Preline UI for their User Interface; e-commerce websites; custom dashboards for businesses; and SaaS platforms where Preline UI is simply part of the UI."

#### 주의사항

- 프로젝트 범위가 변경되어 Preline 컴포넌트를 재배포 가능한 UI 키트로 제공할 경우 Fair Use License 위반
- 비표준 SPDX 식별자이므로 자동 라이선스 스캐너에서 경고 발생 가능

---

## 5. Python AI Analysis Service 라이브러리

런타임: Python 3.12 (PSF-2.0, Permissive)

| # | 라이브러리 | 버전 | 라이선스 (SPDX) | 유형 | 상용 배포 |
|---|-----------|------|----------------|------|----------|
| 1 | FastAPI | 0.115.6 | MIT | Permissive | **안전** |
| 2 | Uvicorn | 0.34.0 | BSD-3-Clause | Permissive | **안전** |
| 3 | SQLAlchemy | 2.0.37 | MIT | Permissive | **안전** |
| 4 | asyncpg | 0.30.0 | Apache-2.0 | Permissive | **안전** |
| 5 | **psycopg2-binary** | **2.9.10** | **LGPL-3.0** | **Weak Copyleft** | **조건부** (§4.2 참조) |
| 6 | oracledb | 2.5.1 | UPL-1.0 OR Apache-2.0 | Permissive | **안전** (§3.4 참조) |
| 7 | scikit-learn | 1.6.1 | BSD-3-Clause | Permissive | **안전** |
| 8 | pandas | 2.2.3 | BSD-3-Clause | Permissive | **안전** |
| 9 | numpy | 2.2.2 | BSD-3-Clause | Permissive | **안전** |
| 10 | Pydantic | 2.10.5 | MIT | Permissive | **안전** |
| 11 | pydantic-settings | 2.7.1 | MIT | Permissive | **안전** |
| 12 | APScheduler | 3.11.0 | MIT | Permissive | **안전** |

---

## 6. Frontend 라이브러리

빌드: Vite 7, TypeScript 5.9, Tailwind CSS 4

### 6.1 Production Dependencies (운영 배포 포함)

| # | 패키지 | 버전 | 라이선스 (SPDX) | 유형 | 상용 배포 |
|---|--------|------|----------------|------|----------|
| 1 | React | ^19.2.0 | MIT | Permissive | **안전** |
| 2 | React DOM | ^19.2.0 | MIT | Permissive | **안전** |
| 3 | React Router | ^7.11.0 | MIT | Permissive | **안전** |
| 4 | Axios | ^1.13.2 | MIT | Permissive | **안전** |
| 5 | TanStack Query | ^5.90.15 | MIT | Permissive | **안전** |
| 6 | Zustand | ^5.0.9 | MIT | Permissive | **안전** |
| 7 | Recharts | ^3.6.0 | MIT | Permissive | **안전** |
| 8 | Lucide React | ^0.562.0 | ISC | Permissive | **안전** |
| 9 | react-arborist | ^3.4.3 | MIT | Permissive | **안전** |
| 10 | i18n-iso-countries | ^7.14.0 | MIT | Permissive | **안전** |
| 11 | **Preline** | **^3.2.3** | **MIT + Fair Use** | **Custom** | **조건부** (§4.3 참조) |

### 6.2 Dev Dependencies (운영 배포 미포함)

| # | 패키지 | 버전 | 라이선스 (SPDX) | 비고 |
|---|--------|------|----------------|------|
| 1 | Vite | ^7.2.4 | MIT | 빌드 도구 |
| 2 | @vitejs/plugin-react | ^5.1.1 | MIT | Vite 플러그인 |
| 3 | TypeScript | ~5.9.3 | Apache-2.0 | 컴파일러 (런타임 미포함) |
| 4 | Tailwind CSS | ^4.1.18 | MIT | CSS 생성기 (런타임 미포함) |
| 5 | ESLint | ^9.39.1 | MIT | 린터 |
| 6 | typescript-eslint | ^8.46.4 | MIT | ESLint 플러그인 |

---

## 7. Infrastructure 소프트웨어

### 7.0 배포 범위

상용 배포 시 DB 서버와 LDAP 서버는 고객사가 별도로 확보한다. 우리 제품에는 클라이언트 라이브러리만 포함된다.

| 구분 | 소프트웨어 | 배포 포함 | 비고 |
|------|-----------|----------|------|
| **배포 포함** | nginx, Swagger UI | O | Docker 컨테이너 이미지에 포함 |
| **클라이언트만 포함** | Oracle Instant Client, libldap | O | 서버 미포함, 클라이언트 라이브러리만 |
| **개발/테스트 전용** | PostgreSQL 15, OpenLDAP Server | X | 상용 배포 미포함 |
| **고객 인프라** | Oracle DB Enterprise, 상용 LDAP | X | 고객 책임 (EULA 제6조) |

### 7.1 배포 포함 소프트웨어

| # | 소프트웨어 | 라이선스 | 유형 | 비고 |
|---|-----------|---------|------|------|
| 1 | nginx (open source) | BSD-2-Clause | Permissive | nginx Plus 아님 |
| 2 | Swagger UI | Apache-2.0 | Permissive | SmartBear 상용 제품 아님 |
| 3 | Docker Engine | Apache-2.0 | Permissive | CE 버전, 고객 환경 설치 |
| 4 | Node.js | MIT | Permissive | 빌드 전용 |

### 7.2 개발/테스트 전용 (상용 배포 미포함)

| # | 소프트웨어 | 라이선스 | 유형 | 비고 |
|---|-----------|---------|------|------|
| 1 | PostgreSQL 15 | PostgreSQL License | Permissive | 개발 DB |
| 2 | OpenLDAP Server | OLDAP-2.8 | Permissive | 개발 LDAP |
| 3 | osixia/docker-openldap | MIT | Permissive | Docker 래퍼 |

### 7.1 Docker Desktop 참고사항

Docker Desktop은 Docker Engine과 별도 라이선스이다:

| 조건 | Docker Engine (서버) | Docker Desktop (개발 PC) |
|------|---------------------|------------------------|
| 라이선스 | Apache-2.0 (무료) | Proprietary (Freemium) |
| 무료 조건 | 제한 없음 | 직원 250명 미만 **AND** 매출 $10M 미만 |
| 유료 구간 | 해당 없음 | Pro $5/월, Team $9/월, Business $24/월 |

운영 서버(Linux)에는 Docker Engine만 설치하므로 라이선스 비용이 발생하지 않는다. 개발자 PC에서 Docker Desktop을 사용하는 경우 조직 규모에 따라 유료 구독이 필요할 수 있다.

---

## 8. 라이선스 유형별 분류

### 8.1 Permissive (허용적 라이선스) — 54개

소스 코드 공개 의무 없이 상용 제품에 자유롭게 포함 가능. 저작권 고지만 유지하면 된다.

| 라이선스 | 해당 라이브러리 수 |
|---------|-----------------|
| MIT | 28개 |
| BSD-3-Clause | 12개 |
| Apache-2.0 | 6개 |
| BSD-2-Clause | 2개 |
| ISC | 1개 |
| PostgreSQL License | 1개 |
| OLDAP-2.8 | 1개 |
| Zlib | 1개 |
| curl | 1개 |
| BSL-1.0 | 1개 (테스트 전용) |

### 8.2 Weak Copyleft (약한 카피레프트) — 1개

| 라이선스 | 라이브러리 | 영향 |
|---------|-----------|------|
| LGPL-3.0 | psycopg2-binary | 동적 링킹이므로 응용프로그램 소스 공개 의무 없음 |

### 8.3 Strong Copyleft (강한 카피레프트) — 0개

GPL, AGPL 라이선스 라이브러리 없음.

### 8.4 Proprietary (독점적) — 1개

| 라이선스 | 라이브러리 | 영향 |
|---------|-----------|------|
| OTN License | Oracle Instant Client | EULA에 4개 조항 포함 필수 (§3.2 참조) |

### 8.5 Custom (비표준) — 1개

| 라이선스 | 라이브러리 | 영향 |
|---------|-----------|------|
| MIT + Fair Use | Preline UI | 경쟁 UI 컴포넌트 라이브러리 제작 금지 |

---

## 9. 상용 배포 준수 체크리스트

### 9.1 필수 조치 (MUST)

| # | 조치 | 대상 | 상태 |
|---|------|------|------|
| 1 | THIRD-PARTY-NOTICES 파일 생성 | 전체 오픈소스 라이브러리 저작권 고지 + 라이선스 전문 | 미완료 |
| 2 | OpenSSL NOTICE 파일 포함 | Apache-2.0 요구사항 | 미완료 |
| 3 | IJG 귀속 문구 포함 | libjpeg-turbo (PA Service) | 미완료 |
| 4 | LGPL-3.0 라이선스 전문 포함 | psycopg2-binary | 미완료 |
| 5 | 제품 EULA에 Oracle 호환 조항 추가 | Oracle Instant Client (§3.2의 4개 조항) | **완료** (`docs/EULA.md` 제5조) |

### 9.2 권장 조치 (SHOULD)

| # | 조치 | 이유 |
|---|------|------|
| 6 | psycopg2-binary → asyncpg 통합 검토 | LGPL 우려 완전 해소 (Apache-2.0) |
| 7 | Preline Fair Use License 법률 검토 기록 | 라이선스 해지 조항 존재 |
| 8 | Docker Desktop 유료 구독 검토 | 직원 250명+ 또는 매출 $10M+ 조직 해당 시 |
| 9 | 라이선스 스캐너 CI 통합 | 신규 의존성 추가 시 자동 감지 |

---

## 10. 결론

### PostgreSQL 전용 배포 시

Oracle 관련 컴포넌트를 제외하면, 프로젝트의 **모든 라이브러리가 Permissive 라이선스**이다 (LGPL인 psycopg2 포함 — 동적 링킹으로 안전). **소스 코드 공개 의무 없이 상용 배포 가능**하며, THIRD-PARTY-NOTICES 파일 작성만으로 라이선스 준수가 완료된다.

### Oracle 포함 배포 시 (기본 배포 구성)

Oracle Instant Client(OTN License)의 재배포 조건을 제품 EULA에 반영 완료 (`docs/EULA.md` 제5조). Oracle DB 서버 및 상용 LDAP 서버는 고객사가 별도로 확보하므로 해당 서버의 라이선스는 고객사 책임이다 (`docs/EULA.md` 제6조).

### 위험 요약

- **HIGH RISK: 0개** — 상용 배포를 차단하는 라이선스 문제 없음
- **MEDIUM RISK: 2개** — EULA 조항 추가 및 라이선스 텍스트 포함으로 해결
- **GPL/AGPL 오염: 없음** — 전체 프로젝트에 카피레프트 라이선스 0개
