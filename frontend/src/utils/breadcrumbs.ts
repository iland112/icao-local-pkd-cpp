export interface BreadcrumbItem {
  label: string;
  path?: string;
}

const ROUTE_BREADCRUMBS: Record<string, BreadcrumbItem[]> = {
  '/': [],

  // PKD Management
  '/icao': [{ label: 'PKD Management' }, { label: 'ICAO 버전 상태' }],
  '/upload': [{ label: 'PKD Management' }, { label: '파일 업로드' }],
  '/upload/certificate': [{ label: 'PKD Management' }, { label: '인증서 업로드' }],
  '/pkd/certificates': [{ label: 'PKD Management' }, { label: '인증서 조회' }],
  '/pkd/trust-chain': [{ label: 'PKD Management' }, { label: '보고서' }, { label: 'DSC Trust Chain 보고서' }],
  '/pkd/crl': [{ label: 'PKD Management' }, { label: '보고서' }, { label: 'CRL 보고서' }],
  '/pkd/dsc-nc': [{ label: 'PKD Management' }, { label: '보고서' }, { label: '표준 부적합 DSC 보고서' }],
  '/ai/analysis': [{ label: 'PKD Management' }, { label: '보고서' }, { label: 'AI 인증서 분석' }],
  '/upload-history': [{ label: 'PKD Management' }, { label: '업로드 이력' }],
  '/sync': [{ label: 'PKD Management' }, { label: '동기화 상태' }],
  '/upload-dashboard': [{ label: 'PKD Management' }, { label: '통계 대시보드' }],

  // Passive Auth
  '/pa/verify': [{ label: 'Passive Auth' }, { label: 'PA 검증 수행' }],
  '/pa/history': [{ label: 'Passive Auth' }, { label: '검증 이력' }],
  '/pa/dashboard': [{ label: 'Passive Auth' }, { label: '통계 대시보드' }],

  // System & Admin
  '/monitoring': [{ label: 'System & Admin' }, { label: '시스템 모니터링' }],
  '/admin/users': [{ label: 'System & Admin' }, { label: '사용자 관리' }],
  '/admin/api-clients': [{ label: 'System & Admin' }, { label: 'API 클라이언트' }],
  '/admin/operation-audit': [{ label: 'System & Admin' }, { label: '운영 감사 로그' }],
  '/admin/audit-log': [{ label: 'System & Admin' }, { label: '인증 감사 로그' }],

  // Profile
  '/profile': [{ label: '프로필' }],
};

/** Dynamic route patterns */
const DYNAMIC_PATTERNS: { regex: RegExp; crumbs: BreadcrumbItem[] }[] = [
  {
    regex: /^\/upload\/(?!certificate)[^/]+$/,
    crumbs: [
      { label: 'PKD Management' },
      { label: '업로드 이력', path: '/upload-history' },
      { label: '상세' },
    ],
  },
  {
    regex: /^\/pa\/(?!verify|history|dashboard)[^/]+$/,
    crumbs: [
      { label: 'Passive Auth' },
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
