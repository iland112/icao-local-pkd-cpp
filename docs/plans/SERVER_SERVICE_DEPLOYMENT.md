# 서버별 서비스 배치 계획

> **작성일**: 2026-03-22
> **기준**: 법무부 전자여권 시스템 사업계획서 H/W 5대 + S/W 구성
> **아키텍처**: FASTpass® SPKD v2.39.0 마이크로서비스

---

## 1. 배치 개요

```
┌─────────────────────────────────────────────────────────────────────┐
│                         내부망                                       │
│                                                                     │
│  ┌──────────────────────┐  ┌──────────────────────┐                │
│  │   PKD서버 #1 (Active) │  │  PKD서버 #2 (Standby) │                │
│  │   16Core / 32GB       │  │  16Core / 32GB        │                │
│  │                       │  │                       │                │
│  │  PKD Management :8081 │  │  PKD Management (DR)  │                │
│  │  PA Service     :8082 │  │  PA Service (DR)      │                │
│  │  Frontend       :80   │  │  Frontend (DR)        │                │
│  │  OpenLDAP Primary:389 │  │  OpenLDAP Replica:389 │                │
│  │  API Gateway   :443   │  │  API Gateway (DR)     │                │
│  └───────────┬───────────┘  └───────────┬───────────┘                │
│              │ 10GbE                     │ 10GbE                     │
│              └───────────┬───────────────┘                           │
│                          │                                           │
│  ┌───────────────────────┴───────────────────────┐                  │
│  │              관리서버 (DB 전용)                  │                  │
│  │              16Core / 32GB                      │                  │
│  │                                                 │                  │
│  │  Oracle RDBMS (8 Core, Named 200 Users)         │                  │
│  │  Monitoring Service :8084                       │                  │
│  │  Chakra MAX (DB 접근제어)                        │                  │
│  │  SecuveTOS (서버보안)                             │                  │
│  │  Sycros v3 (서버모니터링)                         │                  │
│  └───────────────────────┬───────────────────────┘                  │
│                          │ 32Gb FC                                    │
│  ┌───────────────────────┴───────────────────────┐                  │
│  │  SAN 스위치 ×2 (Fabric A/B 이중화)               │                  │
│  │  └── 스토리지: NVMe 7.68TB ×28, 150.42TiB      │                  │
│  └───────────────────────────────────────────────┘                  │
│                                                                     │
├─────────────────────── 방화벽 ───────────────────────────────────────┤
│                         DMZ                                          │
│                                                                     │
│  ┌──────────────────────┐  ┌──────────────────────┐                │
│  │ 중계서버 #1 (Active)   │  │ 중계서버 #2 (Standby) │                │
│  │ 16Core / 32GB         │  │ 16Core / 32GB        │                │
│  │                       │  │                       │                │
│  │ PKD Relay      :8083  │  │ PKD Relay (DR)       │                │
│  │ ├ ICAO LDAP 자동동기화 │  │                       │                │
│  │ ├ DB↔LDAP Reconcile  │  │                       │                │
│  │ ├ ICAO 버전 모니터링   │  │                       │                │
│  │ └ CSR TLS 연결        │  │                       │                │
│  └───────────┬───────────┘  └───────────────────────┘                │
│              │                                                       │
├──────────────┼─────────── 방화벽 ────────────────────────────────────┤
│              │           외부망                                       │
│              ▼                                                       │
│  ┌───────────────────────┐  ┌───────────────────────┐               │
│  │ ICAO PKD              │  │ 외교부                  │               │
│  │ pkddownloadsg.icao.int│  │ 양자 협정 경로           │               │
│  │ LDAP V3 / TLS :636    │  │ CSCA 인증서 수신        │               │
│  │ CSR 인증서 기반 인증    │  │                        │               │
│  └───────────────────────┘  └───────────────────────┘               │
└─────────────────────────────────────────────────────────────────────┘
```

---

## 2. 서버별 상세 배치

### 2.1 PKD서버 #1 (Active)

| 항목 | 값 |
|------|---|
| **사양** | CPU 3.2GHz 16Core · Memory 32GB · DISK 960GB SSD ×2 · NIC 10GbE 2Port |
| **역할** | 핵심 업무 서비스 (인증서 관리 + PA 검증 + 웹 UI) |
| **네트워크** | 내부망 |

| 서비스 | 포트 | 기능 | 예상 메모리 |
|--------|------|------|----------|
| **API Gateway** (nginx) | :80/:443 | SSL/TLS, 리버스 프록시, Rate Limiting, CSP | 0.5GB |
| **PKD Management** | :8081 | 인증서 업로드/파싱/검증, CSR 관리, 통계/감사 | 4GB |
| **PA Service** | :8082 | ICAO 9303 PA 검증 (8단계), Client PA Trust Materials | 4GB |
| **Frontend** (React 19) | :3080 | 관리 웹 UI (28개 페이지) | 0.5GB |
| **OpenLDAP Primary** | :389 | LDAP 쓰기 노드, DIT 구조 (31K+ 엔트리) | 2GB |
| | | **합계** | **~11GB** (여유 21GB) |

### 2.2 PKD서버 #2 (Standby/DR)

| 항목 | 값 |
|------|---|
| **사양** | CPU 3.2GHz 16Core · Memory 32GB · DISK 960GB SSD ×2 · NIC 10GbE 2Port |
| **역할** | Active-Standby DR + LDAP 읽기 분산 |
| **네트워크** | 내부망 |

| 서비스 | 포트 | 기능 | 예상 메모리 |
|--------|------|------|----------|
| **API Gateway** (DR) | :80/:443 | Standby (장애 시 전환) | 0.5GB |
| **PKD Management** (DR) | :8081 | Standby | 4GB |
| **PA Service** (DR) | :8082 | Standby (Active-Active 가능) | 4GB |
| **Frontend** (DR) | :3080 | Standby | 0.5GB |
| **OpenLDAP Secondary** | :389 | MMR 복제, 읽기 분산 | 2GB |
| | | **합계** | **~11GB** (여유 21GB) |

### 2.3 관리서버 (DB + 보안)

| 항목 | 값 |
|------|---|
| **사양** | CPU 3.2GHz 16Core · Memory 32GB · DISK 960GB SSD ×2 · NIC 10GbE 2Port |
| **역할** | Oracle DB 전용 + 보안/모니터링 S/W |
| **네트워크** | 내부망 |

| 서비스/S/W | 포트 | 기능 | 예상 메모리 |
|-----------|------|------|----------|
| **Oracle RDBMS** | :1521 | 8 Core, Named 200 Users, SAN 스토리지 연결 | 16GB |
| **Monitoring Service** | :8084 | 시스템 메트릭, 서비스 헬스체크 | 0.5GB |
| **Chakra MAX** | — | DB 접근제어 (7~8 Core) | 2GB |
| **SecuveTOS V5.0** | — | 서버보안 (CC인증 EAL2 이상) | 0.5GB |
| **Sycros v3 Agent** | — | 서버모니터링, 감사로그, 위협 탐지 | 0.5GB |
| | | **합계** | **~20GB** (여유 12GB) |

### 2.4 중계서버 #1 (Active)

| 항목 | 값 |
|------|---|
| **사양** | CPU 3.2GHz 16Core · Memory 32GB · DISK 960GB SSD ×2 · NIC 10GbE 2Port |
| **역할** | ICAO PKD 연계 + DB↔LDAP 동기화 |
| **네트워크** | DMZ (내부망 ↔ 외부망 브릿지) |

| 서비스 | 포트 | 기능 | 예상 메모리 |
|--------|------|------|----------|
| **PKD Relay** | :8083 | ICAO PKD LDAP V3 자동 동기화 (TLS/SASL EXTERNAL) | 2GB |
| | | DB↔LDAP Reconciliation (불일치 감지/자동 조정) | |
| | | ICAO PKD 버전 모니터링 (매일 자동 체크) | |
| | | CSR 기반 TLS 클라이언트 인증서 연결 | |
| | | SSE 실시간 알림 (동기화 진행 상황) | |
| | | DSC 만료 상태 재검증 | |
| **SecuveTOS V5.0** | — | 서버보안 | 0.5GB |
| **Sycros v3 Agent** | — | 서버모니터링 | 0.5GB |
| | | **합계** | **~3GB** (여유 29GB) |

### 2.5 중계서버 #2 (Standby/DR)

| 항목 | 값 |
|------|---|
| **사양** | CPU 3.2GHz 16Core · Memory 32GB · DISK 960GB SSD ×2 · NIC 10GbE 2Port |
| **역할** | Standby DR |
| **네트워크** | DMZ |

| 서비스 | 포트 | 기능 | 예상 메모리 |
|--------|------|------|----------|
| **PKD Relay** (DR) | :8083 | Standby (장애 시 전환) | 2GB |
| **SecuveTOS V5.0** | — | 서버보안 | 0.5GB |
| **Sycros v3 Agent** | — | 서버모니터링 | 0.5GB |
| | | **합계** | **~3GB** (여유 29GB) |

---

## 3. 스토리지 + 네트워크

### 3.1 스토리지

| 항목 | 사양 |
|------|------|
| **DISK** | NVMe SSD 7.68TB ×28EA |
| **RAID** | RAID6 |
| **Usable** | 150.42 TiB |
| **HBA** | 32G 4p HBA ×4ea |
| **보증** | 3y TCE DMR |

| 용도 | 예상 용량 |
|------|---------|
| Oracle 데이터파일 (인증서 31K+, PA 이력, 감사 로그) | ~50GB |
| LDAP 데이터 (31K+ 엔트리, MMR 복제) | ~5GB |
| PA 검증 바이너리 아카이브 (SOD/DG 원본, 법적 보존) | ~TBD (장기 운영) |
| 백업 + 로그 아카이브 (일일 백업, 감사 로그 장기 보관) | ~100GB+ |
| **여유** | **150+ TiB** |

### 3.2 SAN 스위치

| 항목 | 사양 | 수량 |
|------|------|------|
| SAN 스위치 | 24port 32Gb FC Switch | 2식 (Fabric A/B 이중화) |

### 3.3 랙 및 KVM

| 항목 | 사양 | 수량 |
|------|------|------|
| 랙 | Deep Rack | 2식 |
| KVM | 16port KVM 지원 | 2식 |

---

## 4. 사업계획서 S/W 매핑

| 사업계획서 S/W | 수량 | FASTpass® SPKD 대응 서비스 | 배치 위치 |
|-------------|------|------------------------|---------|
| **PKD 중계 에이전트** | 2식 | PKD Relay Service (:8083) | 중계서버 #1, #2 |
| **PKD Manager** | 1식 | PKD Management (:8081) | PKD서버 #1 |
| **ICAO PKI Toolkit** | 1식 (1,500대) | PA Service (:8082) + Client PA Trust Materials API | PKD서버 #1 |
| **LDAP Server** | 2식 (3,000 Entry) | OpenLDAP MMR (Primary + Secondary) | PKD서버 #1, #2 |
| **Oracle DBMS** | 1식 (8Core, 200 Users) | Oracle RDBMS | 관리서버 |
| **SecuveTOS V5.0** | 5식 | 서버보안 (CC인증 EAL2+) | 전체 5대 서버 |
| **Sycros v3 Agent** | 5식 | 서버모니터링 + Monitoring Service | 전체 5대 서버 |
| **Chakra MAX** | 1식 (7~8 Core) | DB 접근제어 | 관리서버 |

---

## 5. 이중화 및 장애 대응

### HA 전환 시나리오

| 장애 발생 | 전환 방식 | 영향 범위 |
|---------|---------|---------|
| PKD서버 #1 장애 | PKD서버 #2 Active 전환 | 관리 UI + PA 검증 |
| | OpenLDAP Secondary → Primary 승격 | LDAP 쓰기 |
| | API Gateway DR 활성화 | 외부 접근 |
| 중계서버 #1 장애 | 중계서버 #2 Active 전환 | ICAO PKD 연계 일시 중단 |
| | 수동 업로드(PKD Management)로 운영 가능 | |
| 관리서버(Oracle) 장애 | 전체 서비스 영향 | DB 의존 서비스 전체 |
| | SAN 스토리지 데이터 보존 | 데이터 손실 없음 |
| 스토리지 장애 | RAID6 (디스크 2개 동시 장애 허용) | |
| SAN 스위치 장애 | Fabric A/B 이중화 자동 전환 | |

### 메모리 요약

| 서버 | 사용 | 여유 | 활용률 |
|------|------|------|-------|
| PKD서버 #1 | ~11GB | 21GB | 34% |
| PKD서버 #2 | ~11GB | 21GB | 34% |
| 관리서버 | ~20GB | 12GB | 63% |
| 중계서버 #1 | ~3GB | 29GB | 9% |
| 중계서버 #2 | ~3GB | 29GB | 9% |
| **합계** | **~48GB** | **112GB** | **30%** |

---

## 6. 네트워크 통신 흐름

```
ICRM 심사단말 (1,500대)
    │
    │ HTTPS (X-API-Key 인증)
    ▼
PKD서버 #1 — API Gateway (:443)
    │
    ├──→ PKD Management (:8081) ──→ Oracle RDBMS (:1521)
    │      인증서 관리, 업로드               │
    │                                    SAN FC 32Gb
    ├──→ PA Service (:8082) ──→ OpenLDAP (:389)
    │      PA 검증, Trust Materials        │
    │                                    스토리지 150TiB
    └──→ Frontend (:3080)
           관리 웹 UI

    ────────── 방화벽 (DMZ) ──────────

중계서버 #1 — PKD Relay (:8083)
    │
    ├──→ Oracle RDBMS (:1521)    DB↔LDAP 동기화
    ├──→ OpenLDAP (:389)         Reconciliation
    │
    │ LDAPS (:636) / TLS 상호 인증
    ▼
ICAO PKD (pkddownloadsg.icao.int)
    CSCA/DSC/CRL/ML 자동 다운로드
```
