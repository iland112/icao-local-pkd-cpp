/**
 * Tests for pendingDscApi.ts
 *
 * pendingDscApi creates its own axios instance (named pkdApi locally).
 * We hoist a mock so the factory captures our object, then verify each
 * exported function calls the correct endpoint.
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

import { pendingDscApi } from '../pendingDscApi';

beforeEach(() => {
  vi.clearAllMocks();
  localStorage.clear();
});

// ---------------------------------------------------------------------------
// pendingDscApi.getList
// ---------------------------------------------------------------------------

describe('pendingDscApi.getList', () => {
  it('should GET /certificates/pending-dsc with no params when called without arguments', () => {
    pendingDscApi.getList();
    expect(mockInstance.get).toHaveBeenCalledWith(
      '/certificates/pending-dsc',
      { params: undefined }
    );
  });

  it('should pass status, country, page, and size filters', () => {
    const params = { status: 'PENDING', country: 'KR', page: 1, size: 20 };
    pendingDscApi.getList(params);
    const [url, config] = mockInstance.get.mock.calls[0];
    expect(url).toBe('/certificates/pending-dsc');
    expect(config.params).toEqual(params);
  });

  it('should pass only provided filters', () => {
    pendingDscApi.getList({ country: 'DE' });
    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.country).toBe('DE');
    expect(config.params.status).toBeUndefined();
  });
});

// ---------------------------------------------------------------------------
// pendingDscApi.getStats
// ---------------------------------------------------------------------------

describe('pendingDscApi.getStats', () => {
  it('should GET /certificates/pending-dsc/stats', () => {
    pendingDscApi.getStats();
    expect(mockInstance.get).toHaveBeenCalledWith('/certificates/pending-dsc/stats');
  });
});

// ---------------------------------------------------------------------------
// pendingDscApi.approve
// ---------------------------------------------------------------------------

describe('pendingDscApi.approve', () => {
  it('should POST to /certificates/pending-dsc/{id}/approve with comment', () => {
    pendingDscApi.approve('dsc-uuid-001', 'Approved after review');
    expect(mockInstance.post).toHaveBeenCalledWith(
      '/certificates/pending-dsc/dsc-uuid-001/approve',
      { comment: 'Approved after review' }
    );
  });

  it('should post with undefined comment when no comment is provided', () => {
    pendingDscApi.approve('dsc-uuid-002');
    const [, body] = mockInstance.post.mock.calls[0];
    expect(body).toEqual({ comment: undefined });
  });

  it('should embed the id in the URL path', () => {
    pendingDscApi.approve('my-dsc-id', 'ok');
    const [url] = mockInstance.post.mock.calls[0];
    expect(url).toBe('/certificates/pending-dsc/my-dsc-id/approve');
  });
});

// ---------------------------------------------------------------------------
// pendingDscApi.reject
// ---------------------------------------------------------------------------

describe('pendingDscApi.reject', () => {
  it('should POST to /certificates/pending-dsc/{id}/reject with comment', () => {
    pendingDscApi.reject('dsc-uuid-003', 'Duplicate certificate');
    expect(mockInstance.post).toHaveBeenCalledWith(
      '/certificates/pending-dsc/dsc-uuid-003/reject',
      { comment: 'Duplicate certificate' }
    );
  });

  it('should post with undefined comment when not provided', () => {
    pendingDscApi.reject('dsc-uuid-004');
    const [, body] = mockInstance.post.mock.calls[0];
    expect(body).toEqual({ comment: undefined });
  });

  it('should embed the id in the URL path', () => {
    pendingDscApi.reject('my-reject-id', 'reason');
    const [url] = mockInstance.post.mock.calls[0];
    expect(url).toBe('/certificates/pending-dsc/my-reject-id/reject');
  });
});

// ---------------------------------------------------------------------------
// Request interceptor: JWT injection
// ---------------------------------------------------------------------------

describe('pendingDscApi request interceptor', () => {
  it('should have registered a request interceptor', () => {
    expect(capturedInterceptors.requestFulfilled).toBeTypeOf('function');
  });

  it('should inject Authorization header when access_token is present', () => {
    localStorage.setItem('access_token', 'dsc-jwt-token');
    const config = { headers: {} as Record<string, string> };
    const result = capturedInterceptors.requestFulfilled!(config as never) as typeof config;
    expect(result.headers.Authorization).toBe('Bearer dsc-jwt-token');
  });

  it('should not add Authorization when no token is stored', () => {
    const config = { headers: {} as Record<string, string> };
    const result = capturedInterceptors.requestFulfilled!(config as never) as typeof config;
    expect(result.headers.Authorization).toBeUndefined();
  });
});

// ---------------------------------------------------------------------------
// Response interceptor: 401 handling
// ---------------------------------------------------------------------------

describe('pendingDscApi response interceptor (401 handling)', () => {
  it('should have registered a response error handler', () => {
    expect(capturedInterceptors.responseErrorHandler).toBeTypeOf('function');
  });

  it('should clear auth and dispatch auth:expired on 401 outside /login', async () => {
    localStorage.setItem('access_token', 'token');
    localStorage.setItem('user', '{}');
    const dispatchSpy = vi.spyOn(window, 'dispatchEvent');
    Object.defineProperty(window, 'location', { value: { pathname: '/admin/pending-dsc' }, writable: true });

    const error = { response: { status: 401 } };
    await expect(capturedInterceptors.responseErrorHandler!(error)).rejects.toEqual(error);

    expect(localStorage.getItem('access_token')).toBeNull();
    expect(localStorage.getItem('user')).toBeNull();
    expect(dispatchSpy).toHaveBeenCalled();
    dispatchSpy.mockRestore();
  });

  it('should NOT clear auth when 401 occurs on the /login page', async () => {
    localStorage.setItem('access_token', 'token');
    Object.defineProperty(window, 'location', { value: { pathname: '/login' }, writable: true });

    const error = { response: { status: 401 } };
    await expect(capturedInterceptors.responseErrorHandler!(error)).rejects.toEqual(error);

    expect(localStorage.getItem('access_token')).toBe('token');
  });

  it('should re-reject non-401 errors', async () => {
    const error = { response: { status: 404 } };
    await expect(capturedInterceptors.responseErrorHandler!(error)).rejects.toEqual(error);
  });
});
