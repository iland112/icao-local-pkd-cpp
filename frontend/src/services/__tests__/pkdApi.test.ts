/**
 * Tests for pkdApi.ts
 *
 * pkdApi.ts creates one axios instance (pkdApi) and builds several API
 * namespaces on top of it (healthApi, certificateApi, uploadHistoryApi,
 * ldapApi, icaoApi).  We mock axios.create so every namespace delegates
 * to our mock instance.
 */

import { describe, it, expect, vi, beforeEach } from 'vitest';

const { mockInstance, capturedInterceptors } = vi.hoisted(() => {
  const capturedInterceptors: {
    requestFulfilled?: (config: Record<string, unknown>) => Record<string, unknown>;
    responseErrorHandler?: (error: unknown) => unknown;
  } = {};

  const instance = {
    get: vi.fn(),
    post: vi.fn(),
    delete: vi.fn(),
    interceptors: {
      request: {
        use: vi.fn((fulfilled: (c: Record<string, unknown>) => Record<string, unknown>) => {
          capturedInterceptors.requestFulfilled = fulfilled;
        }),
      },
      response: {
        use: vi.fn((_fulfilled: unknown, rejected: (e: unknown) => unknown) => {
          capturedInterceptors.responseErrorHandler = rejected;
        }),
      },
    },
  };

  return { mockInstance: instance, capturedInterceptors };
});

vi.mock('axios', () => ({
  default: {
    create: vi.fn(() => mockInstance),
  },
}));

import { healthApi, certificateApi, uploadHistoryApi, ldapApi, icaoApi } from '../pkdApi';

beforeEach(() => {
  vi.clearAllMocks();
  localStorage.clear();
});

// ===========================================================================
// healthApi
// ===========================================================================

describe('healthApi.check', () => {
  it('should GET /health', () => {
    healthApi.check();
    expect(mockInstance.get).toHaveBeenCalledWith('/health');
  });
});

describe('healthApi.checkDatabase', () => {
  it('should GET /health/database', () => {
    healthApi.checkDatabase();
    expect(mockInstance.get).toHaveBeenCalledWith('/health/database');
  });
});

describe('healthApi.checkLdap', () => {
  it('should GET /health/ldap', () => {
    healthApi.checkLdap();
    expect(mockInstance.get).toHaveBeenCalledWith('/health/ldap');
  });
});

// ===========================================================================
// certificateApi
// ===========================================================================

describe('certificateApi.search', () => {
  it('should GET /certificates/search with provided params', () => {
    const params = { country: 'KR', certType: 'DSC' as const };
    certificateApi.search(params);
    expect(mockInstance.get).toHaveBeenCalledWith('/certificates/search', { params });
  });

  it('should pass all filter params through unchanged', () => {
    const params = { country: 'DE', certType: 'CSCA' as const, validationStatus: 'VALID' as const, text: 'test', limit: 50, offset: 0 };
    certificateApi.search(params);
    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params).toEqual(params);
  });
});

describe('certificateApi.getCountries', () => {
  it('should GET /certificates/countries', () => {
    certificateApi.getCountries();
    expect(mockInstance.get).toHaveBeenCalledWith('/certificates/countries');
  });
});

describe('certificateApi.getDetail', () => {
  it('should GET /certificates/detail with dn param', () => {
    certificateApi.getDetail('cn=test,c=KR');
    expect(mockInstance.get).toHaveBeenCalledWith('/certificates/detail', {
      params: { dn: 'cn=test,c=KR' },
    });
  });
});

describe('certificateApi.exportFile', () => {
  it('should GET /certificates/export/file with dn and default DER format as blob', () => {
    certificateApi.exportFile('cn=test,c=KR');
    expect(mockInstance.get).toHaveBeenCalledWith('/certificates/export/file', {
      params: { dn: 'cn=test,c=KR', format: 'DER' },
      responseType: 'blob',
    });
  });

  it('should use the specified format', () => {
    certificateApi.exportFile('cn=test,c=KR', 'PEM');
    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.format).toBe('PEM');
  });
});

describe('certificateApi.exportCountry', () => {
  it('should GET /certificates/export/country with country and default DER format', () => {
    certificateApi.exportCountry('KR');
    expect(mockInstance.get).toHaveBeenCalledWith('/certificates/export/country', {
      params: { country: 'KR', format: 'DER' },
      responseType: 'blob',
    });
  });

  it('should pass PEM format when specified', () => {
    certificateApi.exportCountry('US', 'PEM');
    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.format).toBe('PEM');
  });
});

describe('certificateApi.exportAll', () => {
  it('should GET /certificates/export/all with default PEM format and long timeout', () => {
    certificateApi.exportAll();
    expect(mockInstance.get).toHaveBeenCalledWith('/certificates/export/all', {
      params: { format: 'PEM' },
      responseType: 'blob',
      timeout: 300000,
    });
  });

  it('should use DER format when specified', () => {
    certificateApi.exportAll('DER');
    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.format).toBe('DER');
  });
});

describe('certificateApi.getDscNcReport', () => {
  it('should GET /certificates/dsc-nc/report with no params when called without args', () => {
    certificateApi.getDscNcReport();
    expect(mockInstance.get).toHaveBeenCalledWith('/certificates/dsc-nc/report', { params: undefined });
  });

  it('should pass filters when provided', () => {
    const params = { country: 'KR', conformanceCode: 'A001', page: 2, size: 10 };
    certificateApi.getDscNcReport(params);
    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params).toEqual(params);
  });
});

describe('certificateApi.getCrlReport', () => {
  it('should GET /certificates/crl/report with no params when called without args', () => {
    certificateApi.getCrlReport();
    expect(mockInstance.get).toHaveBeenCalledWith('/certificates/crl/report', { params: undefined });
  });

  it('should pass country and status filters', () => {
    certificateApi.getCrlReport({ country: 'DE', status: 'VALID' });
    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params).toEqual({ country: 'DE', status: 'VALID' });
  });
});

describe('certificateApi.getCrlDetail', () => {
  it('should GET /certificates/crl/{id}', () => {
    certificateApi.getCrlDetail('crl-uuid-001');
    expect(mockInstance.get).toHaveBeenCalledWith('/certificates/crl/crl-uuid-001');
  });
});

describe('certificateApi.downloadCrl', () => {
  it('should GET /certificates/crl/{id}/download as blob', () => {
    certificateApi.downloadCrl('crl-uuid-002');
    expect(mockInstance.get).toHaveBeenCalledWith(
      '/certificates/crl/crl-uuid-002/download',
      { responseType: 'blob' }
    );
  });
});

describe('certificateApi.getDoc9303Checklist', () => {
  it('should GET /certificates/doc9303-checklist with fingerprint param', () => {
    certificateApi.getDoc9303Checklist('abc123fingerprint');
    expect(mockInstance.get).toHaveBeenCalledWith('/certificates/doc9303-checklist', {
      params: { fingerprint: 'abc123fingerprint' },
    });
  });
});

describe('certificateApi.getQualityReport', () => {
  it('should GET /certificates/quality/report with no params when called without args', () => {
    certificateApi.getQualityReport();
    expect(mockInstance.get).toHaveBeenCalledWith('/certificates/quality/report', { params: undefined });
  });

  it('should pass filters when provided', () => {
    const params = { country: 'KR', certType: 'DSC', category: 'algorithm', page: 1, size: 20 };
    certificateApi.getQualityReport(params);
    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params).toEqual(params);
  });
});

// ===========================================================================
// uploadHistoryApi
// ===========================================================================

describe('uploadHistoryApi.getHistory', () => {
  it('should GET /upload/history with page params', () => {
    const params = { page: 1, size: 20 };
    uploadHistoryApi.getHistory(params as never);
    expect(mockInstance.get).toHaveBeenCalledWith('/upload/history', { params });
  });
});

describe('uploadHistoryApi.getDetail', () => {
  it('should GET /upload/detail/{uploadId}', () => {
    uploadHistoryApi.getDetail('upload-uuid-001');
    expect(mockInstance.get).toHaveBeenCalledWith('/upload/detail/upload-uuid-001');
  });
});

describe('uploadHistoryApi.getIssues', () => {
  it('should GET /upload/{uploadId}/issues', () => {
    uploadHistoryApi.getIssues('upload-uuid-002');
    expect(mockInstance.get).toHaveBeenCalledWith('/upload/upload-uuid-002/issues');
  });
});

describe('uploadHistoryApi.getStatistics', () => {
  it('should GET /upload/statistics', () => {
    uploadHistoryApi.getStatistics();
    expect(mockInstance.get).toHaveBeenCalledWith('/upload/statistics');
  });
});

describe('uploadHistoryApi.getValidationReasons', () => {
  it('should GET /upload/statistics/validation-reasons', () => {
    uploadHistoryApi.getValidationReasons();
    expect(mockInstance.get).toHaveBeenCalledWith('/upload/statistics/validation-reasons');
  });
});

describe('uploadHistoryApi.getCountryStatistics', () => {
  it('should GET /upload/countries with default limit=20', () => {
    uploadHistoryApi.getCountryStatistics();
    expect(mockInstance.get).toHaveBeenCalledWith('/upload/countries', { params: { limit: 20 } });
  });

  it('should pass custom limit', () => {
    uploadHistoryApi.getCountryStatistics(50);
    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.limit).toBe(50);
  });
});

describe('uploadHistoryApi.getDetailedCountryStatistics', () => {
  it('should GET /upload/countries/detailed with default limit=0', () => {
    uploadHistoryApi.getDetailedCountryStatistics();
    expect(mockInstance.get).toHaveBeenCalledWith('/upload/countries/detailed', { params: { limit: 0 } });
  });

  it('should pass custom limit', () => {
    uploadHistoryApi.getDetailedCountryStatistics(100);
    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.limit).toBe(100);
  });
});

describe('uploadHistoryApi.getChanges', () => {
  it('should GET /upload/changes with default limit=10', () => {
    uploadHistoryApi.getChanges();
    expect(mockInstance.get).toHaveBeenCalledWith('/upload/changes', { params: { limit: 10 } });
  });

  it('should pass custom limit', () => {
    uploadHistoryApi.getChanges(5);
    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.limit).toBe(5);
  });
});

describe('uploadHistoryApi.getLdifStructure', () => {
  it('should GET /upload/{uploadId}/ldif-structure with default maxEntries=100', () => {
    uploadHistoryApi.getLdifStructure('upload-uuid-003');
    expect(mockInstance.get).toHaveBeenCalledWith(
      '/upload/upload-uuid-003/ldif-structure',
      { params: { maxEntries: 100 } }
    );
  });

  it('should pass custom maxEntries', () => {
    uploadHistoryApi.getLdifStructure('upload-uuid-003', 500);
    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.maxEntries).toBe(500);
  });
});

// ===========================================================================
// ldapApi
// ===========================================================================

describe('ldapApi.getHealth', () => {
  it('should GET /health/ldap', () => {
    ldapApi.getHealth();
    expect(mockInstance.get).toHaveBeenCalledWith('/health/ldap');
  });
});

describe('ldapApi.getStatistics', () => {
  it('should GET /ldap/statistics', () => {
    ldapApi.getStatistics();
    expect(mockInstance.get).toHaveBeenCalledWith('/ldap/statistics');
  });
});

describe('ldapApi.searchCertificates', () => {
  it('should GET /ldap/certificates with params', () => {
    ldapApi.searchCertificates({ country: 'KR', type: 'DSC' });
    expect(mockInstance.get).toHaveBeenCalledWith('/ldap/certificates', {
      params: { country: 'KR', type: 'DSC' },
    });
  });
});

describe('ldapApi.getCertificateByFingerprint', () => {
  it('should GET /ldap/certificates/{fingerprint}', () => {
    ldapApi.getCertificateByFingerprint('fp456');
    expect(mockInstance.get).toHaveBeenCalledWith('/ldap/certificates/fp456');
  });
});

describe('ldapApi.searchCrls', () => {
  it('should GET /ldap/crls with params', () => {
    ldapApi.searchCrls({ country: 'US' });
    expect(mockInstance.get).toHaveBeenCalledWith('/ldap/crls', { params: { country: 'US' } });
  });
});

describe('ldapApi.checkRevocation', () => {
  it('should GET /ldap/revocation/check with serialNumber and issuerDn', () => {
    ldapApi.checkRevocation({ serialNumber: '01:02:03', issuerDn: 'CN=CA,C=KR' });
    expect(mockInstance.get).toHaveBeenCalledWith('/ldap/revocation/check', {
      params: { serialNumber: '01:02:03', issuerDn: 'CN=CA,C=KR' },
    });
  });
});

// ===========================================================================
// icaoApi
// ===========================================================================

describe('icaoApi.getStatus', () => {
  it('should GET /icao/status', () => {
    icaoApi.getStatus();
    expect(mockInstance.get).toHaveBeenCalledWith('/icao/status');
  });
});

describe('icaoApi.getHistory', () => {
  it('should GET /icao/history with default limit=10', () => {
    icaoApi.getHistory();
    expect(mockInstance.get).toHaveBeenCalledWith('/icao/history', { params: { limit: 10 } });
  });

  it('should pass custom limit', () => {
    icaoApi.getHistory(50);
    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.limit).toBe(50);
  });
});

describe('icaoApi.checkUpdates', () => {
  it('should GET /icao/check-updates', () => {
    icaoApi.checkUpdates();
    expect(mockInstance.get).toHaveBeenCalledWith('/icao/check-updates');
  });
});

describe('icaoApi.getLatest', () => {
  it('should GET /icao/latest', () => {
    icaoApi.getLatest();
    expect(mockInstance.get).toHaveBeenCalledWith('/icao/latest');
  });
});

// ===========================================================================
// Request interceptor: JWT injection
// ===========================================================================

describe('pkdApi request interceptor', () => {
  it('should have registered a request interceptor at module load time', () => {
    expect(capturedInterceptors.requestFulfilled).toBeTypeOf('function');
  });

  it('should inject Authorization header when access_token is present', () => {
    localStorage.setItem('access_token', 'pkd-test-jwt');
    const config = { headers: {} as Record<string, string> };
    const result = capturedInterceptors.requestFulfilled!(config as never) as typeof config;
    expect(result.headers.Authorization).toBe('Bearer pkd-test-jwt');
  });

  it('should not set Authorization when no token is stored', () => {
    const config = { headers: {} as Record<string, string> };
    const result = capturedInterceptors.requestFulfilled!(config as never) as typeof config;
    expect(result.headers.Authorization).toBeUndefined();
  });
});

// ===========================================================================
// Response interceptor: 401 handling
// ===========================================================================

describe('pkdApi response interceptor (401 handling)', () => {
  it('should have registered a response error handler', () => {
    expect(capturedInterceptors.responseErrorHandler).toBeTypeOf('function');
  });

  it('should clear auth and dispatch auth:expired on 401 outside /login', async () => {
    localStorage.setItem('access_token', 'token');
    localStorage.setItem('user', '{}');
    const dispatchSpy = vi.spyOn(window, 'dispatchEvent');
    Object.defineProperty(window, 'location', { value: { pathname: '/dashboard' }, writable: true });

    const error = { response: { status: 401 } };
    await expect(capturedInterceptors.responseErrorHandler!(error)).rejects.toEqual(error);

    expect(localStorage.getItem('access_token')).toBeNull();
    expect(localStorage.getItem('user')).toBeNull();
    expect(dispatchSpy).toHaveBeenCalled();
    dispatchSpy.mockRestore();
  });

  it('should not clear auth when 401 occurs on /login page', async () => {
    localStorage.setItem('access_token', 'token');
    Object.defineProperty(window, 'location', { value: { pathname: '/login' }, writable: true });

    const error = { response: { status: 401 } };
    await expect(capturedInterceptors.responseErrorHandler!(error)).rejects.toEqual(error);

    expect(localStorage.getItem('access_token')).toBe('token');
  });

  it('should re-reject non-401 errors without side effects', async () => {
    const error = { response: { status: 500 } };
    await expect(capturedInterceptors.responseErrorHandler!(error)).rejects.toEqual(error);
  });
});
