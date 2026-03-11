export interface BreadcrumbItem {
  /** i18n key (e.g. 'nav:menu.fileUpload') or plain text for dynamic items */
  labelKey: string;
  path?: string;
}

const ROUTE_BREADCRUMBS: Record<string, BreadcrumbItem[]> = {
  '/': [],

  // 인증서 관리
  '/icao': [{ labelKey: 'nav:sections.certManagement' }, { labelKey: 'nav:menu.icaoVersion' }],
  '/upload': [{ labelKey: 'nav:sections.certManagement' }, { labelKey: 'nav:menu.fileUpload' }],
  '/upload/certificate': [{ labelKey: 'nav:sections.certManagement' }, { labelKey: 'nav:menu.certUpload' }],
  '/pkd/certificates': [{ labelKey: 'nav:sections.certManagement' }, { labelKey: 'nav:menu.certSearch' }],
  '/upload-history': [{ labelKey: 'nav:sections.certManagement' }, { labelKey: 'nav:menu.uploadHistory' }],
  '/sync': [{ labelKey: 'nav:sections.certManagement' }, { labelKey: 'nav:menu.syncStatus' }],

  // 위·변조 검사
  '/pa/verify': [{ labelKey: 'nav:sections.forgeryDetection' }, { labelKey: 'nav:menu.paVerify' }],
  '/pa/history': [{ labelKey: 'nav:sections.forgeryDetection' }, { labelKey: 'nav:menu.verifyHistory' }],

  // 보고서 & 분석
  '/upload-dashboard': [{ labelKey: 'nav:sections.reportsAnalysis' }, { labelKey: 'nav:menu.certReports' }, { labelKey: 'nav:menu.certStats' }],
  '/pkd/trust-chain': [{ labelKey: 'nav:sections.reportsAnalysis' }, { labelKey: 'nav:menu.certReports' }, { labelKey: 'nav:menu.dscTrustChain' }],
  '/pkd/crl': [{ labelKey: 'nav:sections.reportsAnalysis' }, { labelKey: 'nav:menu.certReports' }, { labelKey: 'nav:menu.crlReport' }],
  '/pkd/dsc-nc': [{ labelKey: 'nav:sections.reportsAnalysis' }, { labelKey: 'nav:menu.certReports' }, { labelKey: 'nav:menu.nonConformantDsc' }],
  '/pa/dashboard': [{ labelKey: 'nav:sections.reportsAnalysis' }, { labelKey: 'nav:menu.paStats' }],
  '/ai/analysis': [{ labelKey: 'nav:sections.reportsAnalysis' }, { labelKey: 'nav:menu.aiAnalysis' }],

  // 시스템 관리
  '/monitoring': [{ labelKey: 'nav:sections.systemAdmin' }, { labelKey: 'nav:menu.systemMonitoring' }],
  '/admin/users': [{ labelKey: 'nav:sections.systemAdmin' }, { labelKey: 'nav:menu.userManagement' }],
  '/admin/api-clients': [{ labelKey: 'nav:sections.systemAdmin' }, { labelKey: 'nav:menu.apiClients' }],
  '/admin/operation-audit': [{ labelKey: 'nav:sections.systemAdmin' }, { labelKey: 'nav:menu.operationAudit' }],
  '/admin/audit-log': [{ labelKey: 'nav:sections.systemAdmin' }, { labelKey: 'nav:menu.authAudit' }],
  '/admin/pending-dsc': [{ labelKey: 'nav:sections.certManagement' }, { labelKey: 'nav:menu.pendingDsc' }],

  // Profile
  '/profile': [{ labelKey: 'nav:breadcrumb.profile' }],
};

/** Dynamic route patterns */
const DYNAMIC_PATTERNS: { regex: RegExp; crumbs: BreadcrumbItem[] }[] = [
  {
    regex: /^\/upload\/(?!certificate)[^/]+$/,
    crumbs: [
      { labelKey: 'nav:sections.certManagement' },
      { labelKey: 'nav:menu.uploadHistory', path: '/upload-history' },
      { labelKey: 'nav:breadcrumb.detail' },
    ],
  },
  {
    regex: /^\/pa\/(?!verify|history|dashboard)[^/]+$/,
    crumbs: [
      { labelKey: 'nav:sections.forgeryDetection' },
      { labelKey: 'nav:menu.verifyHistory', path: '/pa/history' },
      { labelKey: 'nav:breadcrumb.detail' },
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
