# FASTpass® SPKD — Frontend Guide

**Version**: v2.37.0 | **Last Updated**: 2026-03-18
**Tech Stack**: React 19, TypeScript, Tailwind CSS 4, Vite, Lucide Icons, Recharts

---

## Part 1: Design System

---

### 1. Design Concept

#### Brand Identity

FASTpass® SPKD(SmartCore PKD)는 전자여권 위·변조 검사 시스템으로, **엔터프라이즈급 보안 소프트웨어**에 적합한 디자인을 지향한다.

| 항목 | 값 |
|------|----|
| 로고 아이콘 | Shield + "F" + Speed Lines 커스텀 SVG (`favicon.svg`) |
| 브랜드 단색 | `bg-[#02385e]` (FASTpass® SPKD Navy) |
| 시스템 명 | FASTpass® SPKD (SmartCore PKD) |
| 부제 | 전자여권 위·변조 검사 시스템 |
| 표준 참조 | ICAO Doc 9303, RFC 5280, RFC 5652 |

> **v2.30.0 변경**: 기존 `ShieldCheck` 아이콘 + `from-blue-600 to-indigo-600` 그래디언트에서 커스텀 방패 SVG + 단색 `#02385e` 버튼으로 전환. 로그인 페이지 모던 리디자인 (카드 래퍼 제거, flat layout, `radial-gradient` 도트 패턴 배경).

#### Design Principles

1. **Enterprise Clarity** — 복잡한 인증서 데이터를 명확하게 전달
2. **Consistent Tokens** — 동일 역할의 요소는 동일 스타일 적용
3. **Dark Mode First** — 모든 색상에 light/dark 변형 제공
4. **Responsive Grid** — Mobile-first, `md`/`lg`/`xl` 반응형 브레이크포인트
5. **Minimal Decoration** — 기능 중심 UI, 불필요한 장식 배제

---

### 2. Color Theme

#### 2.1 Neutral Palette

| 용도 | Light | Dark |
|------|-------|------|
| 페이지 배경 | `bg-gray-50` | `dark:bg-gray-900` |
| 카드 배경 | `bg-white` | `dark:bg-gray-800` |
| 1차 텍스트 | `text-gray-900` | `dark:text-white` |
| 2차 텍스트 | `text-gray-700` | `dark:text-gray-300` |
| 3차 텍스트 | `text-gray-600` | `dark:text-gray-400` |
| 보조 텍스트 | `text-gray-500` | `dark:text-gray-400` |
| 아이콘 (기본) | `text-gray-400` | `dark:text-gray-500` |
| 테두리 | `border-gray-200` | `dark:border-gray-700` |
| 호버 배경 | `hover:bg-gray-100` | `dark:hover:bg-gray-700` |

#### 2.2 Functional Area Gradients (Icon Badge)

페이지 헤더 아이콘 뱃지에 적용하는 기능별 그래디언트:

| 영역 | 그래디언트 | 적용 페이지 |
|------|-----------|------------|
| PKD 관리 | `from-indigo-500 to-purple-600` | Dashboard, Upload, UploadHistory |
| 인증서 검색 | `from-blue-500 to-indigo-600` | CertificateSearch |
| 보고서 (DSC_NC) | `from-teal-500 to-cyan-600` | DscNcReport |
| 보고서 (CRL) | `from-amber-500 to-orange-600` | CrlReport |
| 보고서 (Trust Chain) | `from-emerald-500 to-teal-600` | TrustChainValidationReport |
| PA 서비스 | `from-teal-500 to-cyan-600` | PAVerify, PAHistory, PADashboard |
| AI 분석 | `from-purple-500 to-violet-600` | AiAnalysisDashboard |
| 시스템 모니터링 | `from-blue-500 to-cyan-600` | MonitoringDashboard, IcaoStatus |
| 동기화 | `from-orange-500 to-rose-600` | SyncDashboard |
| 관리자 | `from-blue-500 to-indigo-600` | UserManagement, AuditLog, OperationAuditLog |

#### 2.3 Status Colors

| 상태 | 배경 (Light) | 배경 (Dark) | 텍스트 (Light) | 텍스트 (Dark) |
|------|-------------|-------------|---------------|---------------|
| Success | `bg-green-50` | `dark:bg-green-900/30` | `text-green-700` | `dark:text-green-300` |
| Error | `bg-red-50` | `dark:bg-red-900/30` | `text-red-700` | `dark:text-red-300` |
| Warning | `bg-amber-50` | `dark:bg-amber-900/20` | `text-amber-700` | `dark:text-amber-300` |
| Info | `bg-blue-50` | `dark:bg-blue-900/20` | `text-blue-700` | `dark:text-blue-300` |

#### 2.4 Certificate Type Badge Colors

| 타입 | 배경 | 텍스트 |
|------|------|--------|
| CSCA | `bg-blue-50 dark:bg-blue-900/20` | `text-blue-700 dark:text-blue-300` |
| DSC | `bg-green-50 dark:bg-green-900/20` | `text-green-700 dark:text-green-300` |
| MLSC | `bg-purple-50 dark:bg-purple-900/20` | `text-purple-700 dark:text-purple-300` |
| DSC_NC | `bg-orange-50 dark:bg-orange-900/20` | `text-orange-700 dark:text-orange-300` |
| CRL | `bg-amber-50 dark:bg-amber-900/20` | `text-amber-700 dark:text-amber-300` |

#### 2.5 Risk Level Colors (AI Analysis)

| 레벨 | 배경 | 텍스트 |
|------|------|--------|
| LOW | `bg-green-500` | `text-white` |
| MEDIUM | `bg-yellow-500` | `text-white` |
| HIGH | `bg-orange-500` | `text-white` |
| CRITICAL | `bg-red-500` | `text-white` |

#### 2.6 Validation Status Colors

| 상태 | 색상 |
|------|------|
| VALID | `green-500` |
| EXPIRED_VALID | `amber-500` |
| EXPIRED | `red-500` |
| INVALID | `red-600` |
| PENDING | `gray-400` |
| NOT_YET_VALID | `yellow-500` |

---

### 3. Typography

#### 3.1 Heading Hierarchy

| 레벨 | 클래스 | 용도 |
|------|--------|------|
| H1 | `text-2xl font-bold text-gray-900 dark:text-white` | 페이지 제목 |
| H2 | `text-xl font-bold text-gray-900 dark:text-white` | 섹션 제목 |
| H3 | `text-lg font-semibold text-gray-900 dark:text-white` | 카드 제목 |
| H4 | `text-sm font-semibold text-gray-700 dark:text-gray-300` | 서브섹션 |

#### 3.2 Body Text

| 용도 | 클래스 |
|------|--------|
| 본문 | `text-sm text-gray-700 dark:text-gray-300` |
| 부제 / 설명 | `text-sm text-gray-500 dark:text-gray-400` |
| 소형 캡션 | `text-xs text-gray-500 dark:text-gray-400` |
| 강조 | `text-base font-medium text-gray-900 dark:text-white` |
| 모노스페이스 | `font-mono text-sm` (해시, 시리얼 번호) |

#### 3.3 Labels

| 용도 | 클래스 |
|------|--------|
| Form label | `block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1` |
| Section header (xs) | `text-xs font-semibold uppercase tracking-wider text-gray-400` |

---

### 4. Layout

#### 4.1 Page Structure

```
┌──────────────────────────────────────────────────┐
│ Sidebar (w-64 / w-[70px] collapsed)              │
│ ┌──────────────────────────────────────────────┐ │
│ │ Logo + System Name                           │ │
│ │ Navigation Groups                            │ │
│ │   Section Header (uppercase)                 │ │
│ │   NavItem / NavGroupItem (collapsible)       │ │
│ │   ...                                        │ │
│ │ Dark Mode Toggle (footer)                    │ │
│ └──────────────────────────────────────────────┘ │
│                                                  │
│ Content Area (lg:ps-64 / lg:ps-[70px])           │
│ ┌──────────────────────────────────────────────┐ │
│ │ Header Bar                                   │ │
│ │ ┌──────────────────────────────────────────┐ │ │
│ │ │ Page Content                             │ │ │
│ │ │   w-full px-4 lg:px-6 py-4 space-y-6    │ │ │
│ │ │                                          │ │ │
│ │ │   [Page Header — Icon Badge]             │ │ │
│ │ │   [Stats Cards Grid]                     │ │ │
│ │ │   [Filter Card]                          │ │ │
│ │ │   [Content Cards / Tables / Charts]      │ │ │
│ │ └──────────────────────────────────────────┘ │ │
│ │ Footer                                       │ │
│ └──────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────┘
```

#### 4.2 Page Wrapper

```
w-full px-4 lg:px-6 py-4 space-y-6
```

#### 4.3 Sidebar

| 요소 | 클래스 |
|------|--------|
| 컨테이너 | `bg-white dark:bg-gray-800 border-e border-gray-200 dark:border-gray-700` |
| 너비 (확장) | `w-64` |
| 너비 (축소) | `w-[70px]` |
| 로고 아이콘 | `w-10 h-10 bg-gradient-to-br from-blue-600 to-indigo-600 rounded-xl shadow-lg` |
| 활성 링크 | `bg-blue-50 dark:bg-blue-900/20 text-blue-600 dark:text-blue-400 border-l-2 border-blue-600` |
| 비활성 링크 | `text-gray-700 dark:text-gray-300 hover:bg-gray-100 dark:hover:bg-gray-700` |
| 섹션 헤더 | `px-3 pt-4 pb-2 text-xs font-semibold uppercase tracking-wider text-gray-400` |
| 하위 메뉴 | `ml-4 pl-3 border-l border-gray-200 dark:border-gray-700` |

#### 4.4 Responsive Breakpoints

| Prefix | 너비 | 용도 |
|--------|------|------|
| (base) | 0px+ | 모바일 (1열 그리드) |
| `md:` | 768px+ | 태블릿 (2열 그리드) |
| `lg:` | 1024px+ | 데스크톱 (3열+, 사이드바 표시) |
| `xl:` | 1280px+ | 와이드 (3열 확장) |

---

### 5. Components

#### 5.1 Page Header (Icon Badge Pattern)

모든 페이지에 적용되는 표준 헤더:

```
┌─────────────────────────────────────────────┐
│ [Icon Badge]  Title                         │
│               Subtitle           [Actions]  │
└─────────────────────────────────────────────┘
```

```
컨테이너:  mb-6
아이콘 뱃지: p-3 rounded-xl bg-gradient-to-br from-{C1}-500 to-{C2}-600 shadow-lg
아이콘 크기: w-7 h-7 text-white
제목:      text-2xl font-bold text-gray-900 dark:text-white
부제:      text-sm text-gray-500 dark:text-gray-400
```

#### 5.2 Cards

**Standard Card**:
```
bg-white dark:bg-gray-800 rounded-2xl shadow-lg
border border-gray-200 dark:border-gray-700  (optional)
overflow-hidden  (table cards)
```

**Stats Card (border-l-4)**:
```
bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-{color}-500
```

**Filter Card**:
```
bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5
```

**Table Card**:
```
bg-white dark:bg-gray-800 rounded-2xl shadow-lg overflow-hidden
```

#### 5.3 Dialogs / Modals

```
Backdrop:   fixed inset-0 bg-black/50 backdrop-blur-sm z-50
Container:  flex items-center justify-center p-4
Content:    bg-white dark:bg-gray-800 rounded-2xl shadow-xl max-w-{size} w-full
Header:     px-5 py-4 border-b border-gray-200 dark:border-gray-700
Body:       px-5 py-4
Footer:     px-5 py-3 border-t border-gray-200 dark:border-gray-700
```

**Size Variants:**

| 크기 | 클래스 | 용도 |
|------|--------|------|
| Small | `max-w-md` | 확인 다이얼로그, 비밀번호 변경 |
| Medium | `max-w-2xl` | 일반 폼 |
| Large | `max-w-3xl` | 넓은 폼 (사용자 추가) |
| XLarge | `max-w-4xl` | 상세 보기 |

#### 5.4 Buttons

**Primary (Gradient)**:
```
bg-gradient-to-r from-blue-500 to-indigo-500
hover:from-blue-600 hover:to-indigo-600
text-white rounded-xl shadow-md hover:shadow-lg
transition-all
disabled:opacity-50 disabled:cursor-not-allowed
```

**Secondary**:
```
text-gray-700 dark:text-gray-300
hover:bg-gray-100 dark:hover:bg-gray-700
rounded-xl transition-colors
```

**Danger**:
```
bg-red-600 hover:bg-red-700
text-white rounded-xl transition-colors
```

**Icon-only (Action)**:
```
p-2 rounded-lg hover:bg-{color}-50 dark:hover:bg-{color}-900/30
text-{color}-600 dark:text-{color}-400 transition-colors
```

**Export (Green)**:
```
bg-green-600 hover:bg-green-700
text-white rounded-lg text-sm font-medium
flex items-center gap-2 transition-colors
```

#### 5.5 Form Inputs

**Standard Input**:
```
w-full px-3 py-2 text-sm
border border-gray-300 dark:border-gray-600
rounded-xl
bg-white dark:bg-gray-700
text-gray-900 dark:text-white
focus:outline-none focus:ring-2 focus:ring-blue-500
```

**Select Dropdown**:
```
w-full px-3 py-2 text-sm
border border-gray-200 dark:border-gray-600
rounded-lg
bg-white dark:bg-gray-700
text-gray-900 dark:text-white
focus:outline-none focus:ring-2 focus:ring-blue-500
```

#### 5.6 Tables

```
Table Container:  rounded-2xl shadow-lg overflow-hidden
Header Row:       bg-gray-50 dark:bg-gray-900/50 border-b border-gray-200 dark:border-gray-700
Header Cell:      px-6 py-3 text-xs font-medium text-gray-500 dark:text-gray-400 uppercase
Body Row:         hover:bg-gray-50 dark:hover:bg-gray-900/30 transition-colors
Body Cell:        px-6 py-4 text-sm text-gray-600 dark:text-gray-400
Divider:          divide-y divide-gray-200 dark:divide-gray-700
```

#### 5.7 Status Badges / Pills

**기본 뱃지**:
```
inline-flex items-center gap-1
px-2 py-0.5
bg-{color}-100 dark:bg-{color}-900/30
text-{color}-700 dark:text-{color}-300
text-xs font-medium rounded-full
```

#### 5.8 Alerts / Notifications

**Success**:
```
bg-green-50 dark:bg-green-900/30
border border-green-200 dark:border-green-800
rounded-xl p-4 flex items-center gap-2
```

**Error**:
```
bg-red-50 dark:bg-red-900/30
border border-red-200 dark:border-red-800
rounded-xl p-4 flex items-center gap-2
```

**Warning Banner (ICAO Update)**:
```
bg-gradient-to-r from-amber-50 to-orange-50
dark:from-amber-900/20 dark:to-orange-900/20
border border-amber-200 dark:border-amber-700
rounded-2xl shadow-lg px-6 py-4
```

#### 5.9 Loading States

**Spinner**: `animate-spin w-8 h-8 border-2 border-blue-500 border-t-transparent rounded-full`

**Skeleton**: `bg-gray-200 dark:bg-gray-700 animate-pulse rounded`

#### 5.10 Pagination

```
flex items-center justify-center gap-2
Active:     bg-blue-50 dark:bg-blue-900/20 text-blue-600 dark:text-blue-400 font-medium
Inactive:   text-gray-600 dark:text-gray-400 hover:bg-gray-100 dark:hover:bg-gray-700
Disabled:   text-gray-300 dark:text-gray-600 cursor-not-allowed
```

#### 5.11 GlossaryTerm (전문 용어 도움말)

전문 용어(CSCA, DSC, CRL 등) 옆에 `?` 아이콘을 표시하고 hover 시 풍선 도움말을 제공하는 컴포넌트.

```tsx
import { GlossaryTerm, getGlossaryTooltip } from '@/components/common';

// 정적 라벨 (카드, 섹션 헤더)
<GlossaryTerm term="CSCA" className="text-xs text-blue-700" />

// 테이블 셀/동적 영역 (title 속성 방식)
<span title={getGlossaryTooltip("DSC")}>{certType}</span>
```

지원 용어 (21개): CSCA, DSC, DSC_NC, MLSC, CRL, SOD, DG1, DG2, MRZ, PA, BAC, PKD, LDIF, LDAP, Trust Chain, CSR, Link Certificate, Self-signed, Master List, ML

#### 5.12 CountryFlag (국기 + 국가명 툴팁)

```tsx
import { CountryFlag } from '@/components/common';

<CountryFlag code="KR" />           // KR (hover: "KR — Korea, Republic of")
<CountryFlag code="KR" size="md" /> // 큰 국기
<CountryFlag code="KR" showCode={false} /> // 국기만
```

---

### 6. Spacing System

#### 6.1 Padding

| 토큰 | 값 | 용도 |
|------|----|------|
| `p-2` | 8px | 아이콘 버튼, 컴팩트 요소 |
| `p-3` | 12px | 아이콘 뱃지, 컴팩트 카드 |
| `p-4` | 16px | 검색 바, 알림 |
| `p-5` | 20px | 필터 카드, 통계 카드 |
| `p-6` | 24px | 대형 카드 본문 |

#### 6.2 Gap

| 토큰 | 값 | 용도 |
|------|----|------|
| `gap-1` | 4px | 뱃지 내부 아이콘-텍스트 |
| `gap-2` | 8px | 버튼 그룹, 뱃지 목록 |
| `gap-3` | 12px | 폼 필드 그리드 |
| `gap-4` | 16px | 카드 그리드 |
| `gap-6` | 24px | 페이지 레벨 간격 |

---

### 7. Border Radius & Shadow

| 토큰 | 값 | 용도 |
|------|----|------|
| `rounded-lg` | 8px | 입력 필드, 작은 UI |
| `rounded-xl` | 12px | 아이콘 뱃지, 버튼, 폼 입력 |
| `rounded-2xl` | 16px | 카드, 다이얼로그 |
| `rounded-full` | 100% | 뱃지/필, 아바타 |

| 토큰 | 용도 |
|------|------|
| `shadow-lg` | 표준 카드, 아이콘 뱃지 |
| `shadow-xl` | 다이얼로그 |
| `shadow-md` | 호버 버튼 |

---

### 8. Animation & Transitions

| 패턴 | 용도 |
|------|------|
| `transition-colors` | 버튼, 링크 호버 |
| `transition-all` | 그래디언트 버튼, 카드 호버 |
| `transition-all duration-300` | 사이드바 확장/축소 |
| `animate-spin` | 로딩 스피너 |
| `animate-pulse` | 스켈레톤 로딩 |
| `backdrop-blur-sm` | 모달 배경 |

---

### 9. Landing Page (Login.tsx)

#### 9.1 Hero Panel (좌측 68%)

```
bg-gradient-to-br from-blue-600 via-indigo-600 to-purple-700
```

**Glassmorphism 카드**: `bg-white/5 backdrop-blur-sm rounded-xl border border-white/10 hover:bg-white/10`

**통계 카드**: `bg-white/10 backdrop-blur-sm rounded-xl border border-white/15 hover:bg-white/15`

#### 9.2 Login Panel (우측)

```
배경:  bg-gray-50 dark:bg-gray-900
카드:  bg-white dark:bg-gray-800 rounded-2xl shadow-xl border
버튼:  bg-gradient-to-r from-blue-500 to-cyan-500 shadow-lg shadow-blue-500/25
```

#### 9.3 Animation

```css
@keyframes slideInUp {
  from { opacity: 0; transform: translateY(20px); }
  to   { opacity: 1; transform: translateY(0); }
}
```

Stagger delay: 0.1s -> 0.2s -> 0.3s -> 0.4s

---

### 10. Dark Mode

- Zustand `themeStore`로 상태 관리 (`localStorage` persist)
- `document.documentElement.classList.add/remove('dark')`
- Tailwind CSS `darkMode: 'class'` 전략

모든 색상 속성에 `dark:` 변형을 반드시 제공:

```
bg-white dark:bg-gray-800
text-gray-900 dark:text-white
border-gray-200 dark:border-gray-700
```

---

### 11. Z-Index Stack

| 값 | 용도 |
|----|------|
| `z-10` | 테이블 sticky header |
| `z-40` | 페이지 헤더 바 (sticky) |
| `z-50` | 모달 다이얼로그 |
| `z-60` | 사이드바 |

---

### 12. Icon System

- **라이브러리**: Lucide React (`lucide-react`)
- **페이지 아이콘 크기**: `w-7 h-7` (헤더 뱃지 내)
- **카드 아이콘 크기**: `w-5 h-5`
- **인라인 아이콘**: `w-4 h-4`
- **뱃지 아이콘**: `w-3 h-3`

---

### 13. Grid Patterns (Responsive)

| 용도 | 클래스 |
|------|--------|
| 통계 카드 | `grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4` |
| 사용자 카드 | `grid grid-cols-1 md:grid-cols-2 xl:grid-cols-3 gap-4` |
| 폼 필드 | `grid grid-cols-1 md:grid-cols-2 gap-3` |
| 필터 | `grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-3` |

---

### 14. Chart Styling (Recharts)

```
컨테이너: rounded-lg bg-gray-50 dark:bg-gray-800 p-4 border border-gray-200 dark:border-gray-700
툴팁:    bg-white dark:bg-gray-800 rounded-lg shadow-lg border
텍스트:   text-gray-900 dark:text-white
```

---

### 15. File Structure

```
frontend/src/
├── App.tsx                    # Layout (Sidebar + Header + Content)
├── pages/                     # 29 페이지
│   ├── Login.tsx              # Landing + Login
│   ├── Dashboard.tsx          # 홈 대시보드
│   ├── UserManagement.tsx     # 사용자 관리 (grid 카드)
│   ├── AuditLog.tsx           # 인증 감사 로그
│   ├── OperationAuditLog.tsx  # 운영 감사 로그
│   ├── CertificateSearch.tsx  # 인증서 검색
│   ├── DscNcReport.tsx        # DSC_NC 보고서
│   ├── CrlReport.tsx          # CRL 보고서
│   └── ...
├── components/                # 공유 컴포넌트
│   ├── ErrorBoundary.tsx
│   ├── TreeViewer.tsx
│   ├── CertificateDetailDialog.tsx
│   ├── CertificateSearchFilters.tsx
│   ├── Doc9303ComplianceChecklist.tsx
│   ├── QuickLookupPanel.tsx
│   ├── ValidationSummaryPanel.tsx
│   └── ...
├── services/                  # API 모듈
│   ├── api.ts                 # 메인 API (PKD Management)
│   ├── authApi.ts             # 인증 API
│   ├── paApi.ts               # PA Service API
│   ├── monitoringApi.ts       # 모니터링 API
│   ├── aiAnalysisApi.ts       # AI 분석 API
│   └── csvExport.ts           # CSV 내보내기 유틸리티
└── stores/
    └── themeStore.ts          # 다크 모드 상태
```

---

## Part 2: Page Documentation

---

**총 페이지 수**: 29개

---

### PKD 관리

#### 1. 대시보드 (Dashboard)

| 항목 | 내용 |
|------|------|
| **라우트** | `/` |
| **파일** | `frontend/src/pages/Dashboard.tsx` |
| **인증** | 불필요 (공개) |

시스템 홈 페이지로, 전체 PKD 시스템 현황을 한 눈에 파악할 수 있다.

- **실시간 시계**: 1초 간격 갱신, 현재 날짜/시간 표시
- **연결 상태 표시기**: DB(PostgreSQL/Oracle) 및 LDAP 연결 상태를 실시간 확인
- **ICAO PKD 업데이트 알림 배너**: ICAO 공식 PKD에 새 버전이 감지되면 상단에 알림 표시
- **인증서 통계 카드**: CSCA, DSC, CRL, Master List 총 수량 표시
- **국가별 인증서 분포**: 상위 10개국의 인증서 수량을 진행 바 차트로 표시

**사용 API**: `GET /api/health/database`, `GET /api/health/ldap`, `GET /api/upload/countries?limit=10`, `GET /api/icao/status`

---

#### 2. 파일 업로드 (FileUpload)

| 항목 | 내용 |
|------|------|
| **라우트** | `/upload` |
| **파일** | `frontend/src/pages/FileUpload.tsx` |
| **인증** | 필요 (JWT) |

LDIF 파일 및 Master List 파일 업로드 전용 페이지. SSE(Server-Sent Events)를 통해 처리 진행 상황을 실시간으로 스트리밍한다.

- **드래그 & 드롭**: `.ldif`, `.ml` 확장자 파일
- **3단계 수평 스테퍼**: PARSING -> VALIDATION -> DB_SAVING/LDAP_SAVING
- **실시간 진행 바**: 처리된 항목 수/전체 항목 수
- **EventLog 패널**: SSE 이벤트를 시간순으로 누적 표시
- **오류 요약 패널**: 파싱/DB/LDAP 오류 분류별 수량 및 상세 메시지

**특수 패턴**: SSE 재연결 시 `isProcessingRef` (useRef)로 stale closure 방지, LDAP 연결 실패 시 DB 전용 모드로 진행

---

#### 3. 인증서 업로드 (CertificateUpload)

| 항목 | 내용 |
|------|------|
| **라우트** | `/upload/certificate` |
| **파일** | `frontend/src/pages/CertificateUpload.tsx` |
| **인증** | 필요 (JWT) |

개별 인증서 파일 업로드. **미리보기-저장 확인(Preview-before-Save)** 워크플로우.

- **지원 형식**: PEM, DER, CER, P7B, DL(Deviation List), CRL
- **인증서 카드 서브 탭**: 일반(메타데이터) / 상세(TreeViewer) / Doc 9303(준수 체크리스트)
- **중복 감지**: SHA-256 해시 기반

**페이지 상태 머신**: `IDLE -> FILE_SELECTED -> PREVIEWING -> PREVIEW_READY -> CONFIRMING -> COMPLETED/FAILED`

---

#### 4. 업로드 이력 (UploadHistory)

| 항목 | 내용 |
|------|------|
| **라우트** | `/upload-history` |
| **파일** | `frontend/src/pages/UploadHistory.tsx` |
| **인증** | 불필요 (공개) |

전체 업로드 이력 관리 페이지.

- **필터**: 상태(COMPLETED/FAILED/PROCESSING/PENDING), 파일 형식, 파일명 검색
- **자동 갱신**: PENDING/PROCESSING 상태의 업로드가 있으면 5초 간격으로 자동 갱신
- **재처리**: FAILED 업로드의 이어하기 재처리 (fingerprint 캐시 기반)
- **삭제**: FAILED/PENDING 상태 업로드 삭제

---

#### 5. 업로드 상세 (UploadDetail)

| 항목 | 내용 |
|------|------|
| **라우트** | `/upload/:uploadId` |
| **파일** | `frontend/src/pages/UploadDetail.tsx` |
| **인증** | 불필요 (공개) |

특정 업로드의 전체 상세 정보 페이지. PENDING/PROCESSING 상태일 때 3초마다 자동 갱신.

---

#### 6. 업로드 대시보드 (UploadDashboard)

| 항목 | 내용 |
|------|------|
| **라우트** | `/upload-dashboard` |
| **파일** | `frontend/src/pages/UploadDashboard.tsx` |
| **인증** | 불필요 (공개) |

업로드 및 검증 통계 대시보드. 클릭 가능한 통계 카드와 업로드 트렌드 차트를 제공한다.

---

#### 7. 인증서 검색 (CertificateSearch)

| 항목 | 내용 |
|------|------|
| **라우트** | `/pkd/certificates` |
| **파일** | `frontend/src/pages/CertificateSearch.tsx` |
| **인증** | 불필요 (공개) |

LDAP 기반 인증서 검색 및 상세 조회 페이지.

- **필터**: 국가, 인증서 유형, 유효성, 출처, 검색어
- **4탭 상세 다이얼로그**: 일반 / 상세(TreeViewer) / Doc 9303 / 포렌식(AI)
- **내보내기**: 개별 PEM, 국가별 ZIP, 전체 DIT 구조 ZIP

**특수 패턴**: `AbortController`로 필터 변경 시 이전 검색 요청 자동 취소

---

### 보고서

#### 8. 표준 부적합 DSC 보고서 (DscNcReport)

| 항목 | 내용 |
|------|------|
| **라우트** | `/pkd/dsc-nc` |
| **파일** | `frontend/src/pages/DscNcReport.tsx` |
| **인증** | 불필요 (공개) |

DSC_NC 인증서 분석 보고서 대시보드.

- **5개 차트**: 비준수 코드, 국가별, 발급 연도별, 서명/공개 키 알고리즘
- **CSV 내보내기**: 14개 컬럼, BOM 포함 (Excel UTF-8 호환)

---

#### 9. CRL 보고서 (CrlReport)

| 항목 | 내용 |
|------|------|
| **라우트** | `/pkd/crl` |
| **파일** | `frontend/src/pages/CrlReport.tsx` |
| **인증** | 불필요 (공개) |

인증서 폐기 목록(CRL) 분석 보고서 대시보드.

- **3개 차트**: 국가별 폐기 인증서, 서명 알고리즘, 폐기 사유
- **CRL 다운로드**: `.crl` 바이너리 파일 (DER 형식)
- **폐기 사유 한국어 번역**: RFC 5280 11가지 사유

---

#### 10. DSC Trust Chain 보고서 (TrustChainValidationReport)

| 항목 | 내용 |
|------|------|
| **라우트** | `/pkd/trust-chain` |
| **파일** | `frontend/src/pages/TrustChainValidationReport.tsx` |
| **인증** | 불필요 (공개) |

DSC Trust Chain 검증 통계 및 샘플 인증서 빠른 조회 페이지.

---

### Passive Authentication

#### 11. PA 검증 (PAVerify)

| 항목 | 내용 |
|------|------|
| **라우트** | `/pa/verify` |
| **파일** | `frontend/src/pages/PAVerify.tsx` |
| **인증** | 불필요 (공개) |
| **사이드바 권한** | `pa:verify` |

여권 Passive Authentication(PA) 검증 페이지. ICAO 9303 Part 10 & 11 기반 8단계 검증.

**전체 검증 모드**: SOD 및 DG 파일 업로드하여 8단계 검증 수행 (SOD 파싱, DSC 추출, Trust Chain, CSCA 조회, SOD 서명, DG 해시, CRL 확인, 최종 판정).

**간편 검증 모드**: Subject DN 또는 SHA-256 지문으로 사전 계산된 Trust Chain 결과 즉시 조회 (5~20ms).

---

#### 12. PA 이력 (PAHistory)

| 항목 | 내용 |
|------|------|
| **라우트** | `/pa/history` |
| **파일** | `frontend/src/pages/PAHistory.tsx` |
| **인증** | 불필요 (공개) |
| **사이드바 권한** | `pa:read` |

PA 검증 이력 목록 페이지. 5건/페이지 기본 표시.

- **필터**: 상태(VALID/INVALID/ERROR), 국가, 날짜 범위
- **익명 사용자**: `anonymous (192.168.1.100)` 형식으로 클라이언트 IP 표시

---

#### 13. PA 상세 (PADetail)

| 항목 | 내용 |
|------|------|
| **라우트** | `/pa/:paId` |
| **파일** | `frontend/src/pages/PADetail.tsx` |
| **인증** | 불필요 (공개) |

특정 PA 검증의 전체 상세 페이지. 3개 검증 카드 (인증서 체인, SOD 서명, DG 해시).

---

#### 14. PA 대시보드 (PADashboard)

| 항목 | 내용 |
|------|------|
| **라우트** | `/pa/dashboard` |
| **파일** | `frontend/src/pages/PADashboard.tsx` |
| **인증** | 불필요 (공개) |
| **사이드바 권한** | `pa:stats` (v2.37.0에서 `pa:read`에서 분리) |

PA 검증 통계 대시보드. 도넛 차트, 국가 Top-10, 30일 트렌드.

---

### 동기화 & 모니터링

#### 15. 동기화 대시보드 (SyncDashboard)

| 항목 | 내용 |
|------|------|
| **라우트** | `/sync` |
| **파일** | `frontend/src/pages/SyncDashboard.tsx` |
| **인증** | 불필요 (공개) |

DB-LDAP 동기화 상태 모니터링 및 관리 대시보드.

- **동기화 상태 카드**: DB vs LDAP 인증서 수량 비교
- **인증서 재검증**: 3단계 파이프라인 (만료 상태, Trust Chain, CRL 폐기)
- **설정 관리**: 일일 동기화 시간, 자동 재검증, 자동 재조정

---

#### 16. ICAO 상태 (IcaoStatus)

| 항목 | 내용 |
|------|------|
| **라우트** | `/icao` |
| **파일** | `frontend/src/pages/IcaoStatus.tsx` |
| **인증** | 불필요 (공개) |

ICAO PKD 버전 모니터링 페이지.

- **3개 패널**: DSC/CRL, DSC_NC, CSCA Master List 각각의 ICAO 최신 vs 로컬 버전 비교
- **버전 이력 테이블**: 5가지 상태 (DETECTED/NOTIFIED/DOWNLOADED/IMPORTED/FAILED)

---

#### 17. 시스템 모니터링 (MonitoringDashboard)

| 항목 | 내용 |
|------|------|
| **라우트** | `/monitoring` |
| **파일** | `frontend/src/pages/MonitoringDashboard.tsx` |
| **인증** | 필요 (관리자 전용, v2.37.0에서 `adminOnly` 추가) |

시스템 리소스 및 서비스 상태 실시간 모니터링. 10초 간격으로 자동 갱신.

- **시스템 메트릭 카드**: CPU, 메모리, 디스크, 네트워크 I/O
- **서비스 상태 카드**: 5개 서비스 + DB + LDAP (UP/DEGRADED/DOWN)

---

#### 18. AI 인증서 분석 (AiAnalysisDashboard)

| 항목 | 내용 |
|------|------|
| **라우트** | `/ai/analysis` |
| **파일** | `frontend/src/pages/AiAnalysisDashboard.tsx` |
| **인증** | 불필요 (공개) |

ML 기반 인증서 이상 탐지 및 포렌식 분석 대시보드.

- **위험 수준 분포 바**: LOW/MEDIUM/HIGH/CRITICAL
- **국가별 PKI 성숙도**: 상위 15개국 수평 막대 차트
- **알고리즘 마이그레이션 트렌드**: 연도별 누적 스택 AreaChart
- **분석 실행**: 백그라운드 배치 분석, 3초 폴링으로 진행률 표시

**특수 패턴**: `Promise.allSettled` 7개 통계 API 병렬 호출, `AbortController` 이상 목록 필터 변경 시 이전 요청 취소

---

### 관리자 & 보안

#### 19. API 클라이언트 관리 (ApiClientManagement)

| 항목 | 내용 |
|------|------|
| **라우트** | `/admin/api-clients` |
| **파일** | `frontend/src/pages/ApiClientManagement.tsx` |
| **인증** | 필요 (JWT, 관리자 전용) |

외부 시스템 API Key 발급 및 접근 권한 관리.

- **12가지 API 권한**: cert:read, cert:export, pa:verify, pa:read, pa:stats, upload:read, upload:write, report:read, ai:read, sync:read, icao:read, api-client:manage
- **API Key 발급**: 일회성 표시, SHA-256 해시만 저장 (복구 불가)
- **사용량 조회**: 7/30/90일 기간별 엔드포인트 요청 수 차트

---

#### 20. 로그인 이력 (AuditLog)

| 항목 | 내용 |
|------|------|
| **라우트** | `/admin/audit-log` |
| **파일** | `frontend/src/pages/AuditLog.tsx` |
| **인증** | 필요 (JWT, 관리자 전용) |

사용자 인증 감사 로그 조회. 이벤트 타입별 배지 색상 (LOGIN=초록, LOGIN_FAILED=빨강, LOGOUT=노랑, TOKEN_REFRESH=보라).

---

#### 21. 운영 감사 로그 (OperationAuditLog)

| 항목 | 내용 |
|------|------|
| **라우트** | `/admin/operation-audit` |
| **파일** | `frontend/src/pages/OperationAuditLog.tsx` |
| **인증** | 필요 (JWT, 관리자 전용) |

26가지 작업 유형에 대한 운영 감사 로그 (업로드, PA, 동기화, ICAO, API 클라이언트, 코드 마스터, 사용자, 인증서, 기타).

---

#### 22. 사용자 관리 (UserManagement)

| 항목 | 내용 |
|------|------|
| **라우트** | `/admin/users` |
| **파일** | `frontend/src/pages/UserManagement.tsx` |
| **인증** | 필요 (JWT, 관리자 전용) |

시스템 사용자 및 권한 관리. 그리드 카드 레이아웃으로 사용자 목록을 표시한다.

---

### 공통

#### 23. 로그인 (Login)

| 항목 | 내용 |
|------|------|
| **라우트** | `/login` |
| **파일** | `frontend/src/pages/Login.tsx` |
| **인증** | 불필요 |

시스템 진입점이자 랜딩 페이지. 2패널 레이아웃 (좌측 히어로 + 우측 로그인 폼), JWT 인증, 다크/라이트 모드 토글.

---

#### 24. 프로필 (Profile)

| 항목 | 내용 |
|------|------|
| **라우트** | `/profile` |
| **파일** | `frontend/src/pages/Profile.tsx` |
| **인증** | 필요 (JWT) |

현재 로그인한 사용자의 프로필 및 비밀번호 변경.

---

### 추가 페이지

#### 25. CSR 관리 (CsrManagement)

| 항목 | 내용 |
|------|------|
| **라우트** | `/admin/csr` |
| **파일** | `frontend/src/pages/CsrManagement.tsx` |
| **인증** | JWT 필수 (관리자) |

ICAO PKD CSR 생성, 외부 CSR Import, ICAO 발급 인증서 등록을 관리하는 페이지. 모든 데이터 AES-256-GCM 암호화 저장.

---

#### 26. DSC 등록 승인 (PendingDscApproval)

| 항목 | 내용 |
|------|------|
| **라우트** | `/admin/pending-dsc` |
| **파일** | `frontend/src/pages/PendingDscApproval.tsx` |
| **인증** | 필요 (JWT, 관리자 전용) |

PA 검증에서 자동 추출된 DSC 인증서의 등록 승인/거부 관리.

---

#### 27. 인증서 품질 보고서 (CertificateQualityReport)

| 항목 | 내용 |
|------|------|
| **라우트** | `/pkd/quality` |
| **파일** | `frontend/src/pages/CertificateQualityReport.tsx` |
| **인증** | 불필요 (공개) |

ICAO Doc 9303 기반 인증서 품질(준수/미준수/경고) 분석 보고서 대시보드.

---

#### 28. API 클라이언트 접근 신청 (ApiClientRequest)

| 항목 | 내용 |
|------|------|
| **라우트** | `/api-client-request` |
| **파일** | `frontend/src/pages/ApiClientRequest.tsx` |
| **인증** | 불필요 (공개, Layout 외부) |

외부 시스템에서 API 클라이언트 접근을 신청하는 공개 페이지. PII 암호화 (AES-256-GCM).

---

#### 29. EAC 대시보드 (EacDashboard)

| 항목 | 내용 |
|------|------|
| **라우트** | `/eac/dashboard` |
| **파일** | `frontend/src/pages/EacDashboard.tsx` |
| **인증** | 필요 (관리자 전용, 실험적 기능) |

BSI TR-03110 기반 CVC(Card Verifiable Certificate) 인증서 관리 실험적 대시보드.

---

### 공통 패턴 요약

#### 인증 및 접근 제어

| 유형 | 페이지 |
|------|--------|
| 공개 (JWT 불필요) | Dashboard, CertificateSearch, DscNcReport, CertificateQualityReport, CrlReport, TrustChain, PAVerify, PAHistory, PADetail, PADashboard, SyncDashboard, IcaoStatus, AiAnalysis, UploadHistory, UploadDetail, UploadDashboard, ApiClientRequest |
| JWT 필요 (권한 기반) | FileUpload, CertificateUpload, Profile |
| 관리자 전용 | ApiClientManagement, AuditLog, OperationAuditLog, UserManagement, PendingDscApproval, CsrManagement, MonitoringDashboard, EacDashboard |

#### 데이터 갱신 방식

| 방식 | 페이지 | 간격 | 조건 |
|------|--------|------|------|
| SSE (실시간) | FileUpload | 이벤트 기반 | 업로드 진행 중 |
| 폴링 | UploadHistory | 5초 | PENDING/PROCESSING 존재 시 |
| 폴링 | UploadDetail | 3초 | PENDING/PROCESSING 상태 시 |
| 폴링 | MonitoringDashboard | 10초 | 항상 |
| 폴링 | AiAnalysisDashboard | 3초 | 분석 실행 중 |
| 수동 | 나머지 페이지 | - | 사용자 액션 시 |

#### 성능 최적화 패턴

| 패턴 | 적용 페이지 |
|------|------------|
| `React.lazy()` 코드 스플리팅 | 전체 27개 페이지 (Login, Dashboard 제외) |
| `AbortController` (stale 요청 취소) | CertificateSearch, DscNcReport, MonitoringDashboard, AiAnalysisDashboard |
| `useRef` (stale closure 방지) | FileUpload, AiAnalysisDashboard |
| `Promise.allSettled` (병렬 호출) | AiAnalysisDashboard, Dashboard |
| `useMemo` (재계산 방지) | AiAnalysisDashboard |

#### 차트 라이브러리 (Recharts)

| 차트 유형 | 사용 페이지 |
|-----------|------------|
| LineChart | UploadDashboard |
| BarChart (수직) | DscNcReport, CrlReport, AiAnalysisDashboard, CertificateQualityReport |
| BarChart (수평) | DscNcReport, AiAnalysisDashboard, ApiClientManagement |
| PieChart | DscNcReport, CrlReport, PADashboard, AiAnalysisDashboard |
| AreaChart | PADashboard, AiAnalysisDashboard |

#### Oracle 호환성 (프론트엔드)

| 헬퍼 | 용도 | 사용 페이지 |
|------|------|------------|
| `toBool(v)` | true/1/"1"/"true" 처리 | UserManagement |
| `parsePermissions(v)` | 배열/JSON 문자열/null 처리 | UserManagement |
| `stored_in_ldap` 문자열 | "1"/"0" boolean 처리 | CrlReport |

---

**Copyright 2026 SMARTCORE Inc. All rights reserved.**
