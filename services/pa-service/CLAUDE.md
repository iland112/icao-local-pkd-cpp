# PA Service (Passive Authentication)

**Port**: 8082 (via API Gateway :8080/api/pa)
**Language**: C++20, Drogon Framework
**Role**: ICAO 9303 Part 10 & 11 Passive Authentication 검증

---

## API Endpoints

- `POST /api/pa/verify` — PA 검증 (8단계 전체 프로세스)
- `POST /api/pa/parse-sod` — SOD (Security Object Document) 파싱
- `POST /api/pa/parse-dg1` — DG1 (MRZ 데이터) 파싱
- `POST /api/pa/parse-dg2` — DG2 (얼굴 이미지) 파싱
- `POST /api/pa/parse-mrz-text` — MRZ 텍스트 파싱
- `GET /api/pa/history` — PA 검증 이력
- `GET /api/pa/statistics` — PA 통계
- `GET /api/pa/{id}` — 검증 상세
- `GET /api/pa/{id}/datagroups` — DataGroups 상세
- `POST /api/pa/trust-materials` — 클라이언트 PA용 Trust Materials (CSCA/CRL/LC DER Base64)
- `POST /api/pa/trust-materials/result` — 클라이언트 PA 결과 + 암호화 MRZ 보고
- `GET /api/pa/trust-materials/history` — 클라이언트 PA 요청 이력
- `GET /api/pa/combined-statistics` — 서버 PA + 클라이언트 PA 통합 통계

모든 PA 엔드포인트는 Public (JWT 불필요). API Key 사용량 추적은 nginx auth_request로 처리.

---

## PA 검증 8단계

1. SOD 파싱 (CMS SignerInfo, LDSSecurityObject)
2. DSC 추출 (SOD CMS SignerInfo의 서명 인증서)
3. CSCA 조회 (DSC issuer DN으로 LDAP 검색, 다중 CSCA 후보 지원)
4. Trust Chain 검증 (DSC → Link Certificate → Root CSCA, 순환 참조 감지)
5. CRL 체크 (폐기 여부 + CRL 만료 체크)
6. SOD 서명 검증 (CMS 서명 유효성)
7. DG 해시 검증 (LDSSecurityObject 해시 vs 실제 DG 해시)
8. DSC 자동 등록 (pending_dsc_registration → 관리자 승인 워크플로우)

## 이중 모드

- **서버 PA** (`POST /api/pa/verify`): SOD/DG 전송 → 서버에서 8단계 수행
- **클라이언트 PA** (`POST /api/pa/trust-materials` 시리즈): CSCA/CRL 다운로드 → 클라이언트 로컬 PA → 결과+MRZ 보고

## 코드 구조

```
src/
├── handlers/         # PaHandler (9개), HealthHandler, InfoHandler
├── repositories/     # PA Verification, LDAP Cert/CRL, DataGroup, TrustMaterialRequest
├── services/         # CertificateValidationService (8단계), DscAutoRegistration, TrustMaterial
├── adapters/         # LdapCscaProvider, LdapCrlProvider (icao::validation 인터페이스)
├── auth/             # AES-256-GCM PII 암호화 (MRZ, IP, User-Agent)
└── domain/models/    # CertificateChainValidation, PaVerification
```

## PII 암호화 (개인정보보호법 제29조)

- `pa_verification` 테이블 3개 PII 필드: document_number, client_ip, user_agent
- 여권번호: 고유식별정보 (법 제24조) → 저장 시 AES-256-GCM 암호화 필수
- `PII_ENCRYPTION_KEY` 환경변수 기반, 미설정 시 암호화 비활성화
