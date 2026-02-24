# ICAO Local PKD — Frontend Design System

**Version**: 1.0
**Last Updated**: 2026-02-24
**Tech Stack**: React 19, TypeScript, Tailwind CSS 4, Vite, Lucide Icons, Recharts

---

## 1. Design Concept

### Brand Identity

ICAO Local PKD는 ePassport 인증서 관리 시스템으로, **엔터프라이즈급 보안 소프트웨어**에 적합한 디자인을 지향한다.

| 항목 | 값 |
|------|----|
| 로고 아이콘 | `ShieldCheck` (Lucide) |
| 브랜드 그래디언트 | `from-blue-600 to-indigo-600` |
| 시스템 명 | ICAO Local PKD |
| 부제 | ePassport 인증서 관리 시스템 |
| 표준 참조 | ICAO Doc 9303, RFC 5280, RFC 5652 |

### Design Principles

1. **Enterprise Clarity** — 복잡한 인증서 데이터를 명확하게 전달
2. **Consistent Tokens** — 동일 역할의 요소는 동일 스타일 적용
3. **Dark Mode First** — 모든 색상에 light/dark 변형 제공
4. **Responsive Grid** — Mobile-first, `md`/`lg`/`xl` 반응형 브레이크포인트
5. **Minimal Decoration** — 기능 중심 UI, 불필요한 장식 배제

---

## 2. Color Theme

### 2.1 Neutral Palette

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

### 2.2 Functional Area Gradients (Icon Badge)

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

### 2.3 Status Colors

| 상태 | 배경 (Light) | 배경 (Dark) | 텍스트 (Light) | 텍스트 (Dark) |
|------|-------------|-------------|---------------|---------------|
| Success | `bg-green-50` | `dark:bg-green-900/30` | `text-green-700` | `dark:text-green-300` |
| Error | `bg-red-50` | `dark:bg-red-900/30` | `text-red-700` | `dark:text-red-300` |
| Warning | `bg-amber-50` | `dark:bg-amber-900/20` | `text-amber-700` | `dark:text-amber-300` |
| Info | `bg-blue-50` | `dark:bg-blue-900/20` | `text-blue-700` | `dark:text-blue-300` |

### 2.4 Certificate Type Badge Colors

| 타입 | 배경 | 텍스트 |
|------|------|--------|
| CSCA | `bg-blue-50 dark:bg-blue-900/20` | `text-blue-700 dark:text-blue-300` |
| DSC | `bg-green-50 dark:bg-green-900/20` | `text-green-700 dark:text-green-300` |
| MLSC | `bg-purple-50 dark:bg-purple-900/20` | `text-purple-700 dark:text-purple-300` |
| DSC_NC | `bg-orange-50 dark:bg-orange-900/20` | `text-orange-700 dark:text-orange-300` |
| CRL | `bg-amber-50 dark:bg-amber-900/20` | `text-amber-700 dark:text-amber-300` |

### 2.5 Risk Level Colors (AI Analysis)

| 레벨 | 배경 | 텍스트 |
|------|------|--------|
| LOW | `bg-green-500` | `text-white` |
| MEDIUM | `bg-yellow-500` | `text-white` |
| HIGH | `bg-orange-500` | `text-white` |
| CRITICAL | `bg-red-500` | `text-white` |

### 2.6 Validation Status Colors

| 상태 | 색상 |
|------|------|
| VALID | `green-500` |
| EXPIRED_VALID | `amber-500` |
| EXPIRED | `red-500` |
| INVALID | `red-600` |
| PENDING | `gray-400` |
| NOT_YET_VALID | `yellow-500` |

---

## 3. Typography

### 3.1 Heading Hierarchy

| 레벨 | 클래스 | 용도 |
|------|--------|------|
| H1 | `text-2xl font-bold text-gray-900 dark:text-white` | 페이지 제목 |
| H2 | `text-xl font-bold text-gray-900 dark:text-white` | 섹션 제목 |
| H3 | `text-lg font-semibold text-gray-900 dark:text-white` | 카드 제목 |
| H4 | `text-sm font-semibold text-gray-700 dark:text-gray-300` | 서브섹션 |

### 3.2 Body Text

| 용도 | 클래스 |
|------|--------|
| 본문 | `text-sm text-gray-700 dark:text-gray-300` |
| 부제 / 설명 | `text-sm text-gray-500 dark:text-gray-400` |
| 소형 캡션 | `text-xs text-gray-500 dark:text-gray-400` |
| 강조 | `text-base font-medium text-gray-900 dark:text-white` |
| 모노스페이스 | `font-mono text-sm` (해시, 시리얼 번호) |

### 3.3 Labels

| 용도 | 클래스 |
|------|--------|
| Form label | `block text-sm font-medium text-gray-700 dark:text-gray-300 mb-1` |
| Section header (xs) | `text-xs font-semibold uppercase tracking-wider text-gray-400` |

---

## 4. Layout

### 4.1 Page Structure

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

### 4.2 Page Wrapper

```
w-full px-4 lg:px-6 py-4 space-y-6
```

### 4.3 Sidebar

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

### 4.4 Responsive Breakpoints

| Prefix | 너비 | 용도 |
|--------|------|------|
| (base) | 0px+ | 모바일 (1열 그리드) |
| `md:` | 768px+ | 태블릿 (2열 그리드) |
| `lg:` | 1024px+ | 데스크톱 (3열+, 사이드바 표시) |
| `xl:` | 1280px+ | 와이드 (3열 확장) |

---

## 5. Components

### 5.1 Page Header (Icon Badge Pattern)

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

### 5.2 Cards

#### Standard Card

```
bg-white dark:bg-gray-800 rounded-2xl shadow-lg
border border-gray-200 dark:border-gray-700  (optional)
overflow-hidden  (table cards)
```

#### Stats Card (border-l-4)

```
bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5 border-l-4 border-{color}-500
```

```
┌────┬──────────────────────┐
│ ▌  │  Label (text-sm)     │
│ ▌  │  Value (text-2xl)    │
└────┴──────────────────────┘
```

#### Filter Card

```
bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-5
```

#### Table Card

```
bg-white dark:bg-gray-800 rounded-2xl shadow-lg overflow-hidden
```

### 5.3 Dialogs / Modals

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

### 5.4 Buttons

#### Primary (Gradient)

```
bg-gradient-to-r from-blue-500 to-indigo-500
hover:from-blue-600 hover:to-indigo-600
text-white rounded-xl shadow-md hover:shadow-lg
transition-all
disabled:opacity-50 disabled:cursor-not-allowed
```

#### Secondary

```
text-gray-700 dark:text-gray-300
hover:bg-gray-100 dark:hover:bg-gray-700
rounded-xl transition-colors
```

#### Danger

```
bg-red-600 hover:bg-red-700
text-white rounded-xl transition-colors
```

#### Icon-only (Action)

```
p-2 rounded-lg hover:bg-{color}-50 dark:hover:bg-{color}-900/30
text-{color}-600 dark:text-{color}-400 transition-colors
```

#### Export (Green)

```
bg-green-600 hover:bg-green-700
text-white rounded-lg text-sm font-medium
flex items-center gap-2 transition-colors
```

### 5.5 Form Inputs

#### Standard Input

```
w-full px-3 py-2 text-sm
border border-gray-300 dark:border-gray-600
rounded-xl
bg-white dark:bg-gray-700
text-gray-900 dark:text-white
focus:outline-none focus:ring-2 focus:ring-blue-500
```

#### Input with Icon (Left)

```
<div className="relative">
  <Icon className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
  <input className="... pl-9 ..." />
</div>
```

#### Select Dropdown

```
w-full px-3 py-2 text-sm
border border-gray-200 dark:border-gray-600
rounded-lg
bg-white dark:bg-gray-700
text-gray-900 dark:text-white
focus:outline-none focus:ring-2 focus:ring-blue-500
```

#### Search Input

```
<div className="relative">
  <Search className="absolute left-3 top-1/2 -translate-y-1/2 w-4 h-4 text-gray-400" />
  <input className="... pl-9 pr-4 py-2 ..." placeholder="검색..." />
</div>
```

### 5.6 Tables

```
Table Container:  rounded-2xl shadow-lg overflow-hidden
Header Row:       bg-gray-50 dark:bg-gray-900/50 border-b border-gray-200 dark:border-gray-700
Header Cell:      px-6 py-3 text-xs font-medium text-gray-500 dark:text-gray-400 uppercase
Body Row:         hover:bg-gray-50 dark:hover:bg-gray-900/30 transition-colors
Body Cell:        px-6 py-4 text-sm text-gray-600 dark:text-gray-400
Divider:          divide-y divide-gray-200 dark:divide-gray-700
```

### 5.7 Status Badges / Pills

#### 기본 뱃지

```
inline-flex items-center gap-1
px-2 py-0.5
bg-{color}-100 dark:bg-{color}-900/30
text-{color}-700 dark:text-{color}-300
text-xs font-medium rounded-full
```

#### 관리자 뱃지

```
inline-flex items-center gap-1
px-2 py-0.5
bg-blue-100 dark:bg-blue-900/30
text-blue-700 dark:text-blue-300
text-xs font-medium rounded-full
(Shield w-3 h-3 icon)
```

#### 권한 뱃지

```
px-2 py-0.5
bg-blue-50 dark:bg-blue-900/20
text-blue-600 dark:text-blue-400
text-xs rounded-full
```

### 5.8 Alerts / Notifications

#### Success

```
bg-green-50 dark:bg-green-900/30
border border-green-200 dark:border-green-800
rounded-xl p-4
flex items-center gap-2
(Check w-5 h-5 text-green-600 dark:text-green-400)
(text: text-green-700 dark:text-green-300)
```

#### Error

```
bg-red-50 dark:bg-red-900/30
border border-red-200 dark:border-red-800
rounded-xl p-4
flex items-center gap-2
(AlertCircle w-5 h-5 text-red-600 dark:text-red-400)
(text: text-red-700 dark:text-red-300)
```

#### Warning Banner (ICAO Update)

```
bg-gradient-to-r from-amber-50 to-orange-50
dark:from-amber-900/20 dark:to-orange-900/20
border border-amber-200 dark:border-amber-700
rounded-2xl shadow-lg px-6 py-4
```

### 5.9 Loading States

#### Spinner

```
animate-spin w-8 h-8
border-2 border-blue-500 border-t-transparent rounded-full
```

#### Loading Container

```
bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-12 text-center
(spinner + "로딩 중..." 텍스트)
```

#### Skeleton

```
bg-gray-200 dark:bg-gray-700 animate-pulse rounded
```

### 5.10 Empty States

```
bg-white dark:bg-gray-800 rounded-2xl shadow-lg p-12 text-center
(Icon w-12 h-12 text-gray-300 dark:text-gray-600 mx-auto mb-3)
(text-gray-500 dark:text-gray-400)
```

### 5.11 Pagination

```
flex items-center justify-center gap-2
Page Button:    px-3 py-1 rounded-lg text-sm
Active:         bg-blue-50 dark:bg-blue-900/20 text-blue-600 dark:text-blue-400 font-medium
Inactive:       text-gray-600 dark:text-gray-400 hover:bg-gray-100 dark:hover:bg-gray-700
Disabled:       text-gray-300 dark:text-gray-600 cursor-not-allowed
```

---

## 6. Spacing System

### 6.1 Padding

| 토큰 | 값 | 용도 |
|------|----|------|
| `p-2` | 8px | 아이콘 버튼, 컴팩트 요소 |
| `p-2.5` | 10px | 토글 영역, 리스트 아이템 |
| `p-3` | 12px | 아이콘 뱃지, 컴팩트 카드 |
| `p-4` | 16px | 검색 바, 알림 |
| `p-5` | 20px | 필터 카드, 통계 카드 |
| `p-6` | 24px | 대형 카드 본문 |
| `px-5 py-4` | — | 다이얼로그 헤더/본문 |
| `px-5 py-3` | — | 다이얼로그 푸터 |

### 6.2 Gap

| 토큰 | 값 | 용도 |
|------|----|------|
| `gap-1` | 4px | 뱃지 내부 아이콘-텍스트 |
| `gap-1.5` | 6px | 권한 그리드 |
| `gap-2` | 8px | 버튼 그룹, 뱃지 목록 |
| `gap-3` | 12px | 폼 필드 그리드, 아이콘-텍스트 |
| `gap-4` | 16px | 카드 그리드, 주요 섹션 |
| `gap-6` | 24px | 페이지 레벨 간격 |

### 6.3 Vertical Spacing

| 토큰 | 용도 |
|------|------|
| `space-y-3` | 카드 내부 섹션 |
| `space-y-4` | 폼 필드, 다이얼로그 본문 |
| `space-y-5` | 다이얼로그 섹션 |
| `space-y-6` | 페이지 레벨 섹션 |

---

## 7. Border Radius & Shadow

### 7.1 Border Radius Hierarchy

| 토큰 | 값 | 용도 |
|------|----|------|
| `rounded-lg` | 8px | 입력 필드, 작은 UI, 뱃지 내부 요소 |
| `rounded-xl` | 12px | 아이콘 뱃지, 버튼, 폼 입력, 권한 체크박스 카드 |
| `rounded-2xl` | 16px | 카드, 다이얼로그, 테이블 래퍼 |
| `rounded-full` | 100% | 뱃지/필, 아바타, 상태 인디케이터 |

### 7.2 Shadow Hierarchy

| 토큰 | 용도 |
|------|------|
| `shadow-lg` | 표준 카드, 아이콘 뱃지 |
| `shadow-xl` | 다이얼로그, 호버 시 카드 |
| `shadow-md` | 호버 버튼 |
| `shadow-blue-500/25` | 그래디언트 버튼 (Landing) |

---

## 8. Animation & Transitions

| 패턴 | 용도 |
|------|------|
| `transition-colors` | 버튼, 링크 호버 |
| `transition-all` | 그래디언트 버튼, 카드 호버 |
| `transition-shadow` | 카드 호버 (`hover:shadow-xl`) |
| `transition-all duration-300` | 사이드바 확장/축소 |
| `animate-spin` | 로딩 스피너 |
| `animate-pulse` | 스켈레톤 로딩 |
| `backdrop-blur-sm` | 모달 배경 |

---

## 9. Landing Page (Login.tsx)

Landing 페이지는 시스템 전반과 다른 특별한 디자인을 적용한다:

### 9.1 Hero Panel (좌측 55%)

```
bg-gradient-to-br from-blue-600 via-indigo-600 to-purple-700
```

**Glassmorphism 카드:**
```
bg-white/5 backdrop-blur-sm rounded-xl border border-white/10
hover:bg-white/10
```

**통계 카드:**
```
bg-white/10 backdrop-blur-sm rounded-xl border border-white/15
hover:bg-white/15
```

**장식 요소:**
- 32px 라디얼 그리드 오버레이 (`opacity-[0.03]`)
- 블러 원형 (`blur-2xl`, `blur-3xl`)

### 9.2 Login Panel (우측)

```
배경:  bg-gray-50 dark:bg-gray-900
카드:  bg-white dark:bg-gray-800 rounded-2xl shadow-xl border
입력:  bg-gray-50 dark:bg-gray-700/50 focus:bg-white dark:focus:bg-gray-700
버튼:  bg-gradient-to-r from-blue-500 to-cyan-500 shadow-lg shadow-blue-500/25
```

### 9.3 Animation

```css
@keyframes slideInUp {
  from { opacity: 0; transform: translateY(20px); }
  to   { opacity: 1; transform: translateY(0); }
}
```

Stagger delay: 0.1s → 0.2s → 0.3s → 0.4s

---

## 10. Dark Mode

### 10.1 구현 방식

- Zustand `themeStore`로 상태 관리 (`localStorage` persist)
- `document.documentElement.classList.add/remove('dark')`
- Tailwind CSS `darkMode: 'class'` 전략

### 10.2 패턴 규칙

모든 색상 속성에 `dark:` 변형을 반드시 제공:

```
bg-white dark:bg-gray-800
text-gray-900 dark:text-white
border-gray-200 dark:border-gray-700
bg-{color}-50 dark:bg-{color}-900/20  (반투명 dark)
text-{color}-700 dark:text-{color}-300
hover:bg-gray-100 dark:hover:bg-gray-700
```

---

## 11. Z-Index Stack

| 값 | 용도 |
|----|------|
| `z-10` | 테이블 sticky header |
| `z-40` | 페이지 헤더 바 (sticky) |
| `z-50` | 모달 다이얼로그 |
| `z-60` | 사이드바 |

---

## 12. Icon System

- **라이브러리**: Lucide React (`lucide-react`)
- **페이지 아이콘 크기**: `w-7 h-7` (헤더 뱃지 내)
- **카드 아이콘 크기**: `w-5 h-5`
- **인라인 아이콘**: `w-4 h-4`
- **뱃지 아이콘**: `w-3 h-3`
- **색상 규칙**: 헤더 뱃지 내 `text-white`, 카드 내 `text-gray-400`

---

## 13. Grid Patterns (Responsive)

| 용도 | 클래스 |
|------|--------|
| 통계 카드 | `grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-4` |
| 사용자 카드 | `grid grid-cols-1 md:grid-cols-2 xl:grid-cols-3 gap-4` |
| 폼 필드 | `grid grid-cols-1 md:grid-cols-2 gap-3` |
| 권한 체크박스 | `grid grid-cols-2 lg:grid-cols-3 gap-1.5` |
| 필터 | `grid grid-cols-1 md:grid-cols-2 lg:grid-cols-4 gap-3` |
| 프로필 정보 | `grid grid-cols-1 md:grid-cols-2 gap-4` |

---

## 14. Chart Styling (Recharts)

```
컨테이너: rounded-lg bg-gray-50 dark:bg-gray-800 p-4 border border-gray-200 dark:border-gray-700
툴팁:    bg-white dark:bg-gray-800 rounded-lg shadow-lg border
텍스트:   text-gray-900 dark:text-white
색상:     Recharts 기본 팔레트 + 커스텀 status 색상
```

---

## 15. File Structure

```
frontend/src/
├── App.tsx                    # Layout (Sidebar + Header + Content)
├── pages/                     # 24 페이지
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
