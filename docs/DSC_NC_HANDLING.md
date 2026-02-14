# DSC_NC (Non-Conformant DSC) 처리 가이드

**작성일**: 2026-02-14
**버전**: v2.10.1

---

## 1. 개요

ICAO PKD는 Document Signer Certificate(DSC)를 **준수(Conformant)**와 **비준수(Non-Conformant)**로 분류한다. 비준수 DSC(DSC_NC)는 ICAO Doc 9303 기술 사양을 완전히 충족하지 않지만, 실제 여권 서명에 사용된 유효한 인증서이다.

### 핵심 사실

- DSC_NC는 기술적 비준수이지, 보안적으로 무효한 것이 아니다
- ICAO는 2021년에 nc-data 브랜치를 deprecated (신규 업로드 중단)
- 기존 ~502개 DSC_NC는 여전히 유효하며, 실제 유통 중인 여권에 사용됨
- Receiving State(수신국)가 nc-data 임포트 여부를 결정

---

## 2. ICAO PKD 컬렉션 구조

| 컬렉션 | 내용 | LDAP 경로 | 상태 |
|--------|------|-----------|------|
| icaopkd-001 | CSCA, DSC, CRL (준수) | `dc=data` | Active |
| icaopkd-002 | Master Lists | `dc=data` | Active |
| icaopkd-003 | DSC_NC (비준수) | `dc=nc-data` | Deprecated (2021) |

### LDAP DIT 구조

```
dc=download,dc=pkd,dc=ldap,dc=smartcoreinc,dc=com
├── dc=data                          ← icaopkd-001, 002
│   └── c={COUNTRY}
│       ├── o=csca   (CSCA 인증서)
│       ├── o=dsc    (준수 DSC)
│       ├── o=crl    (CRL)
│       ├── o=mlsc   (Master List Signer)
│       └── o=ml     (Master Lists)
└── dc=nc-data                       ← icaopkd-003 (deprecated)
    └── c={COUNTRY}
        └── o=dsc    (비준수 DSC_NC)
```

### DSC_NC LDAP 속성

| 속성 | 설명 | 예시 |
|------|------|------|
| `pkdConformanceCode` | 비준수 사유 코드 | `ERR:CSCA.CDP.14` |
| `pkdConformanceText` | 비준수 사유 설명 | `The Subject Public Key Info field does not contain an rsaEncryption OID` |
| `pkdVersion` | PKD 버전 | `90` |
| `userCertificate;binary` | 인증서 바이너리 (DER) | - |

---

## 3. 비준수 사유 분류

ICAO PKD가 정의하는 주요 비준수 사유:

| 코드 패턴 | 분류 | 설명 |
|-----------|------|------|
| `ERR:CSCA.*` | CSCA 관련 | CSCA 인증서 형식/알고리즘 비준수 |
| `ERR:DSC.*` | DSC 자체 | DSC 인증서 형식/알고리즘 비준수 |
| `ERR:*.CDP.*` | CRL 배포점 | CRL Distribution Point 관련 이슈 |
| `ERR:*.SIG.*` | 서명 알고리즘 | 서명 알고리즘이 ICAO 권장 목록에 없음 |
| `ERR:*.KEY.*` | 공개키 | 공개키 형식/크기 비준수 |

---

## 4. 다른 NPKD 구현체의 처리 방식

### Keyfactor NPKD (상용 솔루션)

- **conformant + non-conformant 모두 다운로드**
- Receiving State가 nc-data 임포트 여부를 결정하도록 설계
- 인증서 분류 표시: "Conformance" 상태 필드 제공

### German BSI (연방정보보안청)

- CSCA/DSC/CRL만 필수 다운로드
- nc-data는 선택적 임포트
- PA 검증 시 nc-data 포함 여부는 정책 기반

### 일반 권장 사항 (ICAO 커뮤니티)

- DSC_NC는 실제 여권에 사용된 인증서이므로 **수집은 권장**
- PA 검증 시 DSC_NC도 신뢰 체인 검증 대상에 포함해야 함
- 비준수 상태는 정보성으로 표시 (검증 통과/실패와 별개)

---

## 5. 현 시스템의 DSC_NC 처리 아키텍처

### 5.1 데이터 저장

| 저장소 | 준수 DSC | 비준수 DSC_NC |
|--------|---------|--------------|
| DB `certificate` 테이블 | `certificate_type = 'DSC'` | `certificate_type = 'DSC_NC'` |
| LDAP `dc=data` | `o=dsc,c={country},dc=data,...` | - |
| LDAP `dc=nc-data` | - | `o=dsc,c={country},dc=nc-data,...` |

- DB 복합 유니크 인덱스: `(certificate_type, fingerprint_sha256)` → 동일 fingerprint로 DSC와 DSC_NC 별도 저장 가능
- LDAP은 DIT 경로로 자연스럽게 분리

### 5.2 PA Lookup (PKD Management - DB 기반)

```
POST /api/certificates/pa-lookup
→ ValidationRepository.findBySubjectDn() / findByFingerprint()
  → DB 조회 (DSC, DSC_NC 모두 포함)
  → certificateType이 "DSC_NC"이면 enrichWithConformanceData() 호출
    → LDAP nc-data에서 pkdConformanceCode/Text/Version 조회
  → 응답에 conformance 정보 포함
```

### 5.3 PA Verify (PA Service - LDAP 기반)

```
POST /api/pa/verify
→ SOD에서 DSC 추출 (LDAP 조회 아님)
→ CSCA 조회: dc=data (o=csca + o=lc) ← CSCAs는 항상 dc=data에만 존재
→ Trust Chain 검증 (DSC 서명 → CSCA 공개키)
→ CRL 검사: dc=data (o=crl)
→ DSC Conformance 체크: checkDscConformance()
  → DSC fingerprint 계산 (SHA-256)
  → LDAP nc-data에서 해당 fingerprint 검색
  → 존재하면 dscNonConformant=true + conformance 데이터
→ 응답에 conformance 정보 포함 (certificateChainValidation 내)
```

### 5.4 DSC LDAP 검색 (fallback)

```
LdapCertificateRepository.findDscBySubjectDn()
→ dc=data 검색 (conformant DSC)
→ 없으면 dc=nc-data 검색 (non-conformant DSC_NC)
→ isNonConformant 출력 파라미터로 구분
```

---

## 6. 중요 설계 결정

### Q: PA Verify에서 DSC_NC가 검증에 실패하면?

**A**: DSC가 non-conformant라는 것은 ICAO 기술 사양 비준수를 의미하지, 인증서 자체가 무효함을 의미하지 않는다. PA 검증 결과(VALID/INVALID)는 trust chain, 서명 검증, CRL 상태에 따라 결정되며, conformance 상태와 독립적이다.

### Q: dc=data와 dc=nc-data에 같은 DSC가 존재할 수 있는가?

**A**: ICAO PKD 운영상 동일 인증서가 001(conformant)과 003(non-conformant)에 동시 등록되지 않는다. 단, PA auto-registration(`PA_EXTRACTED` source)으로 SOD에서 추출한 DSC가 DB에 별도 등록될 수 있으며, 이 경우 DB에는 같은 fingerprint로 DSC(PA_EXTRACTED)와 DSC_NC(LDIF_PARSED)가 공존한다.

### Q: Reconciliation(DB-LDAP 동기화)에서 DSC_NC는?

**A**: ICAO가 2021년에 nc-data를 deprecated했으므로, Reconciliation 범위에서 DSC_NC는 제외한다. DSC_NC의 LDAP 동기화 상태(`stored_in_ldap`)는 업로드 시 설정되며, 이후 변경하지 않는다.

---

## 7. 운영 데이터 현황

| 항목 | 수량 |
|------|------|
| 전체 DSC_NC | 502 |
| DSC_NC 국가 수 | ~30개국 |
| LDAP nc-data 저장 | 502 (100%) |
| DB certificate 저장 | 502 (certificate_type='DSC_NC') |

---

## 8. 관련 파일

### Backend (PA Service)
- `services/pa-service/src/repositories/ldap_certificate_repository.h` - `checkDscConformance()`, `DscConformanceInfo`
- `services/pa-service/src/repositories/ldap_certificate_repository.cpp` - nc-data LDAP 조회 구현
- `services/pa-service/src/domain/models/certificate_chain_validation.h` - `dscNonConformant`, `pkdConformanceCode/Text`
- `services/pa-service/src/services/certificate_validation_service.cpp` - conformance 체크 호출

### Backend (PKD Management)
- `services/pkd-management/src/repositories/validation_repository.h` - `enrichWithConformanceData()`
- `services/pkd-management/src/repositories/validation_repository.cpp` - PA Lookup conformance LDAP 보조 조회

### Frontend
- `frontend/src/pages/PAVerify.tsx` - PA Verify/Lookup 결과에 Non-Conformant 경고 표시

### 참고 문서
- ICAO Doc 9303 Part 12: Public Key Infrastructure (PKI)
- RFC 5280: Internet X.509 PKI Certificate and CRL Profile
- ICAO PKD Participant Guide (2021): nc-data deprecation notice
