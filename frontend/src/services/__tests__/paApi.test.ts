/**
 * Tests for paApi.ts
 *
 * vi.mock() is hoisted to the top of the file by Vitest's transform.
 * Variables that the factory closure needs must therefore be declared with
 * vi.hoisted() so they are available before the factory executes.
 */

import { describe, it, expect, vi, beforeEach } from 'vitest';

// ---- Hoist shared state so the vi.mock() factory can write to it ----
const { mockInstance, capturedInterceptors } = vi.hoisted(() => {
  const capturedInterceptors: {
    requestFulfilled?: (config: Record<string, unknown>) => Record<string, unknown>;
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
      response: { use: vi.fn() },
    },
  };

  return { mockInstance: instance, capturedInterceptors };
});

vi.mock('axios', () => ({
  default: {
    create: vi.fn(() => mockInstance),
  },
}));

import { paApi } from '../paApi';

beforeEach(() => {
  vi.clearAllMocks();
  localStorage.clear();
});

// ---------------------------------------------------------------------------
// paApi.verify
// ---------------------------------------------------------------------------

describe('paApi.verify', () => {
  it('should POST to /pa/verify with the provided request body', () => {
    const request = { sodData: 'base64sod', dataGroups: [] };

    paApi.verify(request as never);

    expect(mockInstance.post).toHaveBeenCalledOnce();
    expect(mockInstance.post).toHaveBeenCalledWith('/pa/verify', request);
  });
});

// ---------------------------------------------------------------------------
// paApi.getHistory
// ---------------------------------------------------------------------------

describe('paApi.getHistory', () => {
  it('should GET /pa/history with pagination params', () => {
    const params = { page: 2, limit: 10 };

    paApi.getHistory(params as never);

    expect(mockInstance.get).toHaveBeenCalledWith('/pa/history', { params });
  });

  it('should pass all pagination params through unchanged', () => {
    const params = { page: 1, limit: 5, sort: 'createdAt', order: 'desc' };

    paApi.getHistory(params as never);

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params).toEqual(params);
  });
});

// ---------------------------------------------------------------------------
// paApi.getDetail
// ---------------------------------------------------------------------------

describe('paApi.getDetail', () => {
  it('should GET /pa/{id} with the correct id', () => {
    paApi.getDetail('abc-123');

    expect(mockInstance.get).toHaveBeenCalledWith('/pa/abc-123');
  });

  it('should embed the id directly in the URL path', () => {
    paApi.getDetail('verification-uuid-456');

    const [url] = mockInstance.get.mock.calls[0];
    expect(url).toBe('/pa/verification-uuid-456');
  });
});

// ---------------------------------------------------------------------------
// paApi.getStatistics
// ---------------------------------------------------------------------------

describe('paApi.getStatistics', () => {
  it('should GET /pa/statistics with no params', () => {
    paApi.getStatistics();

    expect(mockInstance.get).toHaveBeenCalledOnce();
    expect(mockInstance.get).toHaveBeenCalledWith('/pa/statistics');
  });
});

// ---------------------------------------------------------------------------
// paApi.parseDG1
// ---------------------------------------------------------------------------

describe('paApi.parseDG1', () => {
  it('should POST to /pa/parse-dg1 with the data payload', () => {
    paApi.parseDG1('base64-dg1-data');

    expect(mockInstance.post).toHaveBeenCalledWith('/pa/parse-dg1', { data: 'base64-dg1-data' });
  });

  it('should wrap the raw string in a { data } object', () => {
    paApi.parseDG1('some-binary-data');

    const [, body] = mockInstance.post.mock.calls[0];
    expect(body).toEqual({ data: 'some-binary-data' });
  });
});

// ---------------------------------------------------------------------------
// paApi.parseDG2
// ---------------------------------------------------------------------------

describe('paApi.parseDG2', () => {
  it('should POST to /pa/parse-dg2 with the data payload', () => {
    paApi.parseDG2('base64-dg2-face-image');

    expect(mockInstance.post).toHaveBeenCalledWith('/pa/parse-dg2', { data: 'base64-dg2-face-image' });
  });
});

// ---------------------------------------------------------------------------
// paApi.paLookup
// ---------------------------------------------------------------------------

describe('paApi.paLookup', () => {
  it('should POST to /certificates/pa-lookup with subjectDn', () => {
    paApi.paLookup({ subjectDn: 'CN=Test,C=KR' });

    expect(mockInstance.post).toHaveBeenCalledWith('/certificates/pa-lookup', { subjectDn: 'CN=Test,C=KR' });
  });

  it('should POST to /certificates/pa-lookup with fingerprint', () => {
    paApi.paLookup({ fingerprint: 'abcdef1234567890' });

    expect(mockInstance.post).toHaveBeenCalledWith('/certificates/pa-lookup', { fingerprint: 'abcdef1234567890' });
  });

  it('should POST to /certificates/pa-lookup with both subjectDn and fingerprint', () => {
    const params = { subjectDn: 'CN=Test,C=KR', fingerprint: 'abc123' };

    paApi.paLookup(params);

    const [, body] = mockInstance.post.mock.calls[0];
    expect(body).toEqual(params);
  });

  it('should POST to /certificates/pa-lookup with empty params when nothing provided', () => {
    paApi.paLookup({});

    expect(mockInstance.post).toHaveBeenCalledWith('/certificates/pa-lookup', {});
  });
});

// ---------------------------------------------------------------------------
// paApi.getDataGroups
// ---------------------------------------------------------------------------

describe('paApi.getDataGroups', () => {
  it('should GET /pa/{verificationId}/datagroups with the correct id', () => {
    paApi.getDataGroups('verify-id-789');

    expect(mockInstance.get).toHaveBeenCalledWith('/pa/verify-id-789/datagroups');
  });
});

// ---------------------------------------------------------------------------
// paApi.getTrustMaterials
// ---------------------------------------------------------------------------

describe('paApi.getTrustMaterials', () => {
  it('should POST to /pa/trust-materials with the required countryCode', () => {
    paApi.getTrustMaterials({ countryCode: 'KR' });

    expect(mockInstance.post).toHaveBeenCalledWith('/pa/trust-materials', { countryCode: 'KR' });
  });

  it('should include optional dscIssuerDn and requestedBy fields', () => {
    const params = { countryCode: 'DE', dscIssuerDn: 'CN=DE DSC CA,C=DE', requestedBy: 'admin' };

    paApi.getTrustMaterials(params);

    const [, body] = mockInstance.post.mock.calls[0];
    expect(body).toEqual(params);
  });

  it('should POST to /pa/trust-materials with a 3-letter ISO country code', () => {
    paApi.getTrustMaterials({ countryCode: 'KOR' });

    const [url, body] = mockInstance.post.mock.calls[0];
    expect(url).toBe('/pa/trust-materials');
    expect(body.countryCode).toBe('KOR');
  });
});

// ---------------------------------------------------------------------------
// paApi.reportTrustMaterialResult
// ---------------------------------------------------------------------------

describe('paApi.reportTrustMaterialResult', () => {
  it('should POST to /pa/trust-materials/result with required fields', () => {
    const params = { requestId: 'req-001', verificationStatus: 'VALID' };

    paApi.reportTrustMaterialResult(params);

    expect(mockInstance.post).toHaveBeenCalledWith('/pa/trust-materials/result', params);
  });

  it('should include all optional fields when provided', () => {
    const params = {
      requestId: 'req-002',
      verificationStatus: 'INVALID',
      verificationMessage: 'Trust chain verification failed',
      trustChainValid: false,
      sodSignatureValid: true,
      dgHashValid: true,
      crlCheckPassed: false,
      processingTimeMs: 1234,
      encryptedMrz: 'ENC:aabbccddeeff',
    };

    paApi.reportTrustMaterialResult(params);

    const [url, body] = mockInstance.post.mock.calls[0];
    expect(url).toBe('/pa/trust-materials/result');
    expect(body).toEqual(params);
  });

  it('should send encrypted MRZ when provided', () => {
    paApi.reportTrustMaterialResult({
      requestId: 'req-003',
      verificationStatus: 'VALID',
      encryptedMrz: 'ENC:deadbeef01020304',
    });

    const [, body] = mockInstance.post.mock.calls[0];
    expect(body.encryptedMrz).toBe('ENC:deadbeef01020304');
  });
});

// ---------------------------------------------------------------------------
// paApi.getTrustMaterialHistory
// ---------------------------------------------------------------------------

describe('paApi.getTrustMaterialHistory', () => {
  it('should GET /pa/trust-materials/history with no params when called without arguments', () => {
    paApi.getTrustMaterialHistory();

    expect(mockInstance.get).toHaveBeenCalledWith('/pa/trust-materials/history', { params: undefined });
  });

  it('should pass page and size params', () => {
    paApi.getTrustMaterialHistory({ page: 1, size: 20 });

    expect(mockInstance.get).toHaveBeenCalledWith(
      '/pa/trust-materials/history',
      { params: { page: 1, size: 20 } },
    );
  });

  it('should pass country filter param', () => {
    paApi.getTrustMaterialHistory({ country: 'KR' });

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params.country).toBe('KR');
  });

  it('should pass all params combined', () => {
    const params = { page: 2, size: 10, country: 'DE' };

    paApi.getTrustMaterialHistory(params);

    const [, config] = mockInstance.get.mock.calls[0];
    expect(config.params).toEqual(params);
  });
});

// ---------------------------------------------------------------------------
// paApi.getCombinedStatistics
// ---------------------------------------------------------------------------

describe('paApi.getCombinedStatistics', () => {
  it('should GET /pa/combined-statistics with no params', () => {
    paApi.getCombinedStatistics();

    expect(mockInstance.get).toHaveBeenCalledOnce();
    expect(mockInstance.get).toHaveBeenCalledWith('/pa/combined-statistics');
  });
});

// ---------------------------------------------------------------------------
// Request interceptor: JWT injection
// capturedInterceptors.requestFulfilled is set before any clearAllMocks()
// ---------------------------------------------------------------------------

describe('paApi request interceptor', () => {
  it('should have registered a request interceptor at module load time', () => {
    expect(capturedInterceptors.requestFulfilled).toBeTypeOf('function');
  });

  it('should inject Authorization header when access_token is present in localStorage', () => {
    localStorage.setItem('access_token', 'test-jwt-token');

    const config = { headers: {} as Record<string, string> };
    const result = capturedInterceptors.requestFulfilled!(config as never) as typeof config;

    expect(result.headers.Authorization).toBe('Bearer test-jwt-token');
  });

  it('should not add Authorization header when no token is stored', () => {
    const config = { headers: {} as Record<string, string> };
    const result = capturedInterceptors.requestFulfilled!(config as never) as typeof config;

    expect(result.headers.Authorization).toBeUndefined();
  });
});
