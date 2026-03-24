import { describe, it, expect } from 'vitest';
import { getBreadcrumbs, type BreadcrumbItem } from '../breadcrumbs';

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
function labels(items: BreadcrumbItem[]): string[] {
  return items.map(i => i.labelKey);
}

function paths(items: BreadcrumbItem[]): (string | undefined)[] {
  return items.map(i => i.path);
}

// ---------------------------------------------------------------------------
// Root route
// ---------------------------------------------------------------------------
describe('getBreadcrumbs — root route', () => {
  it('should return empty array for "/"', () => {
    expect(getBreadcrumbs('/')).toEqual([]);
  });
});

// ---------------------------------------------------------------------------
// Static routes — 인증서 관리 (Certificate Management)
// ---------------------------------------------------------------------------
describe('getBreadcrumbs — 인증서 관리 routes', () => {
  it('/icao — ICAO version page has 2 breadcrumbs', () => {
    const crumbs = getBreadcrumbs('/icao');
    expect(crumbs).toHaveLength(2);
    expect(labels(crumbs)).toEqual(['nav:sections.certManagement', 'nav:menu.icaoVersion']);
  });

  it('/upload — file upload page has 2 breadcrumbs', () => {
    const crumbs = getBreadcrumbs('/upload');
    expect(crumbs).toHaveLength(2);
    expect(labels(crumbs)).toEqual(['nav:sections.certManagement', 'nav:menu.fileUpload']);
  });

  it('/upload/certificate — cert upload page has 2 breadcrumbs', () => {
    const crumbs = getBreadcrumbs('/upload/certificate');
    expect(crumbs).toHaveLength(2);
    expect(labels(crumbs)).toEqual(['nav:sections.certManagement', 'nav:menu.certUpload']);
  });

  it('/pkd/certificates — cert search page has 2 breadcrumbs', () => {
    const crumbs = getBreadcrumbs('/pkd/certificates');
    expect(crumbs).toHaveLength(2);
    expect(labels(crumbs)).toEqual(['nav:sections.certManagement', 'nav:menu.certSearch']);
  });

  it('/upload-history — upload history page has 2 breadcrumbs', () => {
    const crumbs = getBreadcrumbs('/upload-history');
    expect(crumbs).toHaveLength(2);
    expect(labels(crumbs)).toEqual(['nav:sections.certManagement', 'nav:menu.uploadHistory']);
  });

  it('/sync — sync status page has 2 breadcrumbs', () => {
    const crumbs = getBreadcrumbs('/sync');
    expect(crumbs).toHaveLength(2);
    expect(labels(crumbs)).toEqual(['nav:sections.certManagement', 'nav:menu.syncStatus']);
  });

  it('/admin/pending-dsc — pending DSC page under certManagement', () => {
    const crumbs = getBreadcrumbs('/admin/pending-dsc');
    expect(crumbs).toHaveLength(2);
    expect(labels(crumbs)[0]).toBe('nav:sections.certManagement');
    expect(labels(crumbs)[1]).toBe('nav:menu.pendingDsc');
  });
});

// ---------------------------------------------------------------------------
// Static routes — 위·변조 검사 (Forgery Detection)
// ---------------------------------------------------------------------------
describe('getBreadcrumbs — 위·변조 검사 routes', () => {
  it('/pa/verify — PA verify page has 2 breadcrumbs', () => {
    const crumbs = getBreadcrumbs('/pa/verify');
    expect(crumbs).toHaveLength(2);
    expect(labels(crumbs)).toEqual(['nav:sections.forgeryDetection', 'nav:menu.paVerify']);
  });

  it('/pa/history — PA history page has 2 breadcrumbs', () => {
    const crumbs = getBreadcrumbs('/pa/history');
    expect(crumbs).toHaveLength(2);
    expect(labels(crumbs)).toEqual(['nav:sections.forgeryDetection', 'nav:menu.verifyHistory']);
  });
});

// ---------------------------------------------------------------------------
// Static routes — 보고서 & 분석 (Reports & Analysis)
// ---------------------------------------------------------------------------
describe('getBreadcrumbs — 보고서 & 분석 routes', () => {
  it('/upload-dashboard — cert stats has 3 breadcrumbs (nested)', () => {
    const crumbs = getBreadcrumbs('/upload-dashboard');
    expect(crumbs).toHaveLength(3);
    expect(labels(crumbs)).toEqual([
      'nav:sections.reportsAnalysis',
      'nav:menu.certReports',
      'nav:menu.certStats',
    ]);
  });

  it('/pkd/trust-chain — trust chain report has 3 breadcrumbs', () => {
    const crumbs = getBreadcrumbs('/pkd/trust-chain');
    expect(crumbs).toHaveLength(3);
    expect(labels(crumbs)[2]).toBe('nav:menu.dscTrustChain');
  });

  it('/pkd/crl — CRL report has 3 breadcrumbs', () => {
    const crumbs = getBreadcrumbs('/pkd/crl');
    expect(crumbs).toHaveLength(3);
    expect(labels(crumbs)[2]).toBe('nav:menu.crlReport');
  });

  it('/pkd/dsc-nc — DSC NC report has 3 breadcrumbs', () => {
    const crumbs = getBreadcrumbs('/pkd/dsc-nc');
    expect(crumbs).toHaveLength(3);
    expect(labels(crumbs)[2]).toBe('nav:menu.nonConformantDsc');
  });

  it('/pa/dashboard — PA stats has 2 breadcrumbs', () => {
    const crumbs = getBreadcrumbs('/pa/dashboard');
    expect(crumbs).toHaveLength(2);
    expect(labels(crumbs)).toEqual(['nav:sections.reportsAnalysis', 'nav:menu.paStats']);
  });

  it('/ai/analysis — AI analysis has 2 breadcrumbs', () => {
    const crumbs = getBreadcrumbs('/ai/analysis');
    expect(crumbs).toHaveLength(2);
    expect(labels(crumbs)).toEqual(['nav:sections.reportsAnalysis', 'nav:menu.aiAnalysis']);
  });
});

// ---------------------------------------------------------------------------
// Static routes — 시스템 관리 (System Admin)
// ---------------------------------------------------------------------------
describe('getBreadcrumbs — 시스템 관리 routes', () => {
  it('/monitoring — system monitoring has 2 breadcrumbs', () => {
    const crumbs = getBreadcrumbs('/monitoring');
    expect(crumbs).toHaveLength(2);
    expect(labels(crumbs)).toEqual(['nav:sections.systemAdmin', 'nav:menu.systemMonitoring']);
  });

  it('/admin/users — user management has 2 breadcrumbs', () => {
    const crumbs = getBreadcrumbs('/admin/users');
    expect(crumbs).toHaveLength(2);
    expect(labels(crumbs)).toEqual(['nav:sections.systemAdmin', 'nav:menu.userManagement']);
  });

  it('/admin/api-clients — API clients has 2 breadcrumbs', () => {
    const crumbs = getBreadcrumbs('/admin/api-clients');
    expect(crumbs).toHaveLength(2);
    expect(labels(crumbs)).toEqual(['nav:sections.systemAdmin', 'nav:menu.apiClients']);
  });

  it('/admin/operation-audit — operation audit has 2 breadcrumbs', () => {
    const crumbs = getBreadcrumbs('/admin/operation-audit');
    expect(crumbs).toHaveLength(2);
    expect(labels(crumbs)).toEqual(['nav:sections.systemAdmin', 'nav:menu.operationAudit']);
  });

  it('/admin/audit-log — auth audit log has 2 breadcrumbs', () => {
    const crumbs = getBreadcrumbs('/admin/audit-log');
    expect(crumbs).toHaveLength(2);
    expect(labels(crumbs)).toEqual(['nav:sections.systemAdmin', 'nav:menu.authAudit']);
  });
});

// ---------------------------------------------------------------------------
// Static routes — Profile
// ---------------------------------------------------------------------------
describe('getBreadcrumbs — profile route', () => {
  it('/profile — profile page has 1 breadcrumb', () => {
    const crumbs = getBreadcrumbs('/profile');
    expect(crumbs).toHaveLength(1);
    expect(labels(crumbs)).toEqual(['nav:breadcrumb.profile']);
  });
});

// ---------------------------------------------------------------------------
// Dynamic routes — upload detail  (/upload/<id>)
// ---------------------------------------------------------------------------
describe('getBreadcrumbs — dynamic upload detail routes', () => {
  it('/upload/abc123 — matches dynamic upload detail pattern', () => {
    const crumbs = getBreadcrumbs('/upload/abc123');
    expect(crumbs).toHaveLength(3);
    expect(labels(crumbs)).toEqual([
      'nav:sections.certManagement',
      'nav:menu.uploadHistory',
      'nav:breadcrumb.detail',
    ]);
  });

  it('/upload/abc123 — middle breadcrumb has path "/upload-history"', () => {
    const crumbs = getBreadcrumbs('/upload/abc123');
    expect(crumbs[1].path).toBe('/upload-history');
  });

  it('/upload/some-uuid-1234 — UUID-style upload ID is matched', () => {
    const crumbs = getBreadcrumbs('/upload/550e8400-e29b-41d4-a716-446655440000');
    expect(crumbs).toHaveLength(3);
    expect(labels(crumbs)[2]).toBe('nav:breadcrumb.detail');
  });

  it('/upload/certificate — static route takes precedence over dynamic pattern', () => {
    // /upload/certificate is a static route, NOT a dynamic detail page
    const crumbs = getBreadcrumbs('/upload/certificate');
    expect(labels(crumbs)).toContain('nav:menu.certUpload');
    expect(labels(crumbs)).not.toContain('nav:breadcrumb.detail');
  });

  it('/upload/certificate/extra — route with extra segment returns empty (unknown route)', () => {
    // Two-segment path is not matched by the single-segment regex
    const crumbs = getBreadcrumbs('/upload/certificate/extra');
    expect(crumbs).toEqual([]);
  });
});

// ---------------------------------------------------------------------------
// Dynamic routes — PA detail  (/pa/<id>)
// ---------------------------------------------------------------------------
describe('getBreadcrumbs — dynamic PA detail routes', () => {
  it('/pa/abc123 — matches dynamic PA detail pattern', () => {
    const crumbs = getBreadcrumbs('/pa/abc123');
    expect(crumbs).toHaveLength(3);
    expect(labels(crumbs)).toEqual([
      'nav:sections.forgeryDetection',
      'nav:menu.verifyHistory',
      'nav:breadcrumb.detail',
    ]);
  });

  it('/pa/abc123 — middle breadcrumb has path "/pa/history"', () => {
    const crumbs = getBreadcrumbs('/pa/abc123');
    expect(crumbs[1].path).toBe('/pa/history');
  });

  it('/pa/verify — static route takes precedence over dynamic PA pattern', () => {
    const crumbs = getBreadcrumbs('/pa/verify');
    expect(labels(crumbs)).toContain('nav:menu.paVerify');
    expect(labels(crumbs)).not.toContain('nav:breadcrumb.detail');
  });

  it('/pa/history — static route takes precedence over dynamic PA pattern', () => {
    const crumbs = getBreadcrumbs('/pa/history');
    expect(labels(crumbs)).toContain('nav:menu.verifyHistory');
    expect(labels(crumbs)).not.toContain('nav:breadcrumb.detail');
  });

  it('/pa/dashboard — static route takes precedence over dynamic PA pattern', () => {
    const crumbs = getBreadcrumbs('/pa/dashboard');
    expect(labels(crumbs)).toContain('nav:menu.paStats');
    expect(labels(crumbs)).not.toContain('nav:breadcrumb.detail');
  });
});

// ---------------------------------------------------------------------------
// Unknown / unregistered routes
// ---------------------------------------------------------------------------
describe('getBreadcrumbs — unknown routes', () => {
  it('should return empty array for a completely unknown path', () => {
    expect(getBreadcrumbs('/nonexistent')).toEqual([]);
  });

  it('should return empty array for "/admin/csr" (not yet in static map)', () => {
    // This route may or may not be added; as of the current source it is absent
    const crumbs = getBreadcrumbs('/admin/csr');
    // It is not in the static map and does not match any dynamic pattern
    expect(crumbs).toEqual([]);
  });

  it('should return empty array for an empty string path', () => {
    expect(getBreadcrumbs('')).toEqual([]);
  });

  it('should return empty array for a path with only a trailing slash that is not registered', () => {
    expect(getBreadcrumbs('/upload/')).toEqual([]);
  });
});

// ---------------------------------------------------------------------------
// BreadcrumbItem shape
// ---------------------------------------------------------------------------
describe('getBreadcrumbs — returned item shape', () => {
  it('items without an explicit path should have path === undefined', () => {
    const crumbs = getBreadcrumbs('/icao');
    // Neither breadcrumb in the /icao route has a path property defined
    crumbs.forEach(c => {
      expect(c.path).toBeUndefined();
    });
  });

  it('items with an explicit path should have a string path', () => {
    // /upload/<id> breadcrumb[1] has path: '/upload-history'
    const crumbs = getBreadcrumbs('/upload/someid');
    const withPath = crumbs.filter(c => c.path !== undefined);
    expect(withPath.length).toBeGreaterThan(0);
    withPath.forEach(c => expect(typeof c.path).toBe('string'));
  });

  it('idempotency — repeated calls with the same path return equal arrays', () => {
    const a = getBreadcrumbs('/pkd/crl');
    const b = getBreadcrumbs('/pkd/crl');
    expect(a).toEqual(b);
  });
});
