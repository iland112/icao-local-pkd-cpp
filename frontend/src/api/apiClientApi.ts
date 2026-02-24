/**
 * API Client Management API module
 * External client agent API key CRUD operations
 */
import { createAuthenticatedClient } from '@/services/authApi';

const authClient = createAuthenticatedClient('/api/auth');

export interface ApiClient {
  id: string;
  client_name: string;
  api_key_prefix: string;
  api_key?: string; // Only returned on create/regenerate
  description: string;
  permissions: string[];
  allowed_endpoints: string[];
  allowed_ips: string[];
  rate_limit_per_minute: number;
  rate_limit_per_hour: number;
  rate_limit_per_day: number;
  is_active: boolean;
  expires_at: string;
  last_used_at: string;
  total_requests: number;
  created_by: string;
  created_at: string;
  updated_at: string;
}

export interface ApiClientListResponse {
  success: boolean;
  total: number;
  clients: ApiClient[];
}

export interface ApiClientResponse {
  success: boolean;
  client: ApiClient;
  warning?: string;
}

export interface CreateApiClientRequest {
  client_name: string;
  description?: string;
  permissions: string[];
  allowed_endpoints?: string[];
  allowed_ips?: string[];
  rate_limit_per_minute?: number;
  rate_limit_per_hour?: number;
  rate_limit_per_day?: number;
  expires_at?: string | null;
}

export interface UpdateApiClientRequest {
  client_name?: string;
  description?: string;
  permissions?: string[];
  allowed_endpoints?: string[];
  allowed_ips?: string[];
  rate_limit_per_minute?: number;
  rate_limit_per_hour?: number;
  rate_limit_per_day?: number;
  is_active?: boolean;
}

export interface UsageStats {
  totalRequests: number;
  topEndpoints: { endpoint: string; count: number }[];
}

export const apiClientApi = {
  /** List all API clients */
  getAll: async (activeOnly = false): Promise<ApiClientListResponse> => {
    const { data } = await authClient.get(`/api-clients?active_only=${activeOnly}`);
    return data;
  },

  /** Get single client by ID */
  getById: async (id: string): Promise<ApiClientResponse> => {
    const { data } = await authClient.get(`/api-clients/${id}`);
    return data;
  },

  /** Create new API client (returns raw API key once) */
  create: async (req: CreateApiClientRequest): Promise<ApiClientResponse> => {
    const { data } = await authClient.post('/api-clients', req);
    return data;
  },

  /** Update API client */
  update: async (id: string, req: UpdateApiClientRequest): Promise<ApiClientResponse> => {
    const { data } = await authClient.put(`/api-clients/${id}`, req);
    return data;
  },

  /** Deactivate API client (soft delete) */
  deactivate: async (id: string): Promise<{ success: boolean; message: string }> => {
    const { data } = await authClient.delete(`/api-clients/${id}`);
    return data;
  },

  /** Regenerate API key (returns new raw API key once) */
  regenerate: async (id: string): Promise<ApiClientResponse> => {
    const { data } = await authClient.post(`/api-clients/${id}/regenerate`);
    return data;
  },

  /** Get usage statistics */
  getUsage: async (id: string, days = 7): Promise<{ success: boolean; usage: UsageStats }> => {
    const { data } = await authClient.get(`/api-clients/${id}/usage?days=${days}`);
    return data;
  },
};
