import axios, { type AxiosError, type AxiosResponse } from 'axios';
import type {
  ApiResponse,
  HealthStatus,
  UploadedFile,
  UploadStatisticsOverview,
  PAVerificationRequest,
  PAVerificationResponse,
  PAHistoryItem,
  PAStatisticsOverview,
  PageRequest,
  PageResponse,
  SyncStatusResponse,
  SyncHistoryItem,
  SyncCheckResponse,
  SyncDiscrepancyItem,
} from '@/types';

const api = axios.create({
  baseURL: '/api',
  timeout: 30000,
  headers: {
    'Content-Type': 'application/json',
  },
});

// Response interceptor for error handling
api.interceptors.response.use(
  (response: AxiosResponse) => response,
  (error: AxiosError) => {
    console.error('API Error:', error.response?.data || error.message);
    return Promise.reject(error);
  }
);

// Health Check APIs
export const healthApi = {
  check: () => api.get<HealthStatus>('/health'),
  checkDatabase: () => api.get<{ status: string; version?: string }>('/health/database'),
  checkLdap: () => api.get<{ status: string; responseTime?: number }>('/health/ldap'),
};

// Upload APIs
export const uploadApi = {
  uploadLdif: (file: File, processingMode: string = 'AUTO') => {
    const formData = new FormData();
    formData.append('file', file);
    formData.append('processingMode', processingMode);
    // Let axios set Content-Type automatically with boundary for multipart
    return api.post<ApiResponse<UploadedFile>>('/upload/ldif', formData, {
      headers: { 'Content-Type': undefined },
      timeout: 300000, // 5 minutes for large files
    });
  },

  uploadMasterList: (file: File, processingMode: string = 'AUTO') => {
    const formData = new FormData();
    formData.append('file', file);
    formData.append('processingMode', processingMode);
    // Let axios set Content-Type automatically with boundary for multipart
    return api.post<ApiResponse<UploadedFile>>('/upload/masterlist', formData, {
      headers: { 'Content-Type': undefined },
      timeout: 300000, // 5 minutes for large files
    });
  },

  getHistory: (params: PageRequest) =>
    api.get<PageResponse<UploadedFile>>('/upload/history', { params }),

  getDetail: (uploadId: string) =>
    api.get<ApiResponse<UploadedFile>>(`/upload/detail/${uploadId}`),

  getStatistics: () =>
    api.get<UploadStatisticsOverview>('/upload/statistics'),

  getCountryStatistics: (limit: number = 20) =>
    api.get<{ country: string; csca: number; dsc: number; dscNc: number; total: number }[]>('/upload/countries', { params: { limit } }),

  // Manual processing triggers
  triggerParse: (uploadId: string) =>
    api.post(`/upload/${uploadId}/parse`),

  triggerValidate: (uploadId: string) =>
    api.post(`/upload/${uploadId}/validate`),

  triggerLdapUpload: (uploadId: string) =>
    api.post(`/upload/${uploadId}/ldap`),

  // Delete failed/pending upload
  deleteUpload: (uploadId: string) =>
    api.delete(`/upload/${uploadId}`),
};

// PA APIs
export const paApi = {
  verify: (request: PAVerificationRequest) =>
    api.post<ApiResponse<PAVerificationResponse>>('/pa/verify', request),

  getHistory: (params: PageRequest) =>
    api.get<PageResponse<PAHistoryItem>>('/pa/history', { params }),

  getDetail: (id: string) =>
    api.get<PAVerificationResponse>(`/pa/${id}`),

  getStatistics: () =>
    api.get<PAStatisticsOverview>('/pa/statistics'),

  parseDG1: (data: string) =>
    api.post('/pa/parse-dg1', { data }),

  parseDG2: (data: string) =>
    api.post('/pa/parse-dg2', { data }),
};

// LDAP APIs
export const ldapApi = {
  getHealth: () =>
    api.get('/health/ldap'),

  getStatistics: () =>
    api.get('/ldap/statistics'),

  searchCertificates: (params: { country?: string; type?: string; fingerprint?: string }) =>
    api.get('/ldap/certificates', { params }),

  getCertificateByFingerprint: (fingerprint: string) =>
    api.get(`/ldap/certificates/${fingerprint}`),

  searchCrls: (params: { country?: string }) =>
    api.get('/ldap/crls', { params }),

  checkRevocation: (params: { serialNumber: string; issuerDn: string }) =>
    api.get('/ldap/revocation/check', { params }),
};

// SSE connection for progress updates
export const createProgressEventSource = (uploadId: string): EventSource => {
  return new EventSource(`/api/progress/stream/${uploadId}`);
};

// Progress status polling (alternative to SSE)
export const getProgressStatus = (uploadId: string) =>
  api.get(`/progress/status/${uploadId}`);

// Sync Service APIs (separate service on port 8083)
const syncApi = axios.create({
  baseURL: '/api/sync',
  timeout: 60000,
  headers: {
    'Content-Type': 'application/json',
  },
});

syncApi.interceptors.response.use(
  (response: AxiosResponse) => response,
  (error: AxiosError) => {
    console.error('Sync API Error:', error.response?.data || error.message);
    return Promise.reject(error);
  }
);

export interface SyncConfigResponse {
  autoReconcile: boolean;
  maxReconcileBatchSize: number;
  dailySyncEnabled: boolean;
  dailySyncHour: number;
  dailySyncMinute: number;
  dailySyncTime: string;
  revalidateCertsOnSync: boolean;
}

export interface UpdateSyncConfigRequest {
  dailySyncEnabled?: boolean;
  dailySyncHour?: number;
  dailySyncMinute?: number;
  autoReconcile?: boolean;
  revalidateCertsOnSync?: boolean;
  maxReconcileBatchSize?: number;
}

export interface UpdateSyncConfigResponse {
  success: boolean;
  message: string;
  config: SyncConfigResponse;
}

export interface RevalidationResult {
  success: boolean;
  totalProcessed: number;
  newlyExpired: number;
  newlyValid: number;
  unchanged: number;
  errors: number;
  durationMs: number;
}

export interface RevalidationHistoryItem {
  id: number;
  executedAt: string;
  totalProcessed: number;
  newlyExpired: number;
  newlyValid: number;
  unchanged: number;
  errors: number;
  durationMs: number;
}

export interface ReconciliationSummary {
  id: number;
  startedAt: string;
  completedAt: string | null;
  triggeredBy: 'MANUAL' | 'AUTO' | 'DAILY_SYNC';
  dryRun: boolean;
  status: 'IN_PROGRESS' | 'COMPLETED' | 'FAILED' | 'PARTIAL' | 'ABORTED';
  totalProcessed: number;
  successCount: number;
  failedCount: number;
  cscaAdded: number;
  cscaDeleted: number;
  dscAdded: number;
  dscDeleted: number;
  dscNcAdded: number;
  dscNcDeleted: number;
  crlAdded: number;
  crlDeleted: number;
  durationMs: number;
  errorMessage: string | null;
  syncStatusId: number | null;
}

export interface ReconciliationLog {
  id: number;
  timestamp: string;
  operation: 'ADD' | 'DELETE' | 'UPDATE' | 'SKIP';
  certType: 'CSCA' | 'DSC' | 'DSC_NC' | 'CRL';
  certId: number | null;
  countryCode: string | null;
  subject: string | null;
  issuer: string | null;
  ldapDn: string | null;
  status: 'SUCCESS' | 'FAILED' | 'SKIPPED';
  errorMessage: string | null;
  durationMs: number;
}

export interface ReconciliationHistoryResponse {
  success: boolean;
  history: ReconciliationSummary[];
  total: number;
  limit: number;
  offset: number;
}

export interface ReconciliationDetailsResponse {
  success: boolean;
  summary: ReconciliationSummary;
  logs: ReconciliationLog[];
}

export const syncServiceApi = {
  getStatus: () => syncApi.get<SyncStatusResponse>('/status'),

  getHistory: (limit: number = 20) =>
    syncApi.get<SyncHistoryItem[]>('/history', { params: { limit } }),

  triggerCheck: () => syncApi.post<SyncCheckResponse>('/check'),

  getDiscrepancies: () => syncApi.get<SyncDiscrepancyItem[]>('/discrepancies'),

  triggerReconcile: () => syncApi.post('/reconcile'),

  getHealth: () => syncApi.get<{ status: string; database?: string }>('/health'),

  getConfig: () => syncApi.get<SyncConfigResponse>('/config'),

  updateConfig: (data: UpdateSyncConfigRequest) =>
    syncApi.put<UpdateSyncConfigResponse>('/config', data),

  // Certificate re-validation APIs (v1.1.0+)
  triggerRevalidation: () => syncApi.post<RevalidationResult>('/revalidate'),

  getRevalidationHistory: (limit: number = 10) =>
    syncApi.get<RevalidationHistoryItem[]>('/revalidation-history', { params: { limit } }),

  triggerDailySync: () => syncApi.post<{ success: boolean; message: string }>('/trigger-daily'),

  // Reconciliation APIs (v1.2.0+)
  getReconciliationHistory: (params?: { limit?: number; offset?: number; status?: string; triggeredBy?: string }) =>
    syncApi.get<ReconciliationHistoryResponse>('/reconcile/history', { params }),

  getReconciliationDetails: (id: number) =>
    syncApi.get<ReconciliationDetailsResponse>(`/reconcile/${id}`),
};

// Monitoring Service APIs (separate service on port 8084)
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
    console.error('Monitoring API Error:', error.response?.data || error.message);
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
  getHealth: () => monitoringApi.get<{ status: string; uptime?: string }>('/health'),

  getSystemOverview: () => monitoringApi.get<SystemOverview>('/system/overview'),

  getServicesHealth: () => monitoringApi.get<ServiceHealth[]>('/services'),

  getMetricsHistory: (params: { hours?: number; limit?: number }) =>
    monitoringApi.get<MetricsHistoryItem[]>('/system/history', { params }),

  getSystemMetricsLatest: () => monitoringApi.get<SystemMetrics>('/system/latest'),
};

export default api;
