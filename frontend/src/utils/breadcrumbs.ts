export interface BreadcrumbItem {
  label: string;
  path?: string;
}

const ROUTE_BREADCRUMBS: Record<string, BreadcrumbItem[]> = {
  '/': [],

  // 인증서 관리
  '/icao': [{ label: '인증서 관리' }, { label: 'ICAO 버전 상태' }],
  '/upload': [{ label: '인증서 관리' }, { label: '파일 업로드' }],
  '/upload/certificate': [{ label: '인증서 관리' }, { label: '인증서 업로드' }],
  '/pkd/certificates': [{ label: '인증서 관리' }, { label: '인증서 조회' }],
  '/upload-history': [{ label: '인증서 관리' }, { label: '업로드 이력' }],
  '/sync': [{ label: '인증서 관리' }, { label: '동기화 상태' }],

  // 위·변조 검사
  '/pa/verify': [{ label: '위·변조 검사' }, { label: 'PA 검증 수행' }],
  '/pa/history': [{ label: '위·변조 검사' }, { label: '검증 이력' }],

  // 보고서 & 분석
  '/upload-dashboard': [{ label: '보고서 & 분석' }, { label: '인증서 보고서' }, { label: '인증서 통계' }],
  '/pkd/trust-chain': [{ label: '보고서 & 분석' }, { label: '인증서 보고서' }, { label: 'DSC Trust Chain' }],
  '/pkd/crl': [{ label: '보고서 & 분석' }, { label: '인증서 보고서' }, { label: 'CRL 보고서' }],
  '/pkd/dsc-nc': [{ label: '보고서 & 분석' }, { label: '인증서 보고서' }, { label: '표준 부적합 DSC' }],
  '/pa/dashboard': [{ label: '보고서 & 분석' }, { label: 'PA 검증 통계' }],
  '/ai/analysis': [{ label: '보고서 & 분석' }, { label: 'AI 인증서 분석' }],

  // 시스템 관리
  '/monitoring': [{ label: '시스템 관리' }, { label: '시스템 모니터링' }],
  '/admin/users': [{ label: '시스템 관리' }, { label: '사용자 관리' }],
  '/admin/api-clients': [{ label: '시스템 관리' }, { label: 'API 클라이언트' }],
  '/admin/operation-audit': [{ label: '시스템 관리' }, { label: '운영 감사 로그' }],
  '/admin/audit-log': [{ label: '시스템 관리' }, { label: '인증 감사 로그' }],

  // Profile
  '/profile': [{ label: '프로필' }],
};

/** Dynamic route patterns */
const DYNAMIC_PATTERNS: { regex: RegExp; crumbs: BreadcrumbItem[] }[] = [
  {
    regex: /^\/upload\/(?!certificate)[^/]+$/,
    crumbs: [
      { label: '인증서 관리' },
      { label: '업로드 이력', path: '/upload-history' },
      { label: '상세' },
    ],
  },
  {
    regex: /^\/pa\/(?!verify|history|dashboard)[^/]+$/,
    crumbs: [
      { label: '위·변조 검사' },
      { label: '검증 이력', path: '/pa/history' },
      { label: '상세' },
    ],
  },
];

export function getBreadcrumbs(pathname: string): BreadcrumbItem[] {
  // Static match first
  if (pathname in ROUTE_BREADCRUMBS) {
    return ROUTE_BREADCRUMBS[pathname];
  }

  // Dynamic pattern match
  for (const { regex, crumbs } of DYNAMIC_PATTERNS) {
    if (regex.test(pathname)) {
      return crumbs;
    }
  }

  return [];
}
