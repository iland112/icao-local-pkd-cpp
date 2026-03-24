/**
 * Tests for apiClientApi.ts
 *
 * apiClientApi uses createAuthenticatedClient() from authApi to build its
 * HTTP client.  We mock the entire authApi module so the authenticated
 * axios instance is our mock object, then verify each method calls the
 * correct endpoint.
 */

import { describe, it, expect, vi, beforeEach } from 'vitest';

// ---- Hoist the mock axios instance ----
const { mockAuthClient } = vi.hoisted(() => {
  const mock = {
    get: vi.fn(),
    post: vi.fn(),
    put: vi.fn(),
    delete: vi.fn(),
    interceptors: {
      request: { use: vi.fn() },
      response: { use: vi.fn() },
    },
  };
  return { mockAuthClient: mock };
});

vi.mock('@/services/authApi', () => ({
  createAuthenticatedClient: vi.fn(() => mockAuthClient),
}));

import { apiClientApi } from '../apiClientApi';

beforeEach(() => {
  vi.clearAllMocks();
});

// ---------------------------------------------------------------------------
// apiClientApi.getAll
// ---------------------------------------------------------------------------

describe('apiClientApi.getAll', () => {
  it('should GET /api-clients?active_only=false by default', async () => {
    mockAuthClient.get.mockResolvedValueOnce({ data: { success: true, total: 0, clients: [] } });

    await apiClientApi.getAll();

    expect(mockAuthClient.get).toHaveBeenCalledWith('/api-clients?active_only=false');
  });

  it('should GET /api-clients?active_only=true when activeOnly=true', async () => {
    mockAuthClient.get.mockResolvedValueOnce({ data: { success: true, total: 0, clients: [] } });

    await apiClientApi.getAll(true);

    expect(mockAuthClient.get).toHaveBeenCalledWith('/api-clients?active_only=true');
  });

  it('should return the response data directly', async () => {
    const responseData = { success: true, total: 1, clients: [{ id: 'c1' }] };
    mockAuthClient.get.mockResolvedValueOnce({ data: responseData });

    const result = await apiClientApi.getAll();

    expect(result).toEqual(responseData);
  });
});

// ---------------------------------------------------------------------------
// apiClientApi.getById
// ---------------------------------------------------------------------------

describe('apiClientApi.getById', () => {
  it('should GET /api-clients/{id}', async () => {
    mockAuthClient.get.mockResolvedValueOnce({ data: { success: true, client: {} } });

    await apiClientApi.getById('client-uuid-001');

    expect(mockAuthClient.get).toHaveBeenCalledWith('/api-clients/client-uuid-001');
  });

  it('should return the response data directly', async () => {
    const responseData = { success: true, client: { id: 'client-uuid-001', client_name: 'Test' } };
    mockAuthClient.get.mockResolvedValueOnce({ data: responseData });

    const result = await apiClientApi.getById('client-uuid-001');

    expect(result).toEqual(responseData);
  });
});

// ---------------------------------------------------------------------------
// apiClientApi.create
// ---------------------------------------------------------------------------

describe('apiClientApi.create', () => {
  it('should POST to /api-clients with the request body', async () => {
    const req = { client_name: 'Test Client', permissions: ['cert:read'] };
    mockAuthClient.post.mockResolvedValueOnce({ data: { success: true, client: {} } });

    await apiClientApi.create(req);

    expect(mockAuthClient.post).toHaveBeenCalledWith('/api-clients', req);
  });

  it('should return the response data directly', async () => {
    const responseData = { success: true, client: { id: 'new-id', api_key: 'icao_xxx_yyy' } };
    mockAuthClient.post.mockResolvedValueOnce({ data: responseData });

    const result = await apiClientApi.create({ client_name: 'New', permissions: [] });

    expect(result).toEqual(responseData);
  });
});

// ---------------------------------------------------------------------------
// apiClientApi.update
// ---------------------------------------------------------------------------

describe('apiClientApi.update', () => {
  it('should PUT to /api-clients/{id} with the update body', async () => {
    const req = { client_name: 'Updated Name', is_active: false };
    mockAuthClient.put.mockResolvedValueOnce({ data: { success: true, client: {} } });

    await apiClientApi.update('client-uuid-002', req);

    expect(mockAuthClient.put).toHaveBeenCalledWith('/api-clients/client-uuid-002', req);
  });

  it('should embed the id in the URL path', async () => {
    mockAuthClient.put.mockResolvedValueOnce({ data: { success: true, client: {} } });

    await apiClientApi.update('my-client-id', { permissions: ['pa:verify'] });

    const [url] = mockAuthClient.put.mock.calls[0];
    expect(url).toBe('/api-clients/my-client-id');
  });
});

// ---------------------------------------------------------------------------
// apiClientApi.deactivate
// ---------------------------------------------------------------------------

describe('apiClientApi.deactivate', () => {
  it('should DELETE /api-clients/{id}', async () => {
    mockAuthClient.delete.mockResolvedValueOnce({ data: { success: true, message: 'deactivated' } });

    await apiClientApi.deactivate('client-uuid-003');

    expect(mockAuthClient.delete).toHaveBeenCalledWith('/api-clients/client-uuid-003');
  });

  it('should return the response data directly', async () => {
    const responseData = { success: true, message: 'Client deactivated' };
    mockAuthClient.delete.mockResolvedValueOnce({ data: responseData });

    const result = await apiClientApi.deactivate('client-uuid-003');

    expect(result).toEqual(responseData);
  });
});

// ---------------------------------------------------------------------------
// apiClientApi.regenerate
// ---------------------------------------------------------------------------

describe('apiClientApi.regenerate', () => {
  it('should POST to /api-clients/{id}/regenerate', async () => {
    mockAuthClient.post.mockResolvedValueOnce({ data: { success: true, client: {} } });

    await apiClientApi.regenerate('client-uuid-004');

    expect(mockAuthClient.post).toHaveBeenCalledWith('/api-clients/client-uuid-004/regenerate');
  });

  it('should embed the id in the URL path', async () => {
    mockAuthClient.post.mockResolvedValueOnce({ data: { success: true, client: {} } });

    await apiClientApi.regenerate('regen-id');

    const [url] = mockAuthClient.post.mock.calls[0];
    expect(url).toBe('/api-clients/regen-id/regenerate');
  });
});

// ---------------------------------------------------------------------------
// apiClientApi.getUsage
// ---------------------------------------------------------------------------

describe('apiClientApi.getUsage', () => {
  it('should GET /api-clients/{id}/usage?days=7 by default', async () => {
    mockAuthClient.get.mockResolvedValueOnce({ data: { success: true, usage: {} } });

    await apiClientApi.getUsage('client-uuid-005');

    expect(mockAuthClient.get).toHaveBeenCalledWith('/api-clients/client-uuid-005/usage?days=7');
  });

  it('should use custom days value when specified', async () => {
    mockAuthClient.get.mockResolvedValueOnce({ data: { success: true, usage: {} } });

    await apiClientApi.getUsage('client-uuid-005', 30);

    expect(mockAuthClient.get).toHaveBeenCalledWith('/api-clients/client-uuid-005/usage?days=30');
  });

  it('should return the response data directly', async () => {
    const responseData = { success: true, usage: { totalRequests: 42, topEndpoints: [] } };
    mockAuthClient.get.mockResolvedValueOnce({ data: responseData });

    const result = await apiClientApi.getUsage('client-uuid-005');

    expect(result).toEqual(responseData);
  });
});
