/**
 * Tests for codeMasterApi.ts
 *
 * codeMasterApi delegates all HTTP calls to pkdApi (the shared axios instance
 * from pkdApi.ts). We capture that instance via vi.hoisted() so the factory
 * can reference it before the variable declaration is reached.
 */

import { describe, it, expect, vi, beforeEach } from 'vitest';

// ---- Hoist the shared pkdApi mock instance ----
const { mockPkdInstance } = vi.hoisted(() => {
  const instance = {
    get: vi.fn(),
    post: vi.fn(),
    put: vi.fn(),
    delete: vi.fn(),
    interceptors: {
      request: { use: vi.fn() },
      response: { use: vi.fn() },
    },
  };
  return { mockPkdInstance: instance };
});

vi.mock('axios', () => ({
  default: {
    create: vi.fn(() => mockPkdInstance),
  },
}));

import { codeMasterApi } from '../codeMasterApi';

beforeEach(() => {
  vi.clearAllMocks();
  localStorage.clear();
});

// ---------------------------------------------------------------------------
// codeMasterApi.getAll
// ---------------------------------------------------------------------------

describe('codeMasterApi.getAll', () => {
  it('should GET /code-master with no params when called without arguments', () => {
    codeMasterApi.getAll();

    expect(mockPkdInstance.get).toHaveBeenCalledOnce();
    expect(mockPkdInstance.get).toHaveBeenCalledWith('/code-master', { params: undefined });
  });

  it('should pass the category filter param', () => {
    codeMasterApi.getAll({ category: 'VALIDATION_STATUS' });

    const [url, config] = mockPkdInstance.get.mock.calls[0];
    expect(url).toBe('/code-master');
    expect(config.params.category).toBe('VALIDATION_STATUS');
  });

  it('should pass activeOnly=true filter', () => {
    codeMasterApi.getAll({ activeOnly: true });

    const [, config] = mockPkdInstance.get.mock.calls[0];
    expect(config.params.activeOnly).toBe(true);
  });

  it('should pass activeOnly=false filter', () => {
    codeMasterApi.getAll({ activeOnly: false });

    const [, config] = mockPkdInstance.get.mock.calls[0];
    expect(config.params.activeOnly).toBe(false);
  });

  it('should pass pagination params', () => {
    codeMasterApi.getAll({ page: 2, size: 50 });

    const [, config] = mockPkdInstance.get.mock.calls[0];
    expect(config.params.page).toBe(2);
    expect(config.params.size).toBe(50);
  });

  it('should pass all params combined', () => {
    const params = { category: 'CERTIFICATE_TYPE', activeOnly: true, page: 1, size: 10 };

    codeMasterApi.getAll(params);

    const [, config] = mockPkdInstance.get.mock.calls[0];
    expect(config.params).toEqual(params);
  });

  it('should use the /code-master URL for each of the defined code categories', () => {
    const categories = [
      'VALIDATION_STATUS', 'CRL_STATUS', 'CRL_REVOCATION_REASON',
      'CERTIFICATE_TYPE', 'UPLOAD_STATUS', 'PROCESSING_STAGE',
      'OPERATION_TYPE', 'PA_ERROR_CODE',
    ];

    for (const category of categories) {
      vi.clearAllMocks();
      codeMasterApi.getAll({ category });

      const [url] = mockPkdInstance.get.mock.calls[0];
      expect(url).toBe('/code-master');
    }
  });
});

// ---------------------------------------------------------------------------
// codeMasterApi.getCategories
// ---------------------------------------------------------------------------

describe('codeMasterApi.getCategories', () => {
  it('should GET /code-master/categories', () => {
    codeMasterApi.getCategories();

    expect(mockPkdInstance.get).toHaveBeenCalledOnce();
    expect(mockPkdInstance.get).toHaveBeenCalledWith('/code-master/categories');
  });

  it('should call the endpoint with only the URL argument (no config object)', () => {
    codeMasterApi.getCategories();

    const call = mockPkdInstance.get.mock.calls[0];
    expect(call.length).toBe(1);
    expect(call[0]).toBe('/code-master/categories');
  });
});

// ---------------------------------------------------------------------------
// codeMasterApi.getById
// ---------------------------------------------------------------------------

describe('codeMasterApi.getById', () => {
  it('should GET /code-master/{id} with the correct id', () => {
    codeMasterApi.getById('uuid-1234-abcd');

    expect(mockPkdInstance.get).toHaveBeenCalledOnce();
    expect(mockPkdInstance.get).toHaveBeenCalledWith('/code-master/uuid-1234-abcd');
  });

  it('should embed the id directly in the URL path', () => {
    codeMasterApi.getById('some-id');

    const [url] = mockPkdInstance.get.mock.calls[0];
    expect(url).toBe('/code-master/some-id');
  });

  it('should handle full UUID-format ids correctly', () => {
    const uuid = '550e8400-e29b-41d4-a716-446655440000';

    codeMasterApi.getById(uuid);

    const [url] = mockPkdInstance.get.mock.calls[0];
    expect(url).toBe(`/code-master/${uuid}`);
  });

  it('should call the endpoint with only the URL argument (no config object)', () => {
    codeMasterApi.getById('any-id');

    const call = mockPkdInstance.get.mock.calls[0];
    expect(call.length).toBe(1);
  });
});

// ---------------------------------------------------------------------------
// Return value passthrough
// ---------------------------------------------------------------------------

describe('codeMasterApi return values', () => {
  it('getAll should return the axios promise directly', () => {
    const expected = Promise.resolve({ data: { success: true, items: [] } });
    mockPkdInstance.get.mockReturnValueOnce(expected);

    const result = codeMasterApi.getAll();

    expect(result).toBe(expected);
  });

  it('getCategories should return the axios promise directly', () => {
    const expected = Promise.resolve({ data: { success: true, categories: [] } });
    mockPkdInstance.get.mockReturnValueOnce(expected);

    const result = codeMasterApi.getCategories();

    expect(result).toBe(expected);
  });

  it('getById should return the axios promise directly', () => {
    const expected = Promise.resolve({ data: { success: true, item: null } });
    mockPkdInstance.get.mockReturnValueOnce(expected);

    const result = codeMasterApi.getById('some-id');

    expect(result).toBe(expected);
  });
});

// ---------------------------------------------------------------------------
// Idempotency — repeated calls produce independent requests
// ---------------------------------------------------------------------------

describe('codeMasterApi idempotency', () => {
  it('should make two separate GET calls when getAll is called twice with different categories', () => {
    codeMasterApi.getAll({ category: 'CERTIFICATE_TYPE' });
    codeMasterApi.getAll({ category: 'UPLOAD_STATUS' });

    expect(mockPkdInstance.get).toHaveBeenCalledTimes(2);
    expect(mockPkdInstance.get.mock.calls[0][1].params.category).toBe('CERTIFICATE_TYPE');
    expect(mockPkdInstance.get.mock.calls[1][1].params.category).toBe('UPLOAD_STATUS');
  });

  it('should make two separate GET calls when getById is called twice with different ids', () => {
    codeMasterApi.getById('id-a');
    codeMasterApi.getById('id-b');

    expect(mockPkdInstance.get).toHaveBeenCalledTimes(2);
    expect(mockPkdInstance.get.mock.calls[0][0]).toBe('/code-master/id-a');
    expect(mockPkdInstance.get.mock.calls[1][0]).toBe('/code-master/id-b');
  });

  it('should not carry state between getAll and getCategories calls', () => {
    codeMasterApi.getAll({ category: 'CRL_STATUS' });
    codeMasterApi.getCategories();

    expect(mockPkdInstance.get).toHaveBeenCalledTimes(2);
    expect(mockPkdInstance.get.mock.calls[0][0]).toBe('/code-master');
    expect(mockPkdInstance.get.mock.calls[1][0]).toBe('/code-master/categories');
  });
});
