/**
 * Pending DSC Registration API module
 * Admin approval workflow for DSC certificates extracted from PA verification
 */
import axios from 'axios';

const pkdApi = axios.create({
  baseURL: '/api',
  timeout: 30000,
  headers: { 'Content-Type': 'application/json' },
});

// JWT interceptor
pkdApi.interceptors.request.use((config) => {
  const token = localStorage.getItem('access_token');
  if (token && config.headers) {
    config.headers.Authorization = `Bearer ${token}`;
  }
  return config;
});

// --- Types ---

export interface PendingDsc {
  id: string;
  fingerprint_sha256: string;
  country_code: string;
  subject_dn: string;
  issuer_dn: string;
  serial_number: string;
  not_before: string;
  not_after: string;
  signature_algorithm: string;
  public_key_algorithm: string;
  public_key_size: number;
  is_self_signed: boolean;
  validation_status: string;
  pa_verification_id: string;
  verification_status: string;
  status: 'PENDING' | 'APPROVED' | 'REJECTED';
  reviewed_by?: string;
  reviewed_at?: string;
  review_comment?: string;
  created_at: string;
  certificate_data?: string; // hex-encoded DER (only in detail)
}

export interface PendingDscListResponse {
  success: boolean;
  data: PendingDsc[];
  total: number;
  page: number;
  size: number;
}

export interface PendingDscStats {
  pendingCount: number;
  approvedCount: number;
  rejectedCount: number;
  totalCount: number;
}

export interface PendingDscStatsResponse {
  success: boolean;
  data: PendingDscStats;
}

export interface PendingDscApproveResponse {
  success: boolean;
  message: string;
  certificateId?: string;
  ldapStored?: boolean;
}

export interface PendingDscRejectResponse {
  success: boolean;
  message: string;
}

// --- API Functions ---

export const pendingDscApi = {
  /** Get paginated list of pending DSC registrations */
  getList: (params?: { status?: string; country?: string; page?: number; size?: number }) =>
    pkdApi.get<PendingDscListResponse>('/certificates/pending-dsc', { params }),

  /** Get pending DSC statistics */
  getStats: () =>
    pkdApi.get<PendingDscStatsResponse>('/certificates/pending-dsc/stats'),

  /** Approve a pending DSC registration (JWT required) */
  approve: (id: string, comment?: string) =>
    pkdApi.post<PendingDscApproveResponse>(`/certificates/pending-dsc/${id}/approve`, { comment }),

  /** Reject a pending DSC registration (JWT required) */
  reject: (id: string, comment?: string) =>
    pkdApi.post<PendingDscRejectResponse>(`/certificates/pending-dsc/${id}/reject`, { comment }),
};
