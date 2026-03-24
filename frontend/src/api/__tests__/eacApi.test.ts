/**
 * Tests for eacApi.ts
 *
 * eacApi creates its own axios instance.  We hoist a mock so the factory
 * closure captures our mock object, then verify each exported function
 * calls the correct endpoint with the correct method and params.
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

import {
  getEacHealth,
  getEacStatistics,
  getEacCountries,
  searchEacCertificates,
  getEacCertificate,
  getEacChain,
  uploadCvc,
  deleteEacCertificate,
  previewCvc,
} from '../eacApi';

beforeEach(() => {
  vi.clearAllMocks();
  localStorage.clear();
});

// ---------------------------------------------------------------------------
// getEacHealth
// ---------------------------------------------------------------------------

describe('getEacHealth', () => {
  it('should GET /health', () => {
    getEacHealth();
    expect(mockInstance.get).toHaveBeenCalledWith('/health');
  });
});

// ---------------------------------------------------------------------------
// getEacStatistics
// ---------------------------------------------------------------------------

describe('getEacStatistics', () => {
  it('should GET /statistics', () => {
    getEacStatistics();
    expect(mockInstance.get).toHaveBeenCalledWith('/statistics');
  });
});

// ---------------------------------------------------------------------------
// getEacCountries
// ---------------------------------------------------------------------------

describe('getEacCountries', () => {
  it('should GET /countries', () => {
    getEacCountries();
    expect(mockInstance.get).toHaveBeenCalledWith('/countries');
  });
});

// ---------------------------------------------------------------------------
// searchEacCertificates
// ---------------------------------------------------------------------------

describe('searchEacCertificates', () => {
  it('should GET /certificates with the provided params', () => {
    const params = { country: 'KR', type: 'IS', status: 'VALID', page: 1, pageSize: 20 };
    searchEacCertificates(params);
    expect(mockInstance.get).toHaveBeenCalledWith('/certificates', { params });
  });

  it('should pass an empty params object when no filters provided', () => {
    searchEacCertificates({});
    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params).toEqual({});
  });

  it('should pass only the fields that are provided', () => {
    searchEacCertificates({ country: 'DE' });
    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.country).toBe('DE');
    expect(config.params.type).toBeUndefined();
  });
});

// ---------------------------------------------------------------------------
// getEacCertificate
// ---------------------------------------------------------------------------

describe('getEacCertificate', () => {
  it('should GET /certificates/{id}', () => {
    getEacCertificate('cvc-uuid-001');
    expect(mockInstance.get).toHaveBeenCalledWith('/certificates/cvc-uuid-001');
  });

  it('should embed the id directly in the URL path', () => {
    getEacCertificate('my-cert-id');
    const [url] = mockInstance.get.mock.calls[0];
    expect(url).toBe('/certificates/my-cert-id');
  });
});

// ---------------------------------------------------------------------------
// getEacChain
// ---------------------------------------------------------------------------

describe('getEacChain', () => {
  it('should GET /certificates/{id}/chain', () => {
    getEacChain('cvc-uuid-002');
    expect(mockInstance.get).toHaveBeenCalledWith('/certificates/cvc-uuid-002/chain');
  });
});

// ---------------------------------------------------------------------------
// uploadCvc
// ---------------------------------------------------------------------------

describe('uploadCvc', () => {
  it('should POST to /upload with a FormData body containing the file', () => {
    const file = new File(['binary'], 'cert.cvc', { type: 'application/octet-stream' });
    uploadCvc(file);

    expect(mockInstance.post).toHaveBeenCalledOnce();
    const [url, formData, config] = mockInstance.post.mock.calls[0];
    expect(url).toBe('/upload');
    expect(formData).toBeInstanceOf(FormData);
    expect(config.headers['Content-Type']).toBe('multipart/form-data');
  });

  it('should include the file under the "file" field in FormData', () => {
    const file = new File(['data'], 'test.cvc');
    uploadCvc(file);

    const [, formData] = mockInstance.post.mock.calls[0];
    expect((formData as FormData).get('file')).toBe(file);
  });
});

// ---------------------------------------------------------------------------
// deleteEacCertificate
// ---------------------------------------------------------------------------

describe('deleteEacCertificate', () => {
  it('should DELETE /certificates/{id}', () => {
    deleteEacCertificate('cvc-uuid-003');
    expect(mockInstance.delete).toHaveBeenCalledWith('/certificates/cvc-uuid-003');
  });
});

// ---------------------------------------------------------------------------
// previewCvc
// ---------------------------------------------------------------------------

describe('previewCvc', () => {
  it('should POST to /upload/preview with a FormData body', () => {
    const file = new File(['binary'], 'preview.cvc', { type: 'application/octet-stream' });
    previewCvc(file);

    expect(mockInstance.post).toHaveBeenCalledOnce();
    const [url, formData, config] = mockInstance.post.mock.calls[0];
    expect(url).toBe('/upload/preview');
    expect(formData).toBeInstanceOf(FormData);
    expect(config.headers['Content-Type']).toBe('multipart/form-data');
  });

  it('should include the file under the "file" field in FormData', () => {
    const file = new File(['data'], 'preview.cvc');
    previewCvc(file);

    const [, formData] = mockInstance.post.mock.calls[0];
    expect((formData as FormData).get('file')).toBe(file);
  });
});

// ---------------------------------------------------------------------------
// Request interceptor: JWT injection
// ---------------------------------------------------------------------------

describe('eacApi request interceptor', () => {
  it('should have registered a request interceptor', () => {
    expect(capturedInterceptors.requestFulfilled).toBeTypeOf('function');
  });

  it('should inject Authorization header when access_token is present', () => {
    localStorage.setItem('access_token', 'eac-jwt-token');
    const config = { headers: {} as Record<string, string> };
    const result = capturedInterceptors.requestFulfilled!(config as never) as typeof config;
    expect(result.headers.Authorization).toBe('Bearer eac-jwt-token');
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

describe('eacApi response interceptor (401 handling)', () => {
  it('should have registered a response error handler', () => {
    expect(capturedInterceptors.responseErrorHandler).toBeTypeOf('function');
  });

  it('should clear auth and dispatch auth:expired on 401 outside /login', async () => {
    localStorage.setItem('access_token', 'token');
    localStorage.setItem('user', '{}');
    const dispatchSpy = vi.spyOn(window, 'dispatchEvent');
    Object.defineProperty(window, 'location', { value: { pathname: '/eac/dashboard' }, writable: true });

    const error = { response: { status: 401 } };
    await expect(capturedInterceptors.responseErrorHandler!(error)).rejects.toEqual(error);

    expect(localStorage.getItem('access_token')).toBeNull();
    expect(localStorage.getItem('user')).toBeNull();
    expect(dispatchSpy).toHaveBeenCalled();
    dispatchSpy.mockRestore();
  });

  it('should re-reject non-401 errors without clearing auth', async () => {
    localStorage.setItem('access_token', 'token');
    const error = { response: { status: 403 } };
    await expect(capturedInterceptors.responseErrorHandler!(error)).rejects.toEqual(error);
    expect(localStorage.getItem('access_token')).toBe('token');
  });
});
