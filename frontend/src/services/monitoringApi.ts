/**
 * Monitoring Service API
 *
 * Handles communication with Monitoring Service (port 8084)
 * Responsibilities:
 * - System metrics (CPU, Memory, Disk, Network)
 * - Service health checks
 * - Metrics history
 *
 * @version 2.12.0
 */

import axios, { type AxiosError, type AxiosResponse } from 'axios';

// --- Types ---

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

// --- Load Monitoring Types ---

export interface NginxStatus {
  activeConnections: number;
  totalRequests: number;
  requestsPerSecond: number;
  reading: number;
  writing: number;
  waiting: number;
}

export interface PoolStats {
  available: number;
  total: number;
  max: number;
}

export interface ServiceLoadMetrics {
  name: string;
  status: string;
  responseTimeMs: number;
  dbPool?: PoolStats;
  ldapPool?: PoolStats;
}

export interface LoadSnapshot {
  timestamp: string;
  nginx: NginxStatus;
  services: ServiceLoadMetrics[];
  system: {
    cpuPercent: number;
    memoryPercent: number;
  };
}

export interface HistoryPoint {
  timestamp: string;
  nginx: {
    activeConnections: number;
    requestsPerSecond: number;
  };
  latency: Record<string, number>;
  system: {
    cpuPercent: number;
    memoryPercent: number;
  };
}

export interface LoadHistory {
  intervalSeconds: number;
  totalPoints: number;
  data: HistoryPoint[];
}

// --- Axios Instance ---

const monitoringClient = axios.create({
  baseURL: '/api/monitoring',
  timeout: 30000,
  headers: {
    'Content-Type': 'application/json',
  },
});

monitoringClient.interceptors.response.use(
  (response: AxiosResponse) => response,
  (error: AxiosError) => {
    if (import.meta.env.DEV) console.error('[Monitoring API Error]:', error.response?.data || error.message);
    return Promise.reject(error);
  }
);

// --- Monitoring Service APIs ---

export const monitoringServiceApi = {
  getHealth: () =>
    monitoringClient.get<{ status: string; uptime?: string }>('/health'),

  getSystemOverview: () =>
    monitoringClient.get<SystemOverview>('/system/overview'),

  getServicesHealth: () =>
    monitoringClient.get<ServiceHealth[]>('/services'),

  getMetricsHistory: (params: { hours?: number; limit?: number }) =>
    monitoringClient.get<MetricsHistoryItem[]>('/system/history', { params }),

  getSystemMetricsLatest: () =>
    monitoringClient.get<SystemMetrics>('/system/latest'),

  getLoadSnapshot: () =>
    monitoringClient.get<LoadSnapshot>('/load'),

  getLoadHistory: (minutes = 30) =>
    monitoringClient.get<LoadHistory>('/load/history', { params: { minutes } }),
};
