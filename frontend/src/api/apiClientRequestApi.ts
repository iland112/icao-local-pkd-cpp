/**
 * API Client Request API module
 * Public request submission + admin approval workflow
 */
import axios from 'axios';
import { createAuthenticatedClient } from '@/services/authApi';

// Public client (no JWT) for submit and status check
const publicClient = axios.create({ baseURL: '/api/auth' });

// Authenticated client for admin operations
const authClient = createAuthenticatedClient('/api/auth');

export interface ApiClientRequestItem {
  id: string;
  requester_name: string;
  requester_org: string;
  requester_contact_phone: string;
  requester_contact_email: string;
  request_reason: string;
  client_name: string;
  description: string;
  device_type: 'SERVER' | 'DESKTOP' | 'MOBILE' | 'OTHER';
  permissions: string[];
  allowed_ips: string[];
  status: 'PENDING' | 'APPROVED' | 'REJECTED';
  reviewed_by: string;
  reviewed_at: string;
  review_comment: string;
  approved_client_id: string;
  created_at: string;
  updated_at: string;
}

export interface SubmitApiClientRequest {
  requester_name: string;
  requester_org: string;
  requester_contact_phone?: string;
  requester_contact_email: string;
  request_reason: string;
  client_name: string;
  description?: string;
  device_type: 'SERVER' | 'DESKTOP' | 'MOBILE' | 'OTHER';
  permissions: string[];
  allowed_ips?: string[];
}

/** Admin sends these settings at approval time */
export interface ApproveRequestPayload {
  review_comment?: string;
  rate_limit_per_minute: number;
  rate_limit_per_hour: number;
  rate_limit_per_day: number;
  requested_days: number;
  allowed_endpoints?: string[];
}

export interface ApiClientRequestListResponse {
  success: boolean;
  total: number;
  requests: ApiClientRequestItem[];
}

export interface ApiClientRequestResponse {
  success: boolean;
  message: string;
  request_id?: string;
  request?: ApiClientRequestItem;
}

export interface ApproveResponse {
  success: boolean;
  message: string;
  warning?: string;
  client?: {
    id: string;
    client_name: string;
    api_key_prefix: string;
    api_key: string;
  };
  request?: ApiClientRequestItem;
}

export const apiClientRequestApi = {
  /** Submit new API client request (public, no auth) */
  submit: async (req: SubmitApiClientRequest): Promise<ApiClientRequestResponse> => {
    const { data } = await publicClient.post('/api-client-requests', req);
    return data;
  },

  /** Check request status by ID (public, no auth) */
  getById: async (id: string): Promise<{ success: boolean; request: ApiClientRequestItem }> => {
    const { data } = await publicClient.get(`/api-client-requests/${id}`);
    return data;
  },

  /** List all requests (admin, JWT required) */
  getAll: async (status = '', limit = 100, offset = 0): Promise<ApiClientRequestListResponse> => {
    const params = new URLSearchParams();
    if (status) params.set('status', status);
    params.set('limit', String(limit));
    params.set('offset', String(offset));
    const { data } = await authClient.get(`/api-client-requests?${params.toString()}`);
    return data;
  },

  /** Approve request (admin, JWT required) — admin provides settings */
  approve: async (id: string, payload: ApproveRequestPayload): Promise<ApproveResponse> => {
    const { data } = await authClient.post(`/api-client-requests/${id}/approve`, payload);
    return data;
  },

  /** Reject request (admin, JWT required) */
  reject: async (id: string, reviewComment = ''): Promise<ApiClientRequestResponse> => {
    const { data } = await authClient.post(`/api-client-requests/${id}/reject`, {
      review_comment: reviewComment,
    });
    return data;
  },
};
