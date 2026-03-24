/**
 * Tests for auditApi.ts
 *
 * Tests the three standalone async functions: getAuditLogs, getAuditStatistics,
 * exportAuditLogs.
 * vi.mock() is hoisted — instance and interceptor callbacks are declared via
 * vi.hoisted() so they are reachable from the factory closure.
 */

import { describe, it, expect, vi, beforeEach } from 'vitest';

// ---- Hoist mock instance and interceptor callbacks ----
const { mockInstance, capturedInterceptors, capturedCreateConfig } = vi.hoisted(() => {
  const capturedInterceptors: {
    requestFulfilled?: (config: Record<string, unknown>) => Record<string, unknown>;
  } = {};
  const capturedCreateConfig: { value?: Record<string, unknown> } = {};

  const instance = {
    get: vi.fn(),
    interceptors: {
      request: {
        use: vi.fn((fulfilled: (c: Record<string, unknown>) => Record<string, unknown>) => {
          capturedInterceptors.requestFulfilled = fulfilled;
        }),
      },
      response: { use: vi.fn() },
    },
  };

  return { mockInstance: instance, capturedInterceptors, capturedCreateConfig };
});

vi.mock('axios', () => ({
  default: {
    create: vi.fn((config: Record<string, unknown>) => {
      capturedCreateConfig.value = config ?? {};
      return mockInstance;
    }),
  },
}));

import { getAuditLogs, getAuditStatistics, exportAuditLogs } from '../auditApi';

beforeEach(() => {
  vi.clearAllMocks();
  localStorage.clear();
});

// ---------------------------------------------------------------------------
// getAuditLogs
// ---------------------------------------------------------------------------

describe('getAuditLogs', () => {
  it('should GET /audit/operations with empty params when called with default arguments', async () => {
    mockInstance.get.mockResolvedValueOnce({
      data: { success: true, data: [], total: 0, limit: 20, offset: 0 },
    });

    await getAuditLogs();

    expect(mockInstance.get).toHaveBeenCalledOnce();
    expect(mockInstance.get).toHaveBeenCalledWith('/audit/operations', { params: {} });
  });

  it('should pass operationType filter', async () => {
    mockInstance.get.mockResolvedValueOnce({ data: { success: true, data: [], total: 0, limit: 20, offset: 0 } });

    await getAuditLogs({ operationType: 'FILE_UPLOAD' });

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.operationType).toBe('FILE_UPLOAD');
  });

  it('should pass username filter', async () => {
    mockInstance.get.mockResolvedValueOnce({ data: { success: true, data: [], total: 0, limit: 20, offset: 0 } });

    await getAuditLogs({ username: 'admin' });

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.username).toBe('admin');
  });

  it('should pass success=true filter', async () => {
    mockInstance.get.mockResolvedValueOnce({ data: { success: true, data: [], total: 0, limit: 20, offset: 0 } });

    await getAuditLogs({ success: true });

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.success).toBe(true);
  });

  it('should pass success=false filter', async () => {
    mockInstance.get.mockResolvedValueOnce({ data: { success: true, data: [], total: 0, limit: 20, offset: 0 } });

    await getAuditLogs({ success: false });

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.success).toBe(false);
  });

  it('should pass ISO 8601 date range filters', async () => {
    mockInstance.get.mockResolvedValueOnce({ data: { success: true, data: [], total: 0, limit: 20, offset: 0 } });

    await getAuditLogs({
      startDate: '2026-01-01T00:00:00Z',
      endDate: '2026-03-22T23:59:59Z',
    });

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.startDate).toBe('2026-01-01T00:00:00Z');
    expect(config.params.endDate).toBe('2026-03-22T23:59:59Z');
  });

  it('should pass pagination params', async () => {
    mockInstance.get.mockResolvedValueOnce({ data: { success: true, data: [], total: 100, limit: 20, offset: 40 } });

    await getAuditLogs({ limit: 20, offset: 40 });

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.limit).toBe(20);
    expect(config.params.offset).toBe(40);
  });

  it('should pass all filter params combined', async () => {
    mockInstance.get.mockResolvedValueOnce({ data: { success: true, data: [], total: 0, limit: 10, offset: 0 } });

    const params = {
      operationType: 'PA_VERIFY' as const,
      username: 'operator',
      success: true,
      startDate: '2026-03-01T00:00:00Z',
      endDate: '2026-03-22T23:59:59Z',
      limit: 10,
      offset: 0,
    };

    await getAuditLogs(params);

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params).toEqual(params);
  });

  it('should return the data property extracted from the axios response', async () => {
    const mockData = {
      success: true,
      data: [
        {
          id: 1,
          userId: 'user-1',
          username: 'admin',
          operationType: 'FILE_UPLOAD',
          success: true,
          createdAt: '2026-03-22T10:00:00Z',
        },
      ],
      total: 1,
      limit: 20,
      offset: 0,
    };
    mockInstance.get.mockResolvedValueOnce({ data: mockData });

    const result = await getAuditLogs();

    expect(result).toEqual(mockData);
  });

  it('should propagate network errors to the caller', async () => {
    mockInstance.get.mockRejectedValueOnce(new Error('Network Error'));

    await expect(getAuditLogs()).rejects.toThrow('Network Error');
  });

  it('should support all 35 defined OperationType values', async () => {
    const operationTypes = [
      'FILE_UPLOAD', 'CERT_EXPORT', 'CERT_UPLOAD', 'UPLOAD_DELETE', 'UPLOAD_RETRY',
      'PA_VERIFY', 'PA_PARSE_SOD', 'PA_PARSE_DG1', 'PA_PARSE_DG2',
      'API_CLIENT_CREATE', 'API_CLIENT_UPDATE', 'API_CLIENT_DELETE', 'API_CLIENT_KEY_REGEN',
      'CODE_MASTER_CREATE', 'CODE_MASTER_UPDATE', 'CODE_MASTER_DELETE',
      'USER_CREATE', 'USER_UPDATE', 'USER_DELETE', 'PASSWORD_CHANGE',
      'ICAO_CHECK', 'SYNC_TRIGGER', 'SYNC_CHECK', 'RECONCILE', 'REVALIDATE',
      'TRIGGER_DAILY_SYNC', 'CONFIG_UPDATE', 'PA_TRUST_MATERIALS',
      'CSR_GENERATE', 'CSR_EXPORT', 'CSR_VIEW', 'CSR_DELETE',
      'DSC_PENDING_SAVE', 'DSC_APPROVE', 'DSC_REJECT',
    ] as const;

    for (const operationType of operationTypes) {
      vi.clearAllMocks();
      mockInstance.get.mockResolvedValueOnce({ data: { success: true, data: [], total: 0, limit: 20, offset: 0 } });

      await getAuditLogs({ operationType });

      const [, config] = mockInstance.get.mock.calls[0];
      expect(config.params.operationType).toBe(operationType);
    }
  });
});

// ---------------------------------------------------------------------------
// getAuditStatistics
// ---------------------------------------------------------------------------

describe('getAuditStatistics', () => {
  it('should GET /audit/operations/stats with no params when called without arguments', async () => {
    mockInstance.get.mockResolvedValueOnce({
      data: {
        success: true,
        data: {
          totalOperations: 0,
          successfulOperations: 0,
          failedOperations: 0,
          operationsByType: {},
          topUsers: [],
          averageDurationMs: 0,
        },
      },
    });

    await getAuditStatistics();

    expect(mockInstance.get).toHaveBeenCalledOnce();
    expect(mockInstance.get).toHaveBeenCalledWith('/audit/operations/stats', { params: undefined });
  });

  it('should pass startDate param', async () => {
    mockInstance.get.mockResolvedValueOnce({ data: { success: true, data: {} } });

    await getAuditStatistics({ startDate: '2026-03-01T00:00:00Z' });

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.startDate).toBe('2026-03-01T00:00:00Z');
  });

  it('should pass endDate param', async () => {
    mockInstance.get.mockResolvedValueOnce({ data: { success: true, data: {} } });

    await getAuditStatistics({ endDate: '2026-03-22T23:59:59Z' });

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.endDate).toBe('2026-03-22T23:59:59Z');
  });

  it('should pass both startDate and endDate together', async () => {
    mockInstance.get.mockResolvedValueOnce({ data: { success: true, data: {} } });

    const params = {
      startDate: '2026-03-01T00:00:00Z',
      endDate: '2026-03-22T23:59:59Z',
    };
    await getAuditStatistics(params);

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params).toEqual(params);
  });

  it('should return the data property extracted from the axios response', async () => {
    const mockData = {
      success: true,
      data: {
        totalOperations: 1500,
        successfulOperations: 1480,
        failedOperations: 20,
        operationsByType: { FILE_UPLOAD: 300, PA_VERIFY: 200 } as Record<string, number>,
        topUsers: [{ username: 'admin', operationCount: 500 }],
        averageDurationMs: 42,
      },
    };
    mockInstance.get.mockResolvedValueOnce({ data: mockData });

    const result = await getAuditStatistics();

    expect(result).toEqual(mockData);
  });

  it('should propagate network errors to the caller', async () => {
    mockInstance.get.mockRejectedValueOnce(new Error('Server Error'));

    await expect(getAuditStatistics()).rejects.toThrow('Server Error');
  });
});

// ---------------------------------------------------------------------------
// exportAuditLogs
// ---------------------------------------------------------------------------

describe('exportAuditLogs', () => {
  it('should GET /audit/operations/export with responseType blob', async () => {
    const blobData = new Blob(['csv,data'], { type: 'text/csv' });
    mockInstance.get.mockResolvedValueOnce({ data: blobData });

    await exportAuditLogs();

    expect(mockInstance.get).toHaveBeenCalledOnce();
    const [url, config] = mockInstance.get.mock.calls[0];
    expect(url).toBe('/audit/operations/export');
    expect(config.responseType).toBe('blob');
  });

  it('should pass filter params alongside responseType blob', async () => {
    mockInstance.get.mockResolvedValueOnce({ data: new Blob() });

    await exportAuditLogs({ operationType: 'FILE_UPLOAD', username: 'admin', limit: 1000 });

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.operationType).toBe('FILE_UPLOAD');
    expect(config.params.username).toBe('admin');
    expect(config.params.limit).toBe(1000);
    expect(config.responseType).toBe('blob');
  });

  it('should use empty params when called without arguments', async () => {
    mockInstance.get.mockResolvedValueOnce({ data: new Blob() });

    await exportAuditLogs();

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params).toEqual({});
  });

  it('should return the Blob data extracted from the axios response', async () => {
    const csvBlob = new Blob(['id,username,operation\n1,admin,FILE_UPLOAD'], { type: 'text/csv' });
    mockInstance.get.mockResolvedValueOnce({ data: csvBlob });

    const result = await exportAuditLogs();

    expect(result).toBe(csvBlob);
  });

  it('should return a Blob instance', async () => {
    mockInstance.get.mockResolvedValueOnce({ data: new Blob(['mock-csv-content']) });

    const result = await exportAuditLogs();

    expect(result).toBeInstanceOf(Blob);
  });

  it('should propagate network errors to the caller', async () => {
    mockInstance.get.mockRejectedValueOnce(new Error('Timeout'));

    await expect(exportAuditLogs()).rejects.toThrow('Timeout');
  });
});

// ---------------------------------------------------------------------------
// Request interceptor: JWT injection
// capturedInterceptors.requestFulfilled is set at factory time
// ---------------------------------------------------------------------------

describe('auditApi request interceptor', () => {
  it('should have registered a request interceptor at module load time', () => {
    expect(capturedInterceptors.requestFulfilled).toBeTypeOf('function');
  });

  it('should inject Authorization header when access_token is present in localStorage', () => {
    localStorage.setItem('access_token', 'audit-jwt-token');

    const config = { headers: {} as Record<string, string> };
    const result = capturedInterceptors.requestFulfilled!(config as never) as typeof config;

    expect(result.headers.Authorization).toBe('Bearer audit-jwt-token');
  });

  it('should not add Authorization header when no token is stored', () => {
    const config = { headers: {} as Record<string, string> };
    const result = capturedInterceptors.requestFulfilled!(config as never) as typeof config;

    expect(result.headers.Authorization).toBeUndefined();
  });
});

// ---------------------------------------------------------------------------
// axios instance configuration
// capturedCreateConfig.value set at factory time — survives vi.clearAllMocks()
// ---------------------------------------------------------------------------

describe('auditApi axios instance configuration', () => {
  it('should create an axios instance with baseURL /api', () => {
    expect(capturedCreateConfig.value?.baseURL).toBe('/api');
  });

  it('should create an axios instance with a 30-second timeout', () => {
    expect(capturedCreateConfig.value?.timeout).toBe(30000);
  });
});

// ---------------------------------------------------------------------------
// Idempotency — repeated calls produce independent requests
// ---------------------------------------------------------------------------

describe('audit API function idempotency', () => {
  it('should make two independent GET calls when getAuditLogs is called twice', async () => {
    mockInstance.get
      .mockResolvedValueOnce({ data: { success: true, data: [], total: 0, limit: 20, offset: 0 } })
      .mockResolvedValueOnce({ data: { success: true, data: [], total: 5, limit: 20, offset: 20 } });

    await getAuditLogs({ offset: 0 });
    await getAuditLogs({ offset: 20 });

    expect(mockInstance.get).toHaveBeenCalledTimes(2);
    expect(mockInstance.get.mock.calls[0][1].params.offset).toBe(0);
    expect(mockInstance.get.mock.calls[1][1].params.offset).toBe(20);
  });

  it('should make independent calls for getAuditLogs and getAuditStatistics', async () => {
    mockInstance.get
      .mockResolvedValueOnce({ data: { success: true, data: [], total: 0, limit: 20, offset: 0 } })
      .mockResolvedValueOnce({ data: { success: true, data: {} } });

    await getAuditLogs();
    await getAuditStatistics();

    expect(mockInstance.get).toHaveBeenCalledTimes(2);
    expect(mockInstance.get.mock.calls[0][0]).toBe('/audit/operations');
    expect(mockInstance.get.mock.calls[1][0]).toBe('/audit/operations/stats');
  });
});
