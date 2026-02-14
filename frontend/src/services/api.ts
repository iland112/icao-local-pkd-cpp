/**
 * Legacy API Module (Backward Compatibility Layer)
 *
 * @deprecated This file is maintained for backward compatibility only.
 * New code should import from specific API modules:
 * - @/services/relayApi - Upload, SSE, Sync operations
 * - @/services/pkdApi - Health, Certificate search, Upload history
 *
 * This file still contains:
 * - PA APIs (not yet migrated to separate module)
 * - Monitoring APIs (not yet migrated to separate module)
 *
 * @version 2.0.0 (Phase 6 - Frontend API Refactoring)
 */

// =============================================================================
// Phase 6: Re-exports from new API modules (Backward Compatibility)
// =============================================================================

// Import APIs from new modules
import {
  uploadApi as relayUploadApi,
  createProgressEventSource,
  getProgressStatus,
  syncApi as syncServiceApi,
  type SyncConfigResponse,
  type UpdateSyncConfigRequest,
  type UpdateSyncConfigResponse,
  type RevalidationResult,
  type RevalidationHistoryItem,
  type ReconciliationSummary,
  type ReconciliationLog,
  type ReconciliationHistoryResponse,
  type ReconciliationDetailsResponse,
} from './relayApi';

import {
  healthApi,
  certificateApi,
  uploadHistoryApi,
  ldapApi,
} from './pkdApi';

// Import authentication API
import {
  authApi,
  createAuthenticatedClient,
  type LoginRequest,
  type LoginResponse,
  type UserInfo,
  type RefreshTokenRequest,
  type RefreshTokenResponse,
  type CurrentUserResponse,
  type LogoutResponse,
} from './authApi';

// Merged uploadApi for backward compatibility
// Combines write operations (relayApi) with read operations (pkdApi)
export const uploadApi = {
  // Write operations from relayApi
  ...relayUploadApi,
  // Read operations from uploadHistoryApi (pkdApi)
  getStatistics: uploadHistoryApi.getStatistics,
  getCountryStatistics: uploadHistoryApi.getCountryStatistics,
  getChanges: uploadHistoryApi.getChanges,
};

// Re-export other APIs
export {
  createProgressEventSource,
  getProgressStatus,
  syncServiceApi,
  healthApi,
  certificateApi,
  uploadHistoryApi,
  ldapApi,
  authApi,
  createAuthenticatedClient,
};

// Re-export types
export type {
  SyncConfigResponse,
  UpdateSyncConfigRequest,
  UpdateSyncConfigResponse,
  RevalidationResult,
  RevalidationHistoryItem,
  ReconciliationSummary,
  ReconciliationLog,
  ReconciliationHistoryResponse,
  ReconciliationDetailsResponse,
  LoginRequest,
  LoginResponse,
  UserInfo,
  RefreshTokenRequest,
  RefreshTokenResponse,
  CurrentUserResponse,
  LogoutResponse,
};

// Development warning (only once per app load)
let warningShown = false;
if (import.meta.env.DEV && !warningShown) {
  console.warn(
    '%c[DEPRECATED API WARNING]',
    'color: orange; font-weight: bold',
    '\nImporting from @/services/api is deprecated.\n' +
    'Use specific API modules instead:\n' +
    '  • @/services/relayApi   → Upload, SSE, Sync\n' +
    '  • @/services/pkdApi     → Health, Certificates\n' +
    '  • @/services/api        → PA, Monitoring (for now)\n'
  );
  warningShown = true;
}

// =============================================================================
// PA Service APIs (port 8082) - Not yet migrated
// =============================================================================

import axios, { type AxiosError, type AxiosResponse } from 'axios';
import type {
  ApiResponse,
  PAVerificationRequest,
  PAVerificationResponse,
  PAHistoryItem,
  PAStatisticsOverview,
  PageRequest,
  PageResponse,
} from '@/types';

const api = axios.create({
  baseURL: '/api',
  timeout: 30000,
  headers: {
    'Content-Type': 'application/json',
  },
});

api.interceptors.response.use(
  (response: AxiosResponse) => response,
  (error: AxiosError) => {
    if (import.meta.env.DEV) console.error('[API Error]:', error.response?.data || error.message);
    return Promise.reject(error);
  }
);

/**
 * PA (Passive Authentication) Service APIs
 * TODO: Migrate to @/services/paApi in future phase
 */
export const paApi = {
  verify: (request: PAVerificationRequest) =>
    api.post<ApiResponse<PAVerificationResponse>>('/pa/verify', request),

  getHistory: (params: PageRequest) =>
    api.get<PageResponse<PAHistoryItem>>('/pa/history', { params }),

  getDetail: (id: string) =>
    api.get<PAVerificationResponse>(`/pa/${id}`),

  getStatistics: () =>
    api.get<ApiResponse<PAStatisticsOverview>>('/pa/statistics'),

  parseDG1: (data: string) =>
    api.post('/pa/parse-dg1', { data }),

  parseDG2: (data: string) =>
    api.post('/pa/parse-dg2', { data }),

  /** Lightweight PA lookup by subject DN or fingerprint (no SOD/DG required) */
  paLookup: (params: { subjectDn?: string; fingerprint?: string }) =>
    api.post('/certificates/pa-lookup', params),
};

// =============================================================================
// Monitoring Service APIs (port 8084) - Not yet migrated
// =============================================================================

const monitoringApi = axios.create({
  baseURL: '/api/monitoring',
  timeout: 30000,
  headers: {
    'Content-Type': 'application/json',
  },
});

monitoringApi.interceptors.response.use(
  (response: AxiosResponse) => response,
  (error: AxiosError) => {
    if (import.meta.env.DEV) console.error('[Monitoring API Error]:', error.response?.data || error.message);
    return Promise.reject(error);
  }
);

export interface CpuMetrics {
  usagePercent: number;
  load1min: number;
  load5min: number;
  load15min: number;
}

export interface MemoryMetrics {
  totalMb: number;
  usedMb: number;
  freeMb: number;
  usagePercent: number;
}

export interface DiskMetrics {
  totalGb: number;
  usedGb: number;
  freeGb: number;
  usagePercent: number;
}

export interface NetworkMetrics {
  bytesSent: number;
  bytesRecv: number;
  packetsSent: number;
  packetsRecv: number;
}

export interface SystemMetrics {
  timestamp: string;
  cpu: CpuMetrics;
  memory: MemoryMetrics;
  disk: DiskMetrics;
  network: NetworkMetrics;
}

export interface ServiceHealth {
  serviceName: string;
  status: 'UP' | 'DEGRADED' | 'DOWN' | 'UNKNOWN';
  responseTimeMs: number;
  errorMessage?: string;
  checkedAt: string;
}

export interface SystemOverview {
  latestMetrics: SystemMetrics;
  services: ServiceHealth[];
}

export interface MetricsHistoryItem {
  timestamp: string;
  cpuUsagePercent: number;
  memoryUsagePercent: number;
  diskUsagePercent: number;
}

export const monitoringServiceApi = {
  getHealth: () =>
    monitoringApi.get<{ status: string; uptime?: string }>('/health'),

  getSystemOverview: () =>
    monitoringApi.get<SystemOverview>('/system/overview'),

  getServicesHealth: () =>
    monitoringApi.get<ServiceHealth[]>('/services'),

  getMetricsHistory: (params: { hours?: number; limit?: number }) =>
    monitoringApi.get<MetricsHistoryItem[]>('/system/history', { params }),

  getSystemMetricsLatest: () =>
    monitoringApi.get<SystemMetrics>('/system/latest'),
};

export default api;
