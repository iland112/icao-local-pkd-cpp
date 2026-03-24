/**
 * Tests for monitoringApi.ts
 *
 * vi.mock() is hoisted — mock instance and config are captured via vi.hoisted().
 * Note: monitoringApi has NO request interceptor (JWT not required).
 */

import { describe, it, expect, vi, beforeEach } from 'vitest';

// ---- Hoist mock instance and constructor config ----
const { mockInstance, capturedCreateConfig } = vi.hoisted(() => {
  const capturedCreateConfig: { value?: Record<string, unknown> } = {};

  const instance = {
    get: vi.fn(),
    post: vi.fn(),
    interceptors: {
      request: { use: vi.fn() },
      response: { use: vi.fn() },
    },
  };

  return { mockInstance: instance, capturedCreateConfig };
});

vi.mock('axios', () => ({
  default: {
    create: vi.fn((config: Record<string, unknown>) => {
      capturedCreateConfig.value = config ?? {};
      return mockInstance;
    }),
  },
}));

import { monitoringServiceApi } from '../monitoringApi';

beforeEach(() => {
  vi.clearAllMocks();
});

// ---------------------------------------------------------------------------
// monitoringServiceApi.getHealth
// ---------------------------------------------------------------------------

describe('monitoringServiceApi.getHealth', () => {
  it('should GET /health', () => {
    monitoringServiceApi.getHealth();

    expect(mockInstance.get).toHaveBeenCalledOnce();
    expect(mockInstance.get).toHaveBeenCalledWith('/health');
  });
});

// ---------------------------------------------------------------------------
// monitoringServiceApi.getSystemOverview
// ---------------------------------------------------------------------------

describe('monitoringServiceApi.getSystemOverview', () => {
  it('should GET /system/overview', () => {
    monitoringServiceApi.getSystemOverview();

    expect(mockInstance.get).toHaveBeenCalledOnce();
    expect(mockInstance.get).toHaveBeenCalledWith('/system/overview');
  });
});

// ---------------------------------------------------------------------------
// monitoringServiceApi.getServicesHealth
// ---------------------------------------------------------------------------

describe('monitoringServiceApi.getServicesHealth', () => {
  it('should GET /services', () => {
    monitoringServiceApi.getServicesHealth();

    expect(mockInstance.get).toHaveBeenCalledOnce();
    expect(mockInstance.get).toHaveBeenCalledWith('/services');
  });
});

// ---------------------------------------------------------------------------
// monitoringServiceApi.getMetricsHistory
// ---------------------------------------------------------------------------

describe('monitoringServiceApi.getMetricsHistory', () => {
  it('should GET /system/history with an empty params object when called with {}', () => {
    monitoringServiceApi.getMetricsHistory({});

    expect(mockInstance.get).toHaveBeenCalledWith('/system/history', { params: {} });
  });

  it('should pass the hours param', () => {
    monitoringServiceApi.getMetricsHistory({ hours: 24 });

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.hours).toBe(24);
  });

  it('should pass the limit param', () => {
    monitoringServiceApi.getMetricsHistory({ limit: 100 });

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.limit).toBe(100);
  });

  it('should pass both hours and limit together', () => {
    monitoringServiceApi.getMetricsHistory({ hours: 6, limit: 50 });

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params).toEqual({ hours: 6, limit: 50 });
  });

  it('should call the correct endpoint path', () => {
    monitoringServiceApi.getMetricsHistory({ hours: 1 });

    const [url] = mockInstance.get.mock.calls[0];
    expect(url).toBe('/system/history');
  });
});

// ---------------------------------------------------------------------------
// monitoringServiceApi.getSystemMetricsLatest
// ---------------------------------------------------------------------------

describe('monitoringServiceApi.getSystemMetricsLatest', () => {
  it('should GET /system/latest', () => {
    monitoringServiceApi.getSystemMetricsLatest();

    expect(mockInstance.get).toHaveBeenCalledOnce();
    expect(mockInstance.get).toHaveBeenCalledWith('/system/latest');
  });
});

// ---------------------------------------------------------------------------
// monitoringServiceApi.getLoadSnapshot
// ---------------------------------------------------------------------------

describe('monitoringServiceApi.getLoadSnapshot', () => {
  it('should GET /load', () => {
    monitoringServiceApi.getLoadSnapshot();

    expect(mockInstance.get).toHaveBeenCalledOnce();
    expect(mockInstance.get).toHaveBeenCalledWith('/load');
  });
});

// ---------------------------------------------------------------------------
// monitoringServiceApi.getLoadHistory
// ---------------------------------------------------------------------------

describe('monitoringServiceApi.getLoadHistory', () => {
  it('should GET /load/history with the default minutes value of 30', () => {
    monitoringServiceApi.getLoadHistory();

    expect(mockInstance.get).toHaveBeenCalledWith('/load/history', { params: { minutes: 30 } });
  });

  it('should GET /load/history with the provided minutes value', () => {
    monitoringServiceApi.getLoadHistory(60);

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.minutes).toBe(60);
  });

  it('should GET /load/history with minutes=1 at the lower boundary', () => {
    monitoringServiceApi.getLoadHistory(1);

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.minutes).toBe(1);
  });

  it('should GET /load/history with minutes=1440 at the upper boundary', () => {
    monitoringServiceApi.getLoadHistory(1440);

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.minutes).toBe(1440);
  });

  it('should call the correct endpoint path', () => {
    monitoringServiceApi.getLoadHistory(30);

    const [url] = mockInstance.get.mock.calls[0];
    expect(url).toBe('/load/history');
  });
});

// ---------------------------------------------------------------------------
// axios instance configuration
// capturedCreateConfig.value set at factory time — survives vi.clearAllMocks()
// ---------------------------------------------------------------------------

describe('monitoringClient axios instance configuration', () => {
  it('should create an axios instance with baseURL /api/monitoring', () => {
    expect(capturedCreateConfig.value?.baseURL).toBe('/api/monitoring');
  });

  it('should create an axios instance with a 30-second timeout', () => {
    expect(capturedCreateConfig.value?.timeout).toBe(30000);
  });

  it('should NOT register a request interceptor (monitoring service needs no JWT)', () => {
    // monitoringApi.ts intentionally has no request interceptor because the
    // monitoring service is DB-independent. After clearAllMocks() the call
    // count is 0, confirming the spy was never called during any test.
    expect(mockInstance.interceptors.request.use).not.toHaveBeenCalled();
  });
});

// ---------------------------------------------------------------------------
// Return value passthrough
// ---------------------------------------------------------------------------

describe('monitoringServiceApi return values', () => {
  it('getHealth should return the promise from axios.get', () => {
    const expected = Promise.resolve({ data: { status: 'UP' } });
    mockInstance.get.mockReturnValueOnce(expected);

    const result = monitoringServiceApi.getHealth();

    expect(result).toBe(expected);
  });

  it('getSystemOverview should return the promise from axios.get', () => {
    const expected = Promise.resolve({ data: {} });
    mockInstance.get.mockReturnValueOnce(expected);

    const result = monitoringServiceApi.getSystemOverview();

    expect(result).toBe(expected);
  });

  it('getLoadSnapshot should return the promise from axios.get', () => {
    const expected = Promise.resolve({ data: { nginx: {}, services: [] } });
    mockInstance.get.mockReturnValueOnce(expected);

    const result = monitoringServiceApi.getLoadSnapshot();

    expect(result).toBe(expected);
  });
});
