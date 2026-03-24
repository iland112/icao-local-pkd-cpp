import { describe, it, expect, vi, beforeEach } from 'vitest';

// ---------------------------------------------------------------------------
// Mock i18n BEFORE importing permissions so the module-level i18n.t calls
// (inside getPermissionLabel) use our stub, not the real i18next instance.
// ---------------------------------------------------------------------------
vi.mock('@/i18n', () => ({
  default: {
    t: (key: string) => `[t:${key}]`,
  },
}));
vi.mock('../../../i18n/index', () => ({
  default: {
    t: (key: string) => `[t:${key}]`,
  },
}));

import {
  PERMISSION_GROUPS,
  AVAILABLE_PERMISSIONS,
  getPermissionLabel,
  type PermissionDef,
  type PermissionGroup,
} from '../permissions';

// ---------------------------------------------------------------------------
// PERMISSION_GROUPS structure
// ---------------------------------------------------------------------------
describe('PERMISSION_GROUPS', () => {
  it('should export a non-empty array', () => {
    expect(PERMISSION_GROUPS).toBeInstanceOf(Array);
    expect(PERMISSION_GROUPS.length).toBeGreaterThan(0);
  });

  it('should have exactly 4 groups', () => {
    expect(PERMISSION_GROUPS).toHaveLength(4);
  });

  it('each group should have a label string', () => {
    PERMISSION_GROUPS.forEach((group: PermissionGroup) => {
      expect(typeof group.label).toBe('string');
      expect(group.label.length).toBeGreaterThan(0);
    });
  });

  it('each group should have a non-empty permissions array', () => {
    PERMISSION_GROUPS.forEach((group: PermissionGroup) => {
      expect(Array.isArray(group.permissions)).toBe(true);
      expect(group.permissions.length).toBeGreaterThan(0);
    });
  });

  it('every permission in every group should have value, label and desc', () => {
    PERMISSION_GROUPS.forEach((group: PermissionGroup) => {
      group.permissions.forEach((p: PermissionDef) => {
        expect(typeof p.value).toBe('string');
        expect(p.value.length).toBeGreaterThan(0);
        expect(typeof p.label).toBe('string');
        expect(p.label.length).toBeGreaterThan(0);
        expect(typeof p.desc).toBe('string');
        expect(p.desc.length).toBeGreaterThan(0);
      });
    });
  });

  it('first group (certManagement) should contain icao:read', () => {
    const first = PERMISSION_GROUPS[0];
    const values = first.permissions.map(p => p.value);
    expect(values).toContain('icao:read');
  });

  it('first group should contain upload:write, cert:read, cert:export, upload:read, sync:read', () => {
    const first = PERMISSION_GROUPS[0];
    const values = first.permissions.map(p => p.value);
    expect(values).toContain('upload:write');
    expect(values).toContain('cert:read');
    expect(values).toContain('cert:export');
    expect(values).toContain('upload:read');
    expect(values).toContain('sync:read');
  });

  it('second group (forgeryDetection) should contain pa:verify and pa:read', () => {
    const second = PERMISSION_GROUPS[1];
    const values = second.permissions.map(p => p.value);
    expect(values).toContain('pa:verify');
    expect(values).toContain('pa:read');
  });

  it('third group (reportAnalysis) should contain report:read, pa:stats, ai:read', () => {
    const third = PERMISSION_GROUPS[2];
    const values = third.permissions.map(p => p.value);
    expect(values).toContain('report:read');
    expect(values).toContain('pa:stats');
    expect(values).toContain('ai:read');
  });

  it('fourth group (systemAdmin) should contain api-client:manage', () => {
    const fourth = PERMISSION_GROUPS[3];
    const values = fourth.permissions.map(p => p.value);
    expect(values).toContain('api-client:manage');
  });

  it('group labels should reference the admin namespace', () => {
    PERMISSION_GROUPS.forEach((group: PermissionGroup) => {
      expect(group.label).toMatch(/^admin:/);
    });
  });
});

// ---------------------------------------------------------------------------
// AVAILABLE_PERMISSIONS flat list
// ---------------------------------------------------------------------------
describe('AVAILABLE_PERMISSIONS', () => {
  it('should be a flat array of all permissions from all groups', () => {
    const totalFromGroups = PERMISSION_GROUPS.reduce(
      (sum, g) => sum + g.permissions.length,
      0
    );
    expect(AVAILABLE_PERMISSIONS).toHaveLength(totalFromGroups);
  });

  it('should contain all 12 known permission values', () => {
    const values = AVAILABLE_PERMISSIONS.map(p => p.value);
    const expected = [
      'icao:read',
      'upload:write',
      'cert:read',
      'cert:export',
      'upload:read',
      'sync:read',
      'pa:verify',
      'pa:read',
      'report:read',
      'pa:stats',
      'ai:read',
      'api-client:manage',
    ];
    expected.forEach(e => expect(values).toContain(e));
  });

  it('should not contain duplicate permission values', () => {
    const values = AVAILABLE_PERMISSIONS.map(p => p.value);
    const unique = new Set(values);
    expect(unique.size).toBe(values.length);
  });

  it('every entry should conform to the PermissionDef shape', () => {
    AVAILABLE_PERMISSIONS.forEach((p: PermissionDef) => {
      expect(typeof p.value).toBe('string');
      expect(typeof p.label).toBe('string');
      expect(typeof p.desc).toBe('string');
    });
  });
});

// ---------------------------------------------------------------------------
// getPermissionLabel
// ---------------------------------------------------------------------------
describe('getPermissionLabel', () => {
  it('should return the translated label for a known permission code', () => {
    // Our i18n mock returns "[t:<key>]" so we can verify the right key is passed
    const result = getPermissionLabel('cert:read');
    expect(result).toBe('[t:admin:userMgmt.perm.certRead]');
  });

  it('should return the translated label for icao:read', () => {
    expect(getPermissionLabel('icao:read')).toBe('[t:admin:userMgmt.perm.icaoRead]');
  });

  it('should return the translated label for upload:write', () => {
    expect(getPermissionLabel('upload:write')).toBe('[t:admin:userMgmt.perm.uploadWrite]');
  });

  it('should return the translated label for cert:export', () => {
    expect(getPermissionLabel('cert:export')).toBe('[t:admin:userMgmt.perm.certExport]');
  });

  it('should return the translated label for upload:read', () => {
    expect(getPermissionLabel('upload:read')).toBe('[t:admin:userMgmt.perm.uploadRead]');
  });

  it('should return the translated label for sync:read', () => {
    expect(getPermissionLabel('sync:read')).toBe('[t:admin:userMgmt.perm.syncRead]');
  });

  it('should return the translated label for pa:verify', () => {
    expect(getPermissionLabel('pa:verify')).toBe('[t:admin:userMgmt.perm.paVerify]');
  });

  it('should return the translated label for pa:read', () => {
    expect(getPermissionLabel('pa:read')).toBe('[t:admin:userMgmt.perm.paRead]');
  });

  it('should return the translated label for report:read', () => {
    expect(getPermissionLabel('report:read')).toBe('[t:admin:userMgmt.perm.reportRead]');
  });

  it('should return the translated label for pa:stats', () => {
    expect(getPermissionLabel('pa:stats')).toBe('[t:admin:userMgmt.perm.paStats]');
  });

  it('should return the translated label for ai:read', () => {
    expect(getPermissionLabel('ai:read')).toBe('[t:admin:userMgmt.perm.aiRead]');
  });

  it('should return the translated label for api-client:manage', () => {
    expect(getPermissionLabel('api-client:manage')).toBe('[t:admin:userMgmt.perm.apiClientManage]');
  });

  it('should return the raw code when the permission is not found', () => {
    expect(getPermissionLabel('unknown:perm')).toBe('unknown:perm');
  });

  it('should return the raw code for an empty string', () => {
    expect(getPermissionLabel('')).toBe('');
  });

  it('should return the raw code for a code that resembles a permission but is not registered', () => {
    expect(getPermissionLabel('cert:write')).toBe('cert:write');
  });

  it('should be case-sensitive — "CERT:READ" is not a known code', () => {
    expect(getPermissionLabel('CERT:READ')).toBe('CERT:READ');
  });

  it('should produce the same result on repeated calls (idempotency)', () => {
    const a = getPermissionLabel('pa:verify');
    const b = getPermissionLabel('pa:verify');
    expect(a).toBe(b);
  });
});
