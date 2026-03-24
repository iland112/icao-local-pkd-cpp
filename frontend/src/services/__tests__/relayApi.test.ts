/**
 * Tests for relayApi.ts (uploadApi, syncApi, createProgressEventSource, getProgressStatus)
 *
 * vi.mock() is hoisted — variables needed by the factory are declared with
 * vi.hoisted() so they exist before the factory runs.
 * EventSource is stubbed globally (jsdom does not provide it).
 */

import { describe, it, expect, vi, beforeEach } from 'vitest';

// ---- EventSource mock (jsdom does not provide EventSource) ----
class MockEventSource {
  url: string;
  constructor(url: string) { this.url = url; }
  close = vi.fn();
  addEventListener = vi.fn();
  removeEventListener = vi.fn();
}
vi.stubGlobal('EventSource', MockEventSource);

// ---- Hoist mock instance and interceptor callbacks ----
const { mockInstance, capturedInterceptors } = vi.hoisted(() => {
  const capturedInterceptors: {
    requestFulfilled?: (config: Record<string, unknown>) => Record<string, unknown>;
    responseRejected?: (error: unknown) => never;
  } = {};

  const instance = {
    get: vi.fn(),
    post: vi.fn(),
    put: vi.fn(),
    delete: vi.fn(),
    interceptors: {
      request: {
        use: vi.fn((fulfilled: (c: Record<string, unknown>) => Record<string, unknown>, _rejected: unknown) => {
          capturedInterceptors.requestFulfilled = fulfilled;
        }),
      },
      response: {
        use: vi.fn((_fulfilled: unknown, rejected: (e: unknown) => never) => {
          capturedInterceptors.responseRejected = rejected;
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

import { uploadApi, syncApi, createProgressEventSource, getProgressStatus } from '../relayApi';

beforeEach(() => {
  vi.clearAllMocks();
  localStorage.clear();

  Object.defineProperty(window, 'location', {
    value: { pathname: '/dashboard' },
    writable: true,
    configurable: true,
  });
});

// ---------------------------------------------------------------------------
// uploadApi.uploadLdif
// ---------------------------------------------------------------------------

describe('uploadApi.uploadLdif', () => {
  it('should POST to /upload/ldif with a FormData containing the file', () => {
    const file = new File(['content'], 'test.ldif', { type: 'text/plain' });

    uploadApi.uploadLdif(file);

    expect(mockInstance.post).toHaveBeenCalledOnce();
    const [url, formData] = mockInstance.post.mock.calls[0];
    expect(url).toBe('/upload/ldif');
    expect(formData).toBeInstanceOf(FormData);
    expect(formData.get('file')).toBe(file);
  });

  it('should NOT append "force" when the flag is not provided', () => {
    uploadApi.uploadLdif(new File(['data'], 'test.ldif'));

    const [, formData] = mockInstance.post.mock.calls[0];
    expect(formData.get('force')).toBeNull();
  });

  it('should append force=true when the force flag is set', () => {
    uploadApi.uploadLdif(new File(['data'], 'test.ldif'), true);

    const [, formData] = mockInstance.post.mock.calls[0];
    expect(formData.get('force')).toBe('true');
  });

  it('should set a 5-minute timeout and clear Content-Type for multipart', () => {
    uploadApi.uploadLdif(new File(['data'], 'test.ldif'));

    const [, , config] = mockInstance.post.mock.calls[0];
    expect(config.timeout).toBe(300000);
    expect(config.headers['Content-Type']).toBeUndefined();
  });
});

// ---------------------------------------------------------------------------
// uploadApi.uploadMasterList
// ---------------------------------------------------------------------------

describe('uploadApi.uploadMasterList', () => {
  it('should POST to /upload/masterlist with a FormData containing the file', () => {
    const file = new File(['cms-data'], 'master.ml');

    uploadApi.uploadMasterList(file);

    const [url, formData] = mockInstance.post.mock.calls[0];
    expect(url).toBe('/upload/masterlist');
    expect(formData.get('file')).toBe(file);
  });

  it('should append force=true when requested', () => {
    uploadApi.uploadMasterList(new File(['data'], 'master.ml'), true);

    const [, formData] = mockInstance.post.mock.calls[0];
    expect(formData.get('force')).toBe('true');
  });

  it('should set a 5-minute timeout', () => {
    uploadApi.uploadMasterList(new File(['data'], 'master.ml'));

    const [, , config] = mockInstance.post.mock.calls[0];
    expect(config.timeout).toBe(300000);
  });
});

// ---------------------------------------------------------------------------
// uploadApi.uploadCertificate
// ---------------------------------------------------------------------------

describe('uploadApi.uploadCertificate', () => {
  it('should POST to /upload/certificate with a FormData containing the file', () => {
    const file = new File(['pem-data'], 'cert.pem');

    uploadApi.uploadCertificate(file);

    const [url, formData] = mockInstance.post.mock.calls[0];
    expect(url).toBe('/upload/certificate');
    expect(formData.get('file')).toBe(file);
  });

  it('should set a 2-minute timeout', () => {
    uploadApi.uploadCertificate(new File(['der-data'], 'cert.der'));

    const [, , config] = mockInstance.post.mock.calls[0];
    expect(config.timeout).toBe(120000);
  });
});

// ---------------------------------------------------------------------------
// uploadApi.previewCertificate
// ---------------------------------------------------------------------------

describe('uploadApi.previewCertificate', () => {
  it('should POST to /upload/certificate/preview with a FormData containing the file', () => {
    const file = new File(['cert-data'], 'preview.pem');

    uploadApi.previewCertificate(file);

    const [url, formData] = mockInstance.post.mock.calls[0];
    expect(url).toBe('/upload/certificate/preview');
    expect(formData.get('file')).toBe(file);
  });

  it('should set a 1-minute timeout', () => {
    uploadApi.previewCertificate(new File(['cert-data'], 'preview.pem'));

    const [, , config] = mockInstance.post.mock.calls[0];
    expect(config.timeout).toBe(60000);
  });
});

// ---------------------------------------------------------------------------
// uploadApi.getHistory
// ---------------------------------------------------------------------------

describe('uploadApi.getHistory', () => {
  it('should GET /upload/history with pagination params', () => {
    const params = { page: 1, limit: 20 };

    uploadApi.getHistory(params as never);

    expect(mockInstance.get).toHaveBeenCalledWith('/upload/history', { params });
  });
});

// ---------------------------------------------------------------------------
// uploadApi.getDetail
// ---------------------------------------------------------------------------

describe('uploadApi.getDetail', () => {
  it('should GET /upload/detail/{uploadId}', () => {
    uploadApi.getDetail('upload-uuid-abc');

    expect(mockInstance.get).toHaveBeenCalledWith('/upload/detail/upload-uuid-abc');
  });
});

// ---------------------------------------------------------------------------
// uploadApi.retryUpload
// ---------------------------------------------------------------------------

describe('uploadApi.retryUpload', () => {
  it('should POST to /upload/{uploadId}/retry', () => {
    uploadApi.retryUpload('upload-uuid-failed');

    expect(mockInstance.post).toHaveBeenCalledWith('/upload/upload-uuid-failed/retry');
  });
});

// ---------------------------------------------------------------------------
// uploadApi.deleteUpload
// ---------------------------------------------------------------------------

describe('uploadApi.deleteUpload', () => {
  it('should DELETE /upload/{uploadId}', () => {
    uploadApi.deleteUpload('upload-uuid-to-delete');

    expect(mockInstance.delete).toHaveBeenCalledWith('/upload/upload-uuid-to-delete');
  });
});

// ---------------------------------------------------------------------------
// createProgressEventSource
// ---------------------------------------------------------------------------

describe('createProgressEventSource', () => {
  it('should create an EventSource pointing to /api/progress/stream/{uploadId}', () => {
    const es = createProgressEventSource('upload-sse-id');

    expect(es).toBeInstanceOf(MockEventSource);
    expect((es as MockEventSource).url).toContain('/api/progress/stream/upload-sse-id');
  });

  it('should return a new EventSource instance per call', () => {
    const es1 = createProgressEventSource('id-1');
    const es2 = createProgressEventSource('id-2');

    expect(es1).not.toBe(es2);
  });

  it('should embed the uploadId verbatim in the URL', () => {
    const id = 'uuid-1234-abcd-5678';

    const es = createProgressEventSource(id);

    expect((es as MockEventSource).url).toContain(id);
  });
});

// ---------------------------------------------------------------------------
// getProgressStatus
// ---------------------------------------------------------------------------

describe('getProgressStatus', () => {
  it('should GET /progress/status/{uploadId}', () => {
    getProgressStatus('upload-poll-id');

    expect(mockInstance.get).toHaveBeenCalledWith('/progress/status/upload-poll-id');
  });
});

// ---------------------------------------------------------------------------
// syncApi.getStatus
// ---------------------------------------------------------------------------

describe('syncApi.getStatus', () => {
  it('should GET /sync/status', () => {
    syncApi.getStatus();

    expect(mockInstance.get).toHaveBeenCalledWith('/sync/status');
  });
});

// ---------------------------------------------------------------------------
// syncApi.getHistory
// ---------------------------------------------------------------------------

describe('syncApi.getHistory', () => {
  it('should GET /sync/history with default limit of 20', () => {
    syncApi.getHistory();

    expect(mockInstance.get).toHaveBeenCalledWith('/sync/history', { params: { limit: 20 } });
  });

  it('should GET /sync/history with the provided limit', () => {
    syncApi.getHistory(50);

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.limit).toBe(50);
  });
});

// ---------------------------------------------------------------------------
// syncApi.triggerCheck
// ---------------------------------------------------------------------------

describe('syncApi.triggerCheck', () => {
  it('should POST to /sync/check', () => {
    syncApi.triggerCheck();

    expect(mockInstance.post).toHaveBeenCalledWith('/sync/check');
  });
});

// ---------------------------------------------------------------------------
// syncApi.getDiscrepancies
// ---------------------------------------------------------------------------

describe('syncApi.getDiscrepancies', () => {
  it('should GET /sync/discrepancies', () => {
    syncApi.getDiscrepancies();

    expect(mockInstance.get).toHaveBeenCalledWith('/sync/discrepancies');
  });
});

// ---------------------------------------------------------------------------
// syncApi.triggerReconcile
// ---------------------------------------------------------------------------

describe('syncApi.triggerReconcile', () => {
  it('should POST to /sync/reconcile', () => {
    syncApi.triggerReconcile();

    expect(mockInstance.post).toHaveBeenCalledWith('/sync/reconcile');
  });
});

// ---------------------------------------------------------------------------
// syncApi.getHealth
// ---------------------------------------------------------------------------

describe('syncApi.getHealth', () => {
  it('should GET /sync/health', () => {
    syncApi.getHealth();

    expect(mockInstance.get).toHaveBeenCalledWith('/sync/health');
  });
});

// ---------------------------------------------------------------------------
// syncApi.getConfig
// ---------------------------------------------------------------------------

describe('syncApi.getConfig', () => {
  it('should GET /sync/config', () => {
    syncApi.getConfig();

    expect(mockInstance.get).toHaveBeenCalledWith('/sync/config');
  });
});

// ---------------------------------------------------------------------------
// syncApi.updateConfig
// ---------------------------------------------------------------------------

describe('syncApi.updateConfig', () => {
  it('should PUT to /sync/config with the provided config data', () => {
    syncApi.updateConfig({ dailySyncEnabled: true, dailySyncHour: 3 });

    expect(mockInstance.put).toHaveBeenCalledWith('/sync/config', { dailySyncEnabled: true, dailySyncHour: 3 });
  });

  it('should send only the changed fields', () => {
    syncApi.updateConfig({ autoReconcile: false });

    const [, body] = mockInstance.put.mock.calls[0];
    expect(body).toEqual({ autoReconcile: false });
  });
});

// ---------------------------------------------------------------------------
// syncApi.triggerRevalidation
// ---------------------------------------------------------------------------

describe('syncApi.triggerRevalidation', () => {
  it('should POST to /sync/revalidate with a 10-minute timeout', () => {
    syncApi.triggerRevalidation();

    const [url, , config] = mockInstance.post.mock.calls[0];
    expect(url).toBe('/sync/revalidate');
    expect(config.timeout).toBe(600000);
  });
});

// ---------------------------------------------------------------------------
// syncApi.getRevalidationHistory
// ---------------------------------------------------------------------------

describe('syncApi.getRevalidationHistory', () => {
  it('should GET /sync/revalidation-history with default limit of 10', () => {
    syncApi.getRevalidationHistory();

    expect(mockInstance.get).toHaveBeenCalledWith(
      '/sync/revalidation-history',
      { params: { limit: 10 } },
    );
  });

  it('should GET /sync/revalidation-history with the provided limit', () => {
    syncApi.getRevalidationHistory(25);

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.limit).toBe(25);
  });
});

// ---------------------------------------------------------------------------
// syncApi.triggerDailySync
// ---------------------------------------------------------------------------

describe('syncApi.triggerDailySync', () => {
  it('should POST to /sync/trigger-daily', () => {
    syncApi.triggerDailySync();

    expect(mockInstance.post).toHaveBeenCalledWith('/sync/trigger-daily');
  });
});

// ---------------------------------------------------------------------------
// syncApi.getReconciliationHistory
// ---------------------------------------------------------------------------

describe('syncApi.getReconciliationHistory', () => {
  it('should GET /sync/reconcile/history with no params when called without arguments', () => {
    syncApi.getReconciliationHistory();

    expect(mockInstance.get).toHaveBeenCalledWith('/sync/reconcile/history', { params: undefined });
  });

  it('should pass all filter params through', () => {
    const params = { limit: 10, offset: 20, status: 'COMPLETED', triggeredBy: 'MANUAL' };

    syncApi.getReconciliationHistory(params);

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params).toEqual(params);
  });
});

// ---------------------------------------------------------------------------
// syncApi.getReconciliationDetails
// ---------------------------------------------------------------------------

describe('syncApi.getReconciliationDetails', () => {
  it('should GET /sync/reconcile/{id} with the correct numeric id', () => {
    syncApi.getReconciliationDetails(42);

    expect(mockInstance.get).toHaveBeenCalledWith('/sync/reconcile/42');
  });

  it('should embed the id in the URL path', () => {
    syncApi.getReconciliationDetails(1);

    const [url] = mockInstance.get.mock.calls[0];
    expect(url).toBe('/sync/reconcile/1');
  });
});

// ---------------------------------------------------------------------------
// syncApi ICAO LDAP sync methods (v2.39.0)
// ---------------------------------------------------------------------------

describe('syncApi.triggerIcaoLdapSync', () => {
  it('should POST to /sync/icao-ldap/trigger', () => {
    syncApi.triggerIcaoLdapSync();

    expect(mockInstance.post).toHaveBeenCalledWith('/sync/icao-ldap/trigger');
  });
});

describe('syncApi.getIcaoLdapSyncStatus', () => {
  it('should GET /sync/icao-ldap/status', () => {
    syncApi.getIcaoLdapSyncStatus();

    expect(mockInstance.get).toHaveBeenCalledWith('/sync/icao-ldap/status');
  });
});

describe('syncApi.getIcaoLdapSyncHistory', () => {
  it('should GET /sync/icao-ldap/history with no params when called without arguments', () => {
    syncApi.getIcaoLdapSyncHistory();

    expect(mockInstance.get).toHaveBeenCalledWith('/sync/icao-ldap/history', { params: undefined });
  });

  it('should pass pagination and status params', () => {
    const params = { limit: 5, offset: 10, status: 'SUCCESS' };

    syncApi.getIcaoLdapSyncHistory(params);

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params).toEqual(params);
  });
});

describe('syncApi.getIcaoLdapSyncConfig', () => {
  it('should GET /sync/icao-ldap/config', () => {
    syncApi.getIcaoLdapSyncConfig();

    expect(mockInstance.get).toHaveBeenCalledWith('/sync/icao-ldap/config');
  });
});

describe('syncApi.updateIcaoLdapSyncConfig', () => {
  it('should PUT to /sync/icao-ldap/config with the provided data', () => {
    syncApi.updateIcaoLdapSyncConfig({ enabled: true, syncIntervalMinutes: 60 });

    expect(mockInstance.put).toHaveBeenCalledWith('/sync/icao-ldap/config', { enabled: true, syncIntervalMinutes: 60 });
  });
});

describe('syncApi.testIcaoLdapConnection', () => {
  it('should POST to /sync/icao-ldap/test', () => {
    syncApi.testIcaoLdapConnection();

    expect(mockInstance.post).toHaveBeenCalledWith('/sync/icao-ldap/test');
  });
});

// ---------------------------------------------------------------------------
// Request interceptor: JWT injection
// ---------------------------------------------------------------------------

describe('relayApi request interceptor', () => {
  it('should have registered a request interceptor at module load time', () => {
    expect(capturedInterceptors.requestFulfilled).toBeTypeOf('function');
  });

  it('should inject Authorization header when access_token is in localStorage', () => {
    localStorage.setItem('access_token', 'relay-jwt-token');

    const config = { headers: {} as Record<string, string> };
    const result = capturedInterceptors.requestFulfilled!(config as never) as typeof config;

    expect(result.headers.Authorization).toBe('Bearer relay-jwt-token');
  });

  it('should not add Authorization header when no token is stored', () => {
    const config = { headers: {} as Record<string, string> };
    const result = capturedInterceptors.requestFulfilled!(config as never) as typeof config;

    expect(result.headers.Authorization).toBeUndefined();
  });
});

// ---------------------------------------------------------------------------
// Response interceptor: 401 → clear localStorage and fire auth:expired
// ---------------------------------------------------------------------------

describe('relayApi response interceptor (401 handling)', () => {
  it('should have registered a response interceptor at module load time', () => {
    expect(capturedInterceptors.responseRejected).toBeTypeOf('function');
  });

  it('should clear localStorage and dispatch auth:expired on 401 on a non-login page', async () => {
    localStorage.setItem('access_token', 'old-token');
    localStorage.setItem('user', '{"id":"1"}');

    Object.defineProperty(window, 'location', {
      value: { pathname: '/dashboard' },
      writable: true,
      configurable: true,
    });

    const dispatchSpy = vi.spyOn(window, 'dispatchEvent');
    const error = { response: { status: 401, data: {} }, message: 'Unauthorized' };

    await expect(capturedInterceptors.responseRejected!(error)).rejects.toBeTruthy();

    expect(localStorage.getItem('access_token')).toBeNull();
    expect(localStorage.getItem('user')).toBeNull();
    expect(dispatchSpy).toHaveBeenCalledWith(
      expect.objectContaining({ type: 'auth:expired' }),
    );
  });

  it('should NOT clear localStorage when the current path is /login', async () => {
    localStorage.setItem('access_token', 'valid-token');

    Object.defineProperty(window, 'location', {
      value: { pathname: '/login' },
      writable: true,
      configurable: true,
    });

    const error = { response: { status: 401, data: {} }, message: 'Unauthorized' };

    await expect(capturedInterceptors.responseRejected!(error)).rejects.toBeTruthy();

    expect(localStorage.getItem('access_token')).toBe('valid-token');
  });

  it('should still reject 409 Conflict errors (duplicate file)', async () => {
    const error = { response: { status: 409, data: { message: 'Duplicate file' } }, message: 'Conflict' };

    await expect(capturedInterceptors.responseRejected!(error)).rejects.toBeTruthy();
  });

  it('should still reject non-401/409 errors', async () => {
    const error = { response: { status: 500, data: {} }, message: 'Server Error' };

    await expect(capturedInterceptors.responseRejected!(error)).rejects.toBeTruthy();
  });
});
