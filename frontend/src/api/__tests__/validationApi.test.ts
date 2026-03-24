/**
 * Tests for validationApi.ts
 *
 * validationApi delegates all HTTP calls to the shared pkdApi instance
 * exported from pkdApi.ts.  We mock that module and verify each function
 * passes the correct URL and params.
 */

import { describe, it, expect, vi, beforeEach } from 'vitest';

// ---- Hoist mock pkdApi ----
const { mockPkdApi } = vi.hoisted(() => {
  const mock = {
    get: vi.fn(),
  };
  return { mockPkdApi: mock };
});

vi.mock('@/services/pkdApi', () => ({
  default: mockPkdApi,
}));

import { getUploadValidations, getCertificateValidation, validationApi } from '../validationApi';

beforeEach(() => {
  vi.clearAllMocks();
});

// ---------------------------------------------------------------------------
// getUploadValidations
// ---------------------------------------------------------------------------

describe('getUploadValidations', () => {
  it('should GET /upload/{uploadId}/validations with no params when called with empty options', async () => {
    mockPkdApi.get.mockResolvedValueOnce({ data: { success: true, items: [], total: 0 } });

    await getUploadValidations('upload-uuid-001');

    expect(mockPkdApi.get).toHaveBeenCalledWith(
      '/upload/upload-uuid-001/validations',
      { params: {} }
    );
  });

  it('should pass limit when provided', async () => {
    mockPkdApi.get.mockResolvedValueOnce({ data: { items: [], total: 0 } });

    await getUploadValidations('upload-uuid-002', { limit: 50 });

    const [, config] = mockPkdApi.get.mock.calls[0];
    expect(config.params.limit).toBe(50);
  });

  it('should pass offset=0 when explicitly specified', async () => {
    mockPkdApi.get.mockResolvedValueOnce({ data: { items: [], total: 0 } });

    await getUploadValidations('upload-uuid-003', { offset: 0 });

    const [, config] = mockPkdApi.get.mock.calls[0];
    expect(config.params.offset).toBe(0);
  });

  it('should pass status filter when provided', async () => {
    mockPkdApi.get.mockResolvedValueOnce({ data: { items: [], total: 0 } });

    await getUploadValidations('upload-uuid-004', { status: 'VALID' });

    const [, config] = mockPkdApi.get.mock.calls[0];
    expect(config.params.status).toBe('VALID');
  });

  it('should pass certType filter when provided', async () => {
    mockPkdApi.get.mockResolvedValueOnce({ data: { items: [], total: 0 } });

    await getUploadValidations('upload-uuid-005', { certType: 'DSC' });

    const [, config] = mockPkdApi.get.mock.calls[0];
    expect(config.params.certType).toBe('DSC');
  });

  it('should pass icaoCategory filter when provided', async () => {
    mockPkdApi.get.mockResolvedValueOnce({ data: { items: [], total: 0 } });

    await getUploadValidations('upload-uuid-006', { icaoCategory: 'algorithm' });

    const [, config] = mockPkdApi.get.mock.calls[0];
    expect(config.params.icaoCategory).toBe('algorithm');
  });

  it('should pass all options together', async () => {
    mockPkdApi.get.mockResolvedValueOnce({ data: { items: [], total: 0 } });

    await getUploadValidations('upload-uuid-007', {
      limit: 25,
      offset: 50,
      status: 'INVALID',
      certType: 'CSCA',
      icaoCategory: 'keyUsage',
    });

    const [url, config] = mockPkdApi.get.mock.calls[0];
    expect(url).toBe('/upload/upload-uuid-007/validations');
    expect(config.params).toEqual({
      limit: 25,
      offset: 50,
      status: 'INVALID',
      certType: 'CSCA',
      icaoCategory: 'keyUsage',
    });
  });

  it('should NOT include offset in params when not provided', async () => {
    mockPkdApi.get.mockResolvedValueOnce({ data: { items: [], total: 0 } });

    await getUploadValidations('upload-uuid-008', { limit: 10 });

    const [, config] = mockPkdApi.get.mock.calls[0];
    expect(config.params).not.toHaveProperty('offset');
  });

  it('should return response.data', async () => {
    const responseData = { success: true, items: [{ id: 'v1' }], total: 1 };
    mockPkdApi.get.mockResolvedValueOnce({ data: responseData });

    const result = await getUploadValidations('upload-uuid-009');

    expect(result).toEqual(responseData);
  });

  it('should embed the uploadId directly in the URL path', async () => {
    mockPkdApi.get.mockResolvedValueOnce({ data: {} });

    await getUploadValidations('my-specific-upload-id');

    const [url] = mockPkdApi.get.mock.calls[0];
    expect(url).toBe('/upload/my-specific-upload-id/validations');
  });
});

// ---------------------------------------------------------------------------
// getCertificateValidation
// ---------------------------------------------------------------------------

describe('getCertificateValidation', () => {
  it('should GET /certificates/validation with fingerprint param', async () => {
    mockPkdApi.get.mockResolvedValueOnce({ data: { success: true, validation: {} } });

    await getCertificateValidation('abcdef1234567890');

    expect(mockPkdApi.get).toHaveBeenCalledWith(
      '/certificates/validation',
      { params: { fingerprint: 'abcdef1234567890' } }
    );
  });

  it('should return response.data', async () => {
    const responseData = { success: true, validation: { status: 'VALID', fingerprint: 'fp123' } };
    mockPkdApi.get.mockResolvedValueOnce({ data: responseData });

    const result = await getCertificateValidation('fp123');

    expect(result).toEqual(responseData);
  });

  it('should pass a long SHA-256 hex fingerprint without modification', async () => {
    const sha256hex = 'a'.repeat(64);
    mockPkdApi.get.mockResolvedValueOnce({ data: {} });

    await getCertificateValidation(sha256hex);

    const [, config] = mockPkdApi.get.mock.calls[0];
    expect(config.params.fingerprint).toBe(sha256hex);
  });
});

// ---------------------------------------------------------------------------
// validationApi (re-exported object)
// ---------------------------------------------------------------------------

describe('validationApi object', () => {
  it('should expose getUploadValidations as a method', () => {
    expect(typeof validationApi.getUploadValidations).toBe('function');
  });

  it('should expose getCertificateValidation as a method', () => {
    expect(typeof validationApi.getCertificateValidation).toBe('function');
  });

  it('validationApi.getUploadValidations should behave identically to the named export', async () => {
    mockPkdApi.get.mockResolvedValueOnce({ data: { total: 0, items: [] } });

    await validationApi.getUploadValidations('uuid-999');

    expect(mockPkdApi.get).toHaveBeenCalledWith(
      '/upload/uuid-999/validations',
      { params: {} }
    );
  });

  it('validationApi.getCertificateValidation should behave identically to the named export', async () => {
    mockPkdApi.get.mockResolvedValueOnce({ data: { status: 'VALID' } });

    await validationApi.getCertificateValidation('fp-test-999');

    expect(mockPkdApi.get).toHaveBeenCalledWith(
      '/certificates/validation',
      { params: { fingerprint: 'fp-test-999' } }
    );
  });
});
