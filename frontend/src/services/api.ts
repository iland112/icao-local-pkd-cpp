/**
 * Legacy API Module (Backward Compatibility Layer)
 *
 * @deprecated This file is maintained for backward compatibility only.
 * New code should import from specific API modules:
 * - @/services/relayApi      - Upload, SSE, Sync operations
 * - @/services/pkdApi         - Health, Certificate search, Upload history
 * - @/services/paApi          - PA verification, DG parsing
 * - @/services/monitoringApi  - System metrics, Service health
 * - @/services/authApi        - Authentication, JWT
 * - @/services/auditApi       - Audit logging
 *
 * @version 2.12.0
 */

// --- Re-exports from new API modules (Backward Compatibility) ---

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
  icaoApi,
} from './pkdApi';

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
  icaoApi,
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

// --- Re-exports from newly extracted modules ---

export { paApi } from './paApi';

export {
  monitoringServiceApi,
  type CpuMetrics,
  type MemoryMetrics,
  type DiskMetrics,
  type NetworkMetrics,
  type SystemMetrics,
  type ServiceHealth,
  type SystemOverview,
  type MetricsHistoryItem,
} from './monitoringApi';
