/**
 * Audit Log API Client
 *
 * Handles communication with PKD Management Service for audit log operations
 * Endpoint: GET /api/audit/operations
 * Endpoint: GET /api/audit/operations/stats
 *
 * @version 1.0.0
 * @since Phase 4.4 - Enhanced Audit Logging
 */

import axios, { type AxiosError, type AxiosResponse } from 'axios';

// =============================================================================
// Types
// =============================================================================

/**
 * Operation types for audit logging
 */
export type OperationType =
  | 'FILE_UPLOAD'
  | 'CERT_EXPORT'
  | 'UPLOAD_DELETE'
  | 'PA_VERIFY'
  | 'SYNC_TRIGGER';

/**
 * Audit log entry
 */
export interface AuditLogEntry {
  id: number;
  userId: string | null;
  username: string | null;
  operationType: OperationType;
  operationSubtype: string | null;
  resourceId: string | null;
  resourceType: string | null;
  ipAddress: string | null;
  userAgent: string | null;
  requestMethod: string | null;
  requestPath: string | null;
  success: boolean;
  statusCode: number | null;
  errorMessage: string | null;
  metadata: Record<string, any> | null;
  durationMs: number | null;
  createdAt: string;
}

/**
 * Audit log query parameters
 */
export interface AuditLogQueryParams {
  operationType?: OperationType;
  username?: string;
  success?: boolean;
  startDate?: string;  // ISO 8601 format
  endDate?: string;    // ISO 8601 format
  limit?: number;
  offset?: number;
}

/**
 * Audit log list response
 */
export interface AuditLogListResponse {
  success: boolean;
  data: AuditLogEntry[];
  total: number;
  limit: number;
  offset: number;
}

/**
 * Audit statistics response
 */
export interface AuditStatisticsResponse {
  success: boolean;
  data: {
    totalOperations: number;
    successfulOperations: number;
    failedOperations: number;
    operationsByType: Record<OperationType, number>;
    topUsers: Array<{
      username: string;
      operationCount: number;
    }>;
    averageDurationMs: number;
  };
}

// =============================================================================
// Axios Instance
// =============================================================================

const auditApi = axios.create({
  baseURL: '/api',
  timeout: 30000,
  headers: {
    'Content-Type': 'application/json',
  },
});

// Response interceptor for error handling
auditApi.interceptors.response.use(
  (response: AxiosResponse) => response,
  (error: AxiosError) => {
    if (import.meta.env.DEV) console.error('[Audit API Error]:', error.response?.data || error.message);
    return Promise.reject(error);
  }
);

// =============================================================================
// API Functions
// =============================================================================

/**
 * Get audit log entries with filtering and pagination
 */
export async function getAuditLogs(
  params: AuditLogQueryParams = {}
): Promise<AuditLogListResponse> {
  const response = await auditApi.get<AuditLogListResponse>('/audit/operations', {
    params,
  });
  return response.data;
}

/**
 * Get audit log statistics
 */
export async function getAuditStatistics(
  params?: {
    startDate?: string;
    endDate?: string;
  }
): Promise<AuditStatisticsResponse> {
  const response = await auditApi.get<AuditStatisticsResponse>('/audit/operations/stats', {
    params,
  });
  return response.data;
}

/**
 * Export audit logs as CSV
 * (Future enhancement)
 */
export async function exportAuditLogs(
  params: AuditLogQueryParams = {}
): Promise<Blob> {
  const response = await auditApi.get('/audit/operations/export', {
    params,
    responseType: 'blob',
  });
  return response.data;
}

export default {
  getAuditLogs,
  getAuditStatistics,
  exportAuditLogs,
};
