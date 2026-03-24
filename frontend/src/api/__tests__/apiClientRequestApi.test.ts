/**
 * Tests for apiClientRequestApi.ts
 *
 * apiClientRequestApi uses two HTTP clients:
 *   - publicClient:  plain axios instance (no JWT) for submit / getById
 *   - authClient:    createAuthenticatedClient() instance for admin ops
 *
 * We mock both axios and authApi so we control both clients.
 */

import { describe, it, expect, vi, beforeEach } from 'vitest';

// ---- Hoist mock instances ----
const { mockPublicClient, mockAuthClient } = vi.hoisted(() => {
  const makeClient = () => ({
    get: vi.fn(),
    post: vi.fn(),
    put: vi.fn(),
    delete: vi.fn(),
    interceptors: {
      request: { use: vi.fn() },
      response: { use: vi.fn() },
    },
  });
  return {
    mockPublicClient: makeClient(),
    mockAuthClient: makeClient(),
  };
});

vi.mock('axios', () => ({
  default: {
    create: vi.fn(() => mockPublicClient),
  },
}));

vi.mock('@/services/authApi', () => ({
  createAuthenticatedClient: vi.fn(() => mockAuthClient),
}));

import { apiClientRequestApi } from '../apiClientRequestApi';

beforeEach(() => {
  vi.clearAllMocks();
});

// ---------------------------------------------------------------------------
// apiClientRequestApi.submit  (public client)
// ---------------------------------------------------------------------------

describe('apiClientRequestApi.submit', () => {
  it('should POST to /api-client-requests via public client', async () => {
    const req = {
      requester_name: '홍길동',
      requester_org: 'Test Org',
      requester_contact_email: 'test@test.com',
      request_reason: 'Integration test',
      client_name: 'My App',
      device_type: 'SERVER' as const,
      permissions: ['cert:read'],
    };
    mockPublicClient.post.mockResolvedValueOnce({ data: { success: true, request_id: 'r1' } });

    await apiClientRequestApi.submit(req);

    expect(mockPublicClient.post).toHaveBeenCalledWith('/api-client-requests', req);
  });

  it('should return the response data directly', async () => {
    const responseData = { success: true, message: 'Submitted', request_id: 'new-req-id' };
    mockPublicClient.post.mockResolvedValueOnce({ data: responseData });

    const result = await apiClientRequestApi.submit({
      requester_name: 'User',
      requester_org: 'Org',
      requester_contact_email: 'u@e.com',
      request_reason: 'Reason',
      client_name: 'App',
      device_type: 'DESKTOP',
      permissions: [],
    });

    expect(result).toEqual(responseData);
  });
});

// ---------------------------------------------------------------------------
// apiClientRequestApi.getById  (public client)
// ---------------------------------------------------------------------------

describe('apiClientRequestApi.getById', () => {
  it('should GET /api-client-requests/{id} via public client', async () => {
    mockPublicClient.get.mockResolvedValueOnce({ data: { success: true, request: {} } });

    await apiClientRequestApi.getById('req-uuid-001');

    expect(mockPublicClient.get).toHaveBeenCalledWith('/api-client-requests/req-uuid-001');
  });

  it('should return the response data directly', async () => {
    const responseData = { success: true, request: { id: 'req-uuid-001', status: 'PENDING' } };
    mockPublicClient.get.mockResolvedValueOnce({ data: responseData });

    const result = await apiClientRequestApi.getById('req-uuid-001');

    expect(result).toEqual(responseData);
  });
});

// ---------------------------------------------------------------------------
// apiClientRequestApi.getAll  (auth client)
// ---------------------------------------------------------------------------

describe('apiClientRequestApi.getAll', () => {
  it('should GET /api-client-requests?limit=100&offset=0 with default args', async () => {
    mockAuthClient.get.mockResolvedValueOnce({ data: { success: true, total: 0, requests: [] } });

    await apiClientRequestApi.getAll();

    const [url] = mockAuthClient.get.mock.calls[0];
    expect(url).toContain('/api-client-requests');
    expect(url).toContain('limit=100');
    expect(url).toContain('offset=0');
  });

  it('should include status param when provided', async () => {
    mockAuthClient.get.mockResolvedValueOnce({ data: { success: true, total: 0, requests: [] } });

    await apiClientRequestApi.getAll('PENDING');

    const [url] = mockAuthClient.get.mock.calls[0];
    expect(url).toContain('status=PENDING');
  });

  it('should omit status param when empty string', async () => {
    mockAuthClient.get.mockResolvedValueOnce({ data: { success: true, total: 0, requests: [] } });

    await apiClientRequestApi.getAll('');

    const [url] = mockAuthClient.get.mock.calls[0];
    expect(url).not.toContain('status=');
  });

  it('should pass custom limit and offset', async () => {
    mockAuthClient.get.mockResolvedValueOnce({ data: { success: true, total: 0, requests: [] } });

    await apiClientRequestApi.getAll('', 50, 10);

    const [url] = mockAuthClient.get.mock.calls[0];
    expect(url).toContain('limit=50');
    expect(url).toContain('offset=10');
  });

  it('should return the response data directly', async () => {
    const responseData = { success: true, total: 1, requests: [{ id: 'r1' }] };
    mockAuthClient.get.mockResolvedValueOnce({ data: responseData });

    const result = await apiClientRequestApi.getAll();

    expect(result).toEqual(responseData);
  });
});

// ---------------------------------------------------------------------------
// apiClientRequestApi.approve  (auth client)
// ---------------------------------------------------------------------------

describe('apiClientRequestApi.approve', () => {
  it('should POST to /api-client-requests/{id}/approve with payload', async () => {
    const payload = {
      review_comment: 'Approved',
      rate_limit_per_minute: 60,
      rate_limit_per_hour: 1000,
      rate_limit_per_day: 10000,
      requested_days: 365,
    };
    mockAuthClient.post.mockResolvedValueOnce({ data: { success: true, message: 'ok' } });

    await apiClientRequestApi.approve('req-uuid-002', payload);

    expect(mockAuthClient.post).toHaveBeenCalledWith(
      '/api-client-requests/req-uuid-002/approve',
      payload
    );
  });

  it('should embed the id in the URL path', async () => {
    mockAuthClient.post.mockResolvedValueOnce({ data: { success: true, message: 'ok' } });

    await apiClientRequestApi.approve('my-req-id', {
      rate_limit_per_minute: 30,
      rate_limit_per_hour: 500,
      rate_limit_per_day: 5000,
      requested_days: 30,
    });

    const [url] = mockAuthClient.post.mock.calls[0];
    expect(url).toBe('/api-client-requests/my-req-id/approve');
  });
});

// ---------------------------------------------------------------------------
// apiClientRequestApi.reject  (auth client)
// ---------------------------------------------------------------------------

describe('apiClientRequestApi.reject', () => {
  it('should POST to /api-client-requests/{id}/reject with review_comment', async () => {
    mockAuthClient.post.mockResolvedValueOnce({ data: { success: true, message: 'rejected' } });

    await apiClientRequestApi.reject('req-uuid-003', 'Insufficient permissions requested');

    expect(mockAuthClient.post).toHaveBeenCalledWith(
      '/api-client-requests/req-uuid-003/reject',
      { review_comment: 'Insufficient permissions requested' }
    );
  });

  it('should send empty string review_comment when not provided', async () => {
    mockAuthClient.post.mockResolvedValueOnce({ data: { success: true, message: 'rejected' } });

    await apiClientRequestApi.reject('req-uuid-004');

    const [, body] = mockAuthClient.post.mock.calls[0];
    expect(body.review_comment).toBe('');
  });

  it('should embed the id in the URL path', async () => {
    mockAuthClient.post.mockResolvedValueOnce({ data: { success: true, message: 'rejected' } });

    await apiClientRequestApi.reject('reject-id');

    const [url] = mockAuthClient.post.mock.calls[0];
    expect(url).toBe('/api-client-requests/reject-id/reject');
  });
});
