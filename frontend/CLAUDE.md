# Frontend

**Port**: 13080 (dev), 443 (production via API Gateway)
**Stack**: React 19, TypeScript, Vite, Tailwind CSS 4, React Router 7

---

## Pages (28개)

| Route | Page | 역할 |
|-------|------|------|
| `/login` | Login | 로그인 |
| `/` | Dashboard | 인증서 통계 홈 |
| `/upload` | FileUpload | LDIF/Master List 업로드 |
| `/upload/certificate` | CertificateUpload | 개별 인증서 업로드 (미리보기→저장) |
| `/upload-history` | UploadHistory | 업로드 이력 |
| `/upload/:uploadId` | UploadDetail | 업로드 상세 |
| `/upload-dashboard` | UploadDashboard | 인증서 통계 대시보드 |
| `/pkd/certificates` | CertificateSearch | 인증서 검색 & 상세 |
| `/pkd/dsc-nc` | DscNcReport | DSC_NC 부적합 보고서 |
| `/pkd/crl` | CrlReport | CRL 보고서 |
| `/pkd/trust-chain` | TrustChainValidationReport | DSC Trust Chain 보고서 |
| `/pa/verify` | PAVerify | PA 검증 수행 |
| `/pa/history` | PAHistory | PA 이력 (서버/클라이언트 탭) |
| `/pa/:paId` | PADetail | PA 상세 |
| `/pa/dashboard` | PADashboard | PA 통계 대시보드 |
| `/sync` | SyncDashboard | DB-LDAP 동기화 모니터링 |
| `/sync/icao-ldap` | IcaoLdapSync | ICAO PKD LDAP 자동 동기화 |
| `/icao` | IcaoStatus | ICAO PKD 버전 추적 |
| `/monitoring` | MonitoringDashboard | 시스템 모니터링 |
| `/ai/analysis` | AiAnalysisDashboard | AI 포렌식 분석 |
| `/admin/users` | UserManagement | 사용자 관리 |
| `/admin/audit-log` | AuditLog | 인증 감사 로그 |
| `/admin/operation-audit` | OperationAuditLog | 운영 감사 로그 |
| `/admin/api-clients` | ApiClientManagement | API 클라이언트 관리 |
| `/api-client-request` | ApiClientRequest | 외부 API 접근 요청 (public) |
| `/admin/pending-dsc` | PendingDscApproval | DSC 등록 승인 |
| `/admin/csr` | CsrManagement | CSR 생성 & 관리 |
| `/profile` | Profile | 사용자 프로필 |

## 핵심 컴포넌트

- **AdminRoute / PrivateRoute**: RBAC 라우트 가드
- **ErrorBoundary**: 전역 에러 바운더리
- **TreeViewer**: react-arborist 기반 트리 시각화
- **CertificateDetailDialog**: 인증서 상세 (4-tab: General/Details/Doc 9303/포렌식)
- **ValidationSummaryPanel**: SSE + post-upload 공유 검증 통계 카드
- **GlossaryTerm**: 전문 용어 hover 풍선 도움말 (21개 용어)
- **SortableHeader**: 클라이언트 사이드 테이블 정렬 (12개 페이지)

## 주요 의존성

React 19, TypeScript, Vite, Tailwind CSS 4, React Router 7, Axios, Zustand, TanStack Query, Recharts, Lucide Icons, i18n-iso-countries

## 테스트

```bash
cd frontend
npm test              # vitest run (98 files, 1485 tests)
npm run test:watch    # watch mode
npm run test:coverage # coverage report
```

## 디자인 시스템

상세: [docs/FRONTEND_DESIGN_SYSTEM.md](../docs/FRONTEND_DESIGN_SYSTEM.md)
- 패딩: 메인 카드 `p-4`, 테이블 행 `py-2`, 다이얼로그 `space-y-3`
- 폰트: 비표준 크기 사용 금지 → `text-xs` 통일
- 버튼: `rounded-lg` 통일
- 반응형: `grid-cols-1 sm:grid-cols-2 lg:grid-cols-4` 패턴
