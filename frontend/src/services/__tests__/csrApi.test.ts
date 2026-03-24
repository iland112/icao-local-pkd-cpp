/**
 * Tests for csrApi.ts
 *
 * csrApi creates its own axios instance at module load time.
 * We hoist a mock instance so the factory closure can capture it,
 * matching the same pattern used by paApi.test.ts.
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

import { csrApiService } from '../csrApi';

beforeEach(() => {
  vi.clearAllMocks();
  localStorage.clear();
});

// ---------------------------------------------------------------------------
// csrApiService.generate
// ---------------------------------------------------------------------------

describe('csrApiService.generate', () => {
  it('should POST to /generate with the generate request body', () => {
    const req = { countryCode: 'KR', organization: 'Test Org', commonName: 'Test CN' };
    csrApiService.generate(req);
    expect(mockInstance.post).toHaveBeenCalledWith('/generate', req);
  });

  it('should include optional memo field when provided', () => {
    const req = { countryCode: 'DE', organization: 'DE Gov', commonName: 'DE CSR', memo: 'ICAO 2026' };
    csrApiService.generate(req);
    const [, body] = mockInstance.post.mock.calls[0];
    expect(body.memo).toBe('ICAO 2026');
  });
});

// ---------------------------------------------------------------------------
// csrApiService.import
// ---------------------------------------------------------------------------

describe('csrApiService.import', () => {
  it('should POST to /import with csrPem and privateKeyPem', () => {
    const req = { csrPem: '-----BEGIN CERTIFICATE REQUEST-----', privateKeyPem: '-----BEGIN RSA PRIVATE KEY-----' };
    csrApiService.import(req);
    expect(mockInstance.post).toHaveBeenCalledWith('/import', req);
  });

  it('should include optional memo field', () => {
    const req = { csrPem: 'pem', privateKeyPem: 'key', memo: 'imported from HSM' };
    csrApiService.import(req);
    const [, body] = mockInstance.post.mock.calls[0];
    expect(body.memo).toBe('imported from HSM');
  });
});

// ---------------------------------------------------------------------------
// csrApiService.list
// ---------------------------------------------------------------------------

describe('csrApiService.list', () => {
  it('should GET root path with default page=1 and pageSize=20', () => {
    csrApiService.list();
    expect(mockInstance.get).toHaveBeenCalledWith('', {
      params: { page: 1, pageSize: 20 },
    });
  });

  it('should pass custom page and pageSize', () => {
    csrApiService.list(3, 10);
    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params).toEqual({ page: 3, pageSize: 10 });
  });

  it('should include status in params when provided', () => {
    csrApiService.list(1, 20, 'ISSUED');
    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.status).toBe('ISSUED');
  });

  it('should NOT include status in params when not provided', () => {
    csrApiService.list(1, 20);
    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params).not.toHaveProperty('status');
  });
});

// ---------------------------------------------------------------------------
// csrApiService.getById
// ---------------------------------------------------------------------------

describe('csrApiService.getById', () => {
  it('should GET /{id} with the correct id', () => {
    csrApiService.getById('csr-uuid-001');
    expect(mockInstance.get).toHaveBeenCalledWith('/csr-uuid-001');
  });

  it('should embed the id directly in the URL path', () => {
    csrApiService.getById('another-uuid');
    const [url] = mockInstance.get.mock.calls[0];
    expect(url).toBe('/another-uuid');
  });
});

// ---------------------------------------------------------------------------
// csrApiService.exportPem
// ---------------------------------------------------------------------------

describe('csrApiService.exportPem', () => {
  it('should GET /{id}/export/pem with responseType blob', () => {
    csrApiService.exportPem('csr-uuid-002');
    expect(mockInstance.get).toHaveBeenCalledWith('/csr-uuid-002/export/pem', {
      responseType: 'blob',
    });
  });
});

// ---------------------------------------------------------------------------
// csrApiService.registerCertificate
// ---------------------------------------------------------------------------

describe('csrApiService.registerCertificate', () => {
  it('should POST to /{id}/certificate with certificatePem', () => {
    csrApiService.registerCertificate('csr-uuid-003', '-----BEGIN CERTIFICATE-----');
    expect(mockInstance.post).toHaveBeenCalledWith(
      '/csr-uuid-003/certificate',
      { certificatePem: '-----BEGIN CERTIFICATE-----' }
    );
  });

  it('should embed the id in the URL path', () => {
    csrApiService.registerCertificate('my-id', 'pem-data');
    const [url] = mockInstance.post.mock.calls[0];
    expect(url).toBe('/my-id/certificate');
  });
});

// ---------------------------------------------------------------------------
// csrApiService.deleteById
// ---------------------------------------------------------------------------

describe('csrApiService.deleteById', () => {
  it('should DELETE /{id}', () => {
    csrApiService.deleteById('csr-uuid-004');
    expect(mockInstance.delete).toHaveBeenCalledWith('/csr-uuid-004');
  });
});

// ---------------------------------------------------------------------------
// csrApiService.signWithCA
// ---------------------------------------------------------------------------

describe('csrApiService.signWithCA', () => {
  it('should POST to /{id}/sign with no body', () => {
    csrApiService.signWithCA('csr-uuid-005');
    expect(mockInstance.post).toHaveBeenCalledWith('/csr-uuid-005/sign');
  });

  it('should embed the id in the URL path', () => {
    csrApiService.signWithCA('sign-id');
    const [url] = mockInstance.post.mock.calls[0];
    expect(url).toBe('/sign-id/sign');
  });
});

// ---------------------------------------------------------------------------
// Request interceptor: JWT injection
// ---------------------------------------------------------------------------

describe('csrApi request interceptor', () => {
  it('should have registered a request interceptor at module load time', () => {
    expect(capturedInterceptors.requestFulfilled).toBeTypeOf('function');
  });

  it('should inject Authorization header when access_token is present', () => {
    localStorage.setItem('access_token', 'csr-test-token');
    const config = { headers: {} as Record<string, string> };
    const result = capturedInterceptors.requestFulfilled!(config as never) as typeof config;
    expect(result.headers.Authorization).toBe('Bearer csr-test-token');
  });

  it('should not add Authorization header when no token is stored', () => {
    const config = { headers: {} as Record<string, string> };
    const result = capturedInterceptors.requestFulfilled!(config as never) as typeof config;
    expect(result.headers.Authorization).toBeUndefined();
  });
});

// ---------------------------------------------------------------------------
// Response interceptor: 401 handling
// ---------------------------------------------------------------------------

describe('csrApi response interceptor (401 handling)', () => {
  it('should have registered a response error handler', () => {
    expect(capturedInterceptors.responseErrorHandler).toBeTypeOf('function');
  });

  it('should clear localStorage and dispatch auth:expired on 401 outside /login', async () => {
    localStorage.setItem('access_token', 'token');
    localStorage.setItem('user', '{}');

    const dispatchSpy = vi.spyOn(window, 'dispatchEvent');
    Object.defineProperty(window, 'location', {
      value: { pathname: '/admin/csr' },
      writable: true,
    });

    const error = { response: { status: 401 } };
    await expect(capturedInterceptors.responseErrorHandler!(error)).rejects.toEqual(error);

    expect(localStorage.getItem('access_token')).toBeNull();
    expect(localStorage.getItem('user')).toBeNull();
    expect(dispatchSpy).toHaveBeenCalledWith(expect.any(CustomEvent));

    dispatchSpy.mockRestore();
  });

  it('should re-reject errors that are not 401', async () => {
    const error = { response: { status: 500 } };
    await expect(capturedInterceptors.responseErrorHandler!(error)).rejects.toEqual(error);
  });
});
