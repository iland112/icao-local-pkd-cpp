/**
 * PKD Relay Service API
 *
 * Handles communication with PKD Relay Service (port 8083)
 * Responsibilities:
 * - File upload & parsing (LDIF, Master List)
 * - Server-Sent Events (SSE) for upload progress
 * - DB-LDAP synchronization monitoring
 * - Certificate reconciliation
 *
 * @version 2.0.0
 */

import axios, { type AxiosError, type AxiosResponse } from 'axios';
import type {
  ApiResponse,
  UploadedFile,
  CertificateUploadResponse,
  PageRequest,
  PageResponse,
  SyncStatusResponse,
  SyncHistoryItem,
  SyncCheckResponse,
  SyncDiscrepancyItem,
  CertificatePreviewResult,
} from '@/types';

// --- Axios Instance ---

/**
 * Relay API client
 * Base URL: /api/relay (future-proof) or /api (current)
 *
 * Environment variable controls the path:
 * - VITE_USE_RELAY_PATHS=true  → /api/relay/*
 * - VITE_USE_RELAY_PATHS=false → /api/* (default, backward compatible)
 */
const USE_RELAY_PATHS = import.meta.env.VITE_USE_RELAY_PATHS === 'true';
const BASE_PATH = USE_RELAY_PATHS ? '/api/relay' : '/api';

const relayApi = axios.create({
  baseURL: BASE_PATH,
  timeout: 60000, // 60 seconds for sync operations
  headers: {
    'Content-Type': 'application/json',
  },
});

// Request interceptor: Inject JWT token for protected endpoints (upload/save)
relayApi.interceptors.request.use(
  (config) => {
    const token = localStorage.getItem('access_token');
    if (token && config.headers) {
      config.headers.Authorization = `Bearer ${token}`;
    }
    return config;
  },
  (error) => Promise.reject(error)
);

// Response interceptor for error handling
relayApi.interceptors.response.use(
  (response: AxiosResponse) => response,
  (error: AxiosError) => {
    if (error.response?.status === 401) {
      const currentPath = window.location.pathname;
      if (currentPath !== '/login') {
        localStorage.removeItem('access_token');
        localStorage.removeItem('user');
        window.location.href = '/login';
      }
    }
    if (import.meta.env.DEV) console.error('[Relay API Error]:', error.response?.data || error.message);
    return Promise.reject(error);
  }
);

// --- Upload APIs ---

/**
 * Upload & Parsing APIs
 * Handles LDIF and Master List file uploads with AUTO/MANUAL processing modes
 */
export const uploadApi = {
  /**
   * Upload LDIF file
   * @param file - LDIF file to upload
   * @param processingMode - AUTO (immediate processing) or MANUAL (3-stage processing)
   * @returns Upload record with uploadId (used for SSE connection)
   */
  uploadLdif: (file: File, processingMode: string = 'AUTO') => {
    const formData = new FormData();
    formData.append('file', file);
    formData.append('processingMode', processingMode);

    return relayApi.post<ApiResponse<UploadedFile>>('/upload/ldif', formData, {
      headers: { 'Content-Type': undefined }, // Let axios set multipart boundary
      timeout: 300000, // 5 minutes for large files
    });
  },

  /**
   * Upload Master List file (ICAO CMS/PKCS7 format)
   * @param file - Master List file to upload
   * @param processingMode - AUTO or MANUAL
   * @returns Upload record with uploadId
   */
  uploadMasterList: (file: File, processingMode: string = 'AUTO') => {
    const formData = new FormData();
    formData.append('file', file);
    formData.append('processingMode', processingMode);

    return relayApi.post<ApiResponse<UploadedFile>>('/upload/masterlist', formData, {
      headers: { 'Content-Type': undefined },
      timeout: 300000, // 5 minutes for large files
    });
  },

  /**
   * Upload individual certificate file (PEM, DER, CER, P7B, DL, CRL)
   * Synchronous processing - no SSE needed
   * @param file - Certificate file to upload
   * @returns Upload result with certificate counts
   */
  uploadCertificate: (file: File) => {
    const formData = new FormData();
    formData.append('file', file);

    return relayApi.post<CertificateUploadResponse>('/upload/certificate', formData, {
      headers: { 'Content-Type': undefined },
      timeout: 120000, // 2 minutes
    });
  },

  /**
   * Preview certificate file (parse only, no DB/LDAP save)
   * Returns parsed metadata for user review before confirming save.
   * @param file - Certificate file to preview
   * @returns Parsed certificate metadata
   */
  previewCertificate: (file: File) => {
    const formData = new FormData();
    formData.append('file', file);

    return relayApi.post<CertificatePreviewResult>('/upload/certificate/preview', formData, {
      headers: { 'Content-Type': undefined },
      timeout: 60000, // 1 minute
    });
  },

  /**
   * Get upload history with pagination
   * @param params - Pagination parameters (page, limit, sort, etc.)
   * @returns Paginated list of uploaded files
   */
  getHistory: (params: PageRequest) =>
    relayApi.get<PageResponse<UploadedFile>>('/upload/history', { params }),

  /**
   * Get upload details by ID
   * @param uploadId - Upload record UUID
   * @returns Full upload record with statistics
   */
  getDetail: (uploadId: string) =>
    relayApi.get<ApiResponse<UploadedFile>>(`/upload/detail/${uploadId}`),

  // -------------------------------------------------------------------------
  // Manual Processing Triggers (3-stage workflow)
  // -------------------------------------------------------------------------

  /**
   * Stage 1: Parse LDIF file
   * Parses LDIF entries and saves to temp file (no DB write)
   * @param uploadId - Upload record UUID
   * @returns Parse result
   */
  triggerParse: (uploadId: string) =>
    relayApi.post(`/upload/${uploadId}/parse`),

  /**
   * Stage 2: Validate & Save to DB
   * Reads temp file, validates certificates, saves to PostgreSQL
   * @param uploadId - Upload record UUID
   * @returns Validation result
   */
  triggerValidate: (uploadId: string) =>
    relayApi.post(`/upload/${uploadId}/validate`),

  /**
   * Stage 3: Upload to LDAP
   * Reads from DB and uploads to LDAP directory
   * @param uploadId - Upload record UUID
   * @returns LDAP upload result
   */
  triggerLdapUpload: (uploadId: string) =>
    relayApi.post(`/upload/${uploadId}/ldap`),

  /**
   * Delete failed or pending upload
   * Cleans up DB records and temporary files
   * @param uploadId - Upload record UUID
   * @returns Deletion result
   */
  deleteUpload: (uploadId: string) =>
    relayApi.delete(`/upload/${uploadId}`),
};

// --- Server-Sent Events (SSE) ---

/**
 * Create SSE connection for upload progress monitoring
 *
 * CRITICAL: This factory function creates EventSource for real-time progress updates
 *
 * Environment variable controls the path:
 * - VITE_USE_RELAY_SSE=true  → /api/relay/progress/stream/{uploadId}
 * - VITE_USE_RELAY_SSE=false → /api/progress/stream/{uploadId} (default)
 *
 * SSE Event Flow (AUTO mode):
 * 1. connected          → Connection established
 * 2. PARSING_STARTED    → Parsing begins (10%)
 * 3. PARSING_COMPLETED  → Parsing done (50%)
 * 4. VALIDATION_STARTED → Validation begins (55%)
 * 5. DB_SAVING_STARTED  → DB write begins (72%)
 * 6. DB_SAVING_COMPLETED → DB write done (85%)
 * 7. LDAP_SAVING_STARTED → LDAP upload begins (87%)
 * 8. LDAP_SAVING_COMPLETED → LDAP upload done (100%)
 * 9. COMPLETED          → All stages complete
 *
 * SSE Event Flow (MANUAL mode):
 * Stage 1: PARSING_* events
 * Stage 2: VALIDATION_* + DB_SAVING_* events
 * Stage 3: LDAP_SAVING_* events
 *
 * @param uploadId - Upload record UUID
 * @returns EventSource instance (caller must close() when done)
 *
 * @example
 * const eventSource = createProgressEventSource(uploadId);
 *
 * eventSource.addEventListener('progress', (event) => {
 *   const progress = JSON.parse(event.data);
 *   console.log(progress.stage, progress.percentage);
 * });
 *
 * eventSource.onerror = () => {
 *   eventSource.close();
 *   // Fallback to polling
 * };
 */
export const createProgressEventSource = (uploadId: string): EventSource => {
  const USE_RELAY_SSE = import.meta.env.VITE_USE_RELAY_SSE === 'true';
  const sseBasePath = USE_RELAY_SSE ? '/api/relay/progress' : '/api/progress';

  const url = `${sseBasePath}/stream/${uploadId}`;

  if (import.meta.env.DEV) {
    console.log(`[SSE] Creating EventSource: ${url} (RELAY_SSE=${USE_RELAY_SSE})`);
  }

  return new EventSource(url);
};

/**
 * Get progress status via polling (alternative to SSE)
 * Used as backup when SSE connection fails
 * @param uploadId - Upload record UUID
 * @returns Current upload progress
 */
export const getProgressStatus = (uploadId: string) =>
  relayApi.get(`/progress/status/${uploadId}`);

// --- Sync Service APIs ---

/**
 * Sync configuration interface
 */
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

/**
 * Certificate re-validation result
 */
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

/**
 * Reconciliation (Auto Reconcile) types
 */
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

/**
 * DB-LDAP Sync Monitoring & Reconciliation APIs
 */
export const syncApi = {
  /**
   * Get current sync status
   * Shows DB vs LDAP certificate counts and discrepancies
   */
  getStatus: () => relayApi.get<{ success: boolean; data: SyncStatusResponse }>('/sync/status'),

  /**
   * Get sync check history
   * @param limit - Number of records to return
   */
  getHistory: (limit: number = 20) =>
    relayApi.get<{ success: boolean; data: SyncHistoryItem[]; pagination: { total: number; limit: number; offset: number; count: number } }>('/sync/history', { params: { limit } }),

  /**
   * Trigger manual sync check
   * Counts certificates in DB and LDAP, identifies discrepancies
   */
  triggerCheck: () => relayApi.post<SyncCheckResponse>('/sync/check'),

  /**
   * Get current discrepancies
   * Lists certificates in DB but not in LDAP
   */
  getDiscrepancies: () => relayApi.get<SyncDiscrepancyItem[]>('/sync/discrepancies'),

  /**
   * Trigger manual reconciliation
   * Synchronizes DB to LDAP (adds missing certificates)
   */
  triggerReconcile: () => relayApi.post('/sync/reconcile'),

  /**
   * Health check for sync service
   */
  getHealth: () => relayApi.get<{ status: string; database?: string }>('/sync/health'),

  /**
   * Get sync configuration
   */
  getConfig: () => relayApi.get<SyncConfigResponse>('/sync/config'),

  /**
   * Update sync configuration
   * @param data - Configuration updates
   */
  updateConfig: (data: UpdateSyncConfigRequest) =>
    relayApi.put<UpdateSyncConfigResponse>('/sync/config', data),

  // -------------------------------------------------------------------------
  // Certificate Re-validation
  // -------------------------------------------------------------------------

  /**
   * Trigger certificate re-validation
   * Re-checks validity periods for all certificates
   */
  triggerRevalidation: () => relayApi.post<RevalidationResult>('/sync/revalidate'),

  /**
   * Get re-validation history
   * @param limit - Number of records to return
   */
  getRevalidationHistory: (limit: number = 10) =>
    relayApi.get<RevalidationHistoryItem[]>('/sync/revalidation-history', { params: { limit } }),

  /**
   * Trigger daily sync manually
   * Runs full sync workflow (check + reconcile + revalidate)
   */
  triggerDailySync: () => relayApi.post<{ success: boolean; message: string }>('/sync/trigger-daily'),

  // -------------------------------------------------------------------------
  // Auto Reconcile
  // -------------------------------------------------------------------------

  /**
   * Get reconciliation history
   * @param params - Filter parameters (status, triggeredBy, pagination)
   */
  getReconciliationHistory: (params?: {
    limit?: number;
    offset?: number;
    status?: string;
    triggeredBy?: string;
  }) => relayApi.get<ReconciliationHistoryResponse>('/sync/reconcile/history', { params }),

  /**
   * Get reconciliation details
   * Includes summary and detailed operation logs
   * @param id - Reconciliation summary ID
   */
  getReconciliationDetails: (id: number) =>
    relayApi.get<ReconciliationDetailsResponse>(`/sync/reconcile/${id}`),
};

// --- Default Export ---

export default relayApi;
